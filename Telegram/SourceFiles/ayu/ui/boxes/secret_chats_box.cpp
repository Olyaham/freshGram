#include "ayu/ui/boxes/secret_chats_box.h"

#include "ayu/secret/secret_manager.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "base/unixtime.h"
#include "core/file_utilities.h"
#include "crl/crl_on_main.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "rpl/variable.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"
#include "ui/boxes/confirm_box.h"
#include "ui/click_handler.h"
#include "ui/effects/ripple_animation.h"
#include "ui/empty_userpic.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/format_values.h"
#include "ui/text/text.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_options.h"
#include "ui/toast/toast.h"
#include "ui/userpic_view.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtCore/QDate>
#include <QtCore/QFile>
#include <QtCore/QLocale>
#include <QtCore/QUrl>
#include <QtGui/QClipboard>
#include <QtGui/QCursor>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>
#include <QtGui/QImageReader>
#include <QtGui/QPainterPath>

#include <map>
#include <set>

namespace Ui {
namespace {

using AyuSecret::ChatInfo;
using AyuSecret::ChatState;
using AyuSecret::DeliveryState;
using AyuSecret::MediaType;
using AyuSecret::MessageData;

constexpr auto kChatWidth = 520;
constexpr auto kChatHeight = 600;
constexpr auto kAvatarSize = 40;
constexpr auto kAutoDownloadLimit = int64(10 * 1024 * 1024);

[[nodiscard]] QString Qs(const std::string &value) {
	return QString::fromStdString(value);
}

[[nodiscard]] QString TtlText(int seconds) {
	if (seconds <= 0) {
		return tr::ayu_SecretTimerOff(tr::now);
	} else if (seconds < 60) {
		return QString("%1 s").arg(seconds);
	} else if (seconds < 3600) {
		return QString("%1 min").arg(seconds / 60);
	} else if (seconds < 86400) {
		return QString("%1 h").arg(seconds / 3600);
	} else if (seconds < 7 * 86400) {
		return QString("%1 d").arg(seconds / 86400);
	}
	return QString("%1 w").arg(seconds / (7 * 86400));
}

[[nodiscard]] QString CountdownText(int seconds) {
	if (seconds < 0) {
		seconds = 0;
	}
	if (seconds < 60) {
		return QString("%1 s").arg(seconds);
	} else if (seconds < 3600) {
		return QString("%1:%2")
			.arg(seconds / 60)
			.arg(seconds % 60, 2, 10, QChar('0'));
	}
	return QString("%1:%2:%3")
		.arg(seconds / 3600)
		.arg((seconds / 60) % 60, 2, 10, QChar('0'))
		.arg(seconds % 60, 2, 10, QChar('0'));
}

[[nodiscard]] QString DurationText(int seconds) {
	return QString("%1:%2")
		.arg(seconds / 60)
		.arg(seconds % 60, 2, 10, QChar('0'));
}

[[nodiscard]] QString StatusText(ChatState state) {
	switch (state) {
	case ChatState::Requested: return tr::ayu_SecretChatRequest(tr::now);
	case ChatState::Waiting: return tr::ayu_SecretChatWaiting(tr::now);
	case ChatState::Discarded: return tr::ayu_SecretChatEnded(tr::now);
	case ChatState::Ready: return QString();
	}
	return QString();
}

[[nodiscard]] QString Preview(const MessageData &data) {
	const auto &media = data.media;
	auto label = QString();
	switch (media.type) {
	case MediaType::Photo: label = "Photo"; break;
	case MediaType::Video: label = "Video"; break;
	case MediaType::Voice: label = "Voice message"; break;
	case MediaType::Audio: label = "Audio"; break;
	case MediaType::Sticker: label = Qs(media.emoji) + " Sticker"; break;
	case MediaType::Animation: label = "GIF"; break;
	case MediaType::Location:
	case MediaType::Venue: label = "Location"; break;
	case MediaType::Contact: label = "Contact"; break;
	case MediaType::Document:
	case MediaType::External:
		label = media.fileName.empty() ? QString("File") : Qs(media.fileName);
		break;
	default: break;
	}
	const auto text = Qs(data.text).simplified();
	if (label.isEmpty()) {
		return text;
	}
	return text.isEmpty() ? label : (label + ", " + text);
}

[[nodiscard]] ::EntityType TextEntityType(int type) {
	switch (AyuSecret::EntityType(type)) {
	case AyuSecret::EntityType::Mention: return ::EntityType::Mention;
	case AyuSecret::EntityType::Hashtag: return ::EntityType::Hashtag;
	case AyuSecret::EntityType::BotCommand: return ::EntityType::BotCommand;
	case AyuSecret::EntityType::Url: return ::EntityType::Url;
	case AyuSecret::EntityType::Email: return ::EntityType::Email;
	case AyuSecret::EntityType::Bold: return ::EntityType::Bold;
	case AyuSecret::EntityType::Italic: return ::EntityType::Italic;
	case AyuSecret::EntityType::Code: return ::EntityType::Code;
	case AyuSecret::EntityType::Pre: return ::EntityType::Pre;
	case AyuSecret::EntityType::TextUrl: return ::EntityType::CustomUrl;
	case AyuSecret::EntityType::Phone: return ::EntityType::Phone;
	case AyuSecret::EntityType::Cashtag: return ::EntityType::Cashtag;
	case AyuSecret::EntityType::BankCard: return ::EntityType::BankCard;
	case AyuSecret::EntityType::Underline: return ::EntityType::Underline;
	case AyuSecret::EntityType::Strike: return ::EntityType::StrikeOut;
	case AyuSecret::EntityType::Blockquote: return ::EntityType::Blockquote;
	case AyuSecret::EntityType::Spoiler: return ::EntityType::Spoiler;
	default: return ::EntityType::Invalid;
	}
}

[[nodiscard]] TextWithEntities BuildText(const MessageData &data) {
	auto result = TextWithEntities();
	const auto &media = data.media;
	const auto append = [&](const QString &line, const QString &url) {
		if (line.isEmpty()) {
			return;
		}
		if (!result.text.isEmpty()) {
			result.text += '\n';
		}
		const auto offset = result.text.size();
		result.text += line;
		if (!url.isEmpty()) {
			result.entities.push_back(EntityInText(
				::EntityType::CustomUrl,
				offset,
				line.size(),
				url));
		}
	};
	const auto geo = QString("https://www.openstreetmap.org/?mlat=%1&mlon=%2#map=16/%1/%2")
		.arg(media.latitude, 0, 'f', 6)
		.arg(media.longitude, 0, 'f', 6);
	switch (media.type) {
	case MediaType::Location:
		append("Location", geo);
		append(
			QString("%1, %2")
				.arg(media.latitude, 0, 'f', 5)
				.arg(media.longitude, 0, 'f', 5),
			QString());
		break;
	case MediaType::Venue:
		append(Qs(media.title), geo);
		append(Qs(media.address), QString());
		break;
	case MediaType::Contact:
		append(
			(Qs(media.firstName) + " " + Qs(media.lastName)).trimmed(),
			QString());
		append(Qs(media.phone), QString());
		break;
	case MediaType::WebPage:
		append(Qs(media.url), Qs(media.url));
		break;
	default:
		break;
	}
	auto text = Qs(data.text);
	auto entities = data.entities;
	if (text.isEmpty()) {
		text = Qs(media.caption);
		entities.clear();
	}
	if (text.isEmpty()) {
		return result;
	}
	auto shift = 0;
	if (!result.text.isEmpty()) {
		result.text += '\n';
		shift = result.text.size();
	}
	result.text += text;
	for (const auto &entity : entities) {
		const auto type = TextEntityType(entity.type);
		if (type == ::EntityType::Invalid
			|| entity.length <= 0
			|| entity.offset < 0
			|| entity.offset + entity.length > text.size()) {
			continue;
		}
		result.entities.push_back(EntityInText(
			type,
			shift + entity.offset,
			entity.length,
			Qs(entity.extra)));
	}
	return result;
}

[[nodiscard]] bool IsImageType(MediaType type) {
	return (type == MediaType::Photo)
		|| (type == MediaType::Video)
		|| (type == MediaType::Animation)
		|| (type == MediaType::Sticker);
}

[[nodiscard]] bool IsFileType(MediaType type) {
	return (type == MediaType::Document)
		|| (type == MediaType::Audio)
		|| (type == MediaType::Voice)
		|| (type == MediaType::External);
}

[[nodiscard]] bool HasFile(const MessageData &data) {
	return !data.media.path.empty() && QFile::exists(Qs(data.media.path));
}

[[nodiscard]] QSize MediaSize(const MessageData &data, int inner) {
	const auto &media = data.media;
	if (IsImageType(media.type)) {
		auto w = media.width;
		auto h = media.height;
		if (w <= 0 || h <= 0) {
			w = media.thumbWidth;
			h = media.thumbHeight;
		}
		if (w <= 0 || h <= 0) {
			w = 240;
			h = 160;
		}
		const auto sticker = (media.type == MediaType::Sticker);
		const auto maxW = std::min(inner, sticker ? 180 : 320);
		const auto maxH = sticker ? 180 : 320;
		const auto scale = std::min(
			double(maxW) / w,
			double(maxH) / h);
		return QSize(
			std::max(int(w * scale), 90),
			std::max(int(h * scale), 60));
	} else if (IsFileType(media.type)) {
		return QSize(std::min(inner, 280), 52);
	}
	return QSize();
}

[[nodiscard]] QPixmap MakePixmap(
		const MessageData &data,
		QSize size,
		bool blur) {
	const auto ratio = style::DevicePixelRatio();
	const auto target = size * ratio;
	auto image = QImage();
	if (!blur && HasFile(data)) {
		auto reader = QImageReader(Qs(data.media.path));
		if (reader.canRead()) {
			const auto source = reader.size();
			if (source.isValid()
				&& (source.width() > target.width() * 2
					|| source.height() > target.height() * 2)) {
				reader.setScaledSize(source.scaled(
					target * 2,
					Qt::KeepAspectRatio));
			}
			image = reader.read();
		}
	}
	if (image.isNull() && !data.media.thumb.empty()) {
		image = QImage::fromData(
			data.media.thumb.data(),
			int(data.media.thumb.size()));
	}
	if (image.isNull()) {
		return QPixmap();
	}
	if (blur) {
		image = image.scaled(
			14,
			14,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	image = image.scaled(
		target,
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	if (image.size() != target) {
		const auto left = (image.width() - target.width()) / 2;
		const auto top = (image.height() - target.height()) / 2;
		image = image.copy(QRect(QPoint(left, top), target));
	}
	auto result = QPixmap::fromImage(std::move(image));
	result.setDevicePixelRatio(ratio);
	return result;
}

class ChatRow final : public Ui::RippleButton {
public:
	ChatRow(
		QWidget *parent,
		not_null<Main::Session*> session,
		const ChatInfo &info)
	: RippleButton(parent, st::defaultRippleAnimation)
	, _session(session)
	, _info(info)
	, _title(info.title)
	, _peer(session->data().userLoaded(UserId(info.peerUserId))) {
		if (_peer) {
			_view = _peer->createUserpicView();
			_peer->loadUserpic();
		}
		session->downloaderTaskFinished(
		) | rpl::on_next([=] {
			update();
		}, lifetime());
	}

private:
	int resizeGetHeight(int newWidth) override {
		return kAvatarSize + 2 * st::boxLittleSkip + st::boxLittleSkip;
	}

	[[nodiscard]] QString subtitle() const {
		if (_info.typing && _info.state == ChatState::Ready) {
			return tr::ayu_SecretTyping(tr::now);
		}
		const auto status = StatusText(_info.state);
		if (!status.isEmpty()) {
			return status;
		}
		return _info.lastText.simplified();
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		paintRipple(p, 0, 0);

		const auto left = st::boxRowPadding.left();
		const auto right = st::boxRowPadding.right();
		const auto top = (height() - kAvatarSize) / 2;
		if (_peer) {
			_peer->paintUserpicLeft(p, _view, left, top, width(), kAvatarSize);
		} else {
			PainterHighQualityEnabler hq(p);
			Ui::EmptyUserpic(
				Ui::EmptyUserpic::UserpicColor(
					Ui::EmptyUserpic::ColorIndex(_info.peerUserId)),
				_title).paintCircle(p, left, top, width(), kAvatarSize);
		}

		const auto textLeft = left + kAvatarSize + st::boxLittleSkip * 2;
		auto inner = width() - textLeft - right;
		const auto lineTop = top + (kAvatarSize
			- st::semiboldFont->height
			- st::normalFont->height) / 2;

		if (_info.lastDate > 0) {
			const auto date = base::unixtime::parse(_info.lastDate);
			const auto time = (date.date() == QDate::currentDate())
				? date.time().toString("HH:mm")
				: QLocale().toString(date.date(), "d MMM");
			const auto timeWidth = st::normalFont->width(time);
			p.setFont(st::normalFont);
			p.setPen(st::windowSubTextFg);
			p.drawTextLeft(
				width() - right - timeWidth,
				lineTop,
				width(),
				time);
			inner -= timeWidth + st::boxLittleSkip;
		}

		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		p.drawTextLeft(
			textLeft,
			lineTop,
			width(),
			st::semiboldFont->elided(_title, inner));

		auto subInner = textLeft ? (width() - textLeft - right) : 0;
		if (_info.unread > 0) {
			const auto badge = QString::number(_info.unread);
			const auto badgeWidth = std::max(
				st::semiboldFont->width(badge) + 12,
				st::semiboldFont->height);
			const auto badgeLeft = width() - right - badgeWidth;
			const auto badgeTop = lineTop + st::semiboldFont->height + 1;
			PainterHighQualityEnabler hq(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgActive);
			p.drawRoundedRect(
				QRect(
					badgeLeft,
					badgeTop,
					badgeWidth,
					st::normalFont->height),
				st::normalFont->height / 2,
				st::normalFont->height / 2);
			p.setFont(st::semiboldFont);
			p.setPen(st::windowFgActive);
			p.drawText(
				QRect(
					badgeLeft,
					badgeTop,
					badgeWidth,
					st::normalFont->height),
				Qt::AlignCenter,
				badge);
			subInner -= badgeWidth + st::boxLittleSkip;
		}

		const auto typing = _info.typing && _info.state == ChatState::Ready;
		p.setFont(st::normalFont);
		p.setPen(typing ? st::windowActiveTextFg : st::windowSubTextFg);
		p.drawTextLeft(
			textLeft,
			lineTop + st::semiboldFont->height,
			width(),
			st::normalFont->elided(subtitle(), subInner));
	}

	const not_null<Main::Session*> _session;
	ChatInfo _info;
	QString _title;
	UserData *_peer = nullptr;
	Ui::PeerUserpicView _view;

};

class MessagesView final : public Ui::RpWidget {
public:
	MessagesView(
		QWidget *parent,
		not_null<Main::Session*> session,
		int chatId,
		Fn<void(int64_t)> reply,
		Fn<void(int)> scrollTo)
	: RpWidget(parent)
	, _session(session)
	, _chatId(chatId)
	, _reply(std::move(reply))
	, _scrollTo(std::move(scrollTo)) {
		setMouseTracking(true);
		_tick.setCallback([=] {
			if (_hasCountdown) {
				update();
			}
		});
		_tick.callEach(1000);
	}

	void setPeerName(const QString &name) {
		_peerName = name;
	}

	void setReplyCallback(Fn<void(int64_t)> reply) {
		_reply = std::move(reply);
	}

	void setMessages(std::vector<MessageData> messages, bool ready) {
		_ready = ready;
		auto old = std::move(_items);
		_items.clear();
		_items.reserve(messages.size());
		_hasCountdown = false;
		auto pending = std::vector<int64_t>();
		auto reveal = std::vector<MessageData>();
		auto &manager = AyuSecret::Get(_session);
		for (auto &message : messages) {
			auto item = Item();
			item.data = std::move(message);
			item.hasFile = HasFile(item.data);
			item.progress = manager.progress(_chatId, item.data.randomId);
			item.text.setMarkedText(
				st::messageTextStyle,
				BuildText(item.data),
				kMarkupTextOptions);
			item.blur = !item.data.outgoing
				&& item.data.ttl > 0
				&& !item.data.opened
				&& IsImageType(item.data.media.type);
			if (item.data.expiresAt > 0) {
				_hasCountdown = true;
			}
			const auto id = item.data.randomId;
			for (auto &previous : old) {
				if (previous.data.randomId == id) {
					item.cache = std::move(previous.cache);
					break;
				}
			}
			if (!item.data.outgoing
				&& item.data.media.fileId
				&& !item.hasFile
				&& item.progress < 0) {
				const auto media = item.data.media.type;
				const auto small = item.data.media.size <= kAutoDownloadLimit;
				const auto automatic = small
					&& item.data.ttl <= 0
					&& (media == MediaType::Photo
						|| media == MediaType::Sticker);
				if (automatic && _requested.emplace(id).second) {
					pending.push_back(id);
				}
			}
			if (item.hasFile && _openAfter.erase(id)) {
				reveal.push_back(item.data);
			}
			_items.push_back(std::move(item));
		}
		resizeToWidth(width());
		update();
		if (!pending.empty() || !reveal.empty()) {
			const auto chatId = _chatId;
			const auto session = _session;
			crl::on_main(this, [=] {
				auto &manager = AyuSecret::Get(session);
				for (const auto id : pending) {
					manager.downloadMedia(chatId, id);
				}
				for (const auto &data : reveal) {
					manager.openMessage(chatId, data.randomId);
					if (!IsImageType(data.media.type)) {
						File::Launch(Qs(data.media.path));
					}
				}
			});
		}
	}

	[[nodiscard]] std::pair<QString, QString> describe(int64_t id) const {
		for (const auto &item : _items) {
			if (item.data.randomId == id) {
				return {
					item.data.outgoing
						? tr::ayu_SecretYou(tr::now)
						: _peerName,
					Preview(item.data),
				};
			}
		}
		return { QString(), QString() };
	}

protected:
	int resizeGetHeight(int newWidth) override {
		return layoutItems(newWidth);
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		const auto clip = e->rect();
		if (_items.empty()) {
			p.setFont(st::normalFont);
			p.setPen(st::windowSubTextFg);
			p.drawText(
				rect(),
				Qt::AlignCenter,
				tr::ayu_SecretNoMessages(tr::now));
			return;
		}
		const auto now = base::unixtime::now();
		for (auto &item : _items) {
			const auto first = (item.dayTop >= 0) ? item.dayTop : item.top;
			if (first > clip.bottom()) {
				break;
			}
			if (item.top + item.height < clip.top()) {
				continue;
			}
			paintItem(p, item, now);
		}
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		const auto hit = hitTest(e->pos());
		setCursor((hit.kind == Hit::Kind::None
			|| hit.kind == Hit::Kind::Bubble)
			? style::cur_default
			: style::cur_pointer);
	}

	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			_pressed = hitTest(e->pos());
		}
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		if (e->button() != Qt::LeftButton) {
			return;
		}
		const auto pressed = std::exchange(_pressed, Hit());
		const auto hit = hitTest(e->pos());
		if (pressed.index < 0
			|| pressed.index != hit.index
			|| pressed.kind != hit.kind) {
			return;
		}
		const auto &item = _items[hit.index];
		switch (hit.kind) {
		case Hit::Kind::Link:
			if (hit.link) {
				hit.link->onClick(ClickContext{ Qt::LeftButton });
			}
			break;
		case Hit::Kind::Reply:
			for (const auto &other : _items) {
				if (other.data.randomId == item.data.replyTo) {
					if (_scrollTo) {
						_scrollTo(y() + other.top);
					}
					break;
				}
			}
			break;
		case Hit::Kind::Media:
			activateMedia(item.data);
			break;
		default:
			break;
		}
	}

	void contextMenuEvent(QContextMenuEvent *e) override {
		const auto hit = hitTest(e->pos());
		if (hit.index < 0) {
			return;
		}
		const auto &item = _items[hit.index];
		if (item.data.special) {
			return;
		}
		const auto data = item.data;
		const auto text = BuildText(data).text;
		_menu = base::make_unique_q<Ui::PopupMenu>(
			this,
			st::popupMenuWithIcons);
		if (_ready && _reply) {
			_menu->addAction(
				tr::ayu_SecretReply(tr::now),
				[=] { _reply(data.randomId); },
				&st::menuIconReply);
		}
		if (!text.isEmpty()) {
			_menu->addAction(
				tr::ayu_SecretCopy(tr::now),
				[=] { QGuiApplication::clipboard()->setText(text); },
				&st::menuIconCopy);
		}
		if (data.media.type != MediaType::None && IsImageType(data.media.type)
			|| IsFileType(data.media.type)) {
			_menu->addAction(
				HasFile(data)
					? tr::ayu_SecretOpenFile(tr::now)
					: tr::ayu_SecretDownloadFile(tr::now),
				[=] { activateMedia(data); },
				&st::menuIconDownload);
		}
		const auto chatId = _chatId;
		const auto session = _session;
		_menu->addAction(
			tr::ayu_SecretDeleteMessage(tr::now),
			[=] {
				AyuSecret::Get(session).deleteMessages(
					chatId,
					{ data.randomId });
			},
			&st::menuIconDelete);
		_menu->popup(e->globalPos());
	}

	void leaveEventHook(QEvent *e) override {
		setCursor(style::cur_default);
	}

private:
	struct Cache {
		QPixmap pixmap;
		QString path;
		QSize size;
		bool blur = false;
		bool valid = false;
	};

	struct Item {
		MessageData data;
		Ui::Text::String text;
		Cache cache;
		bool hasFile = false;
		bool blur = false;
		double progress = -1.;
		int top = 0;
		int height = 0;
		int dayTop = -1;
		QString dayText;
		QRect bubble;
		QRect replyRect;
		QRect mediaRect;
		QRect textRect;
		QString replyName;
		QString replyText;
		bool hasReply = false;
		int footerWidth = 0;
		int footerTop = 0;
	};

	struct Hit {
		enum class Kind {
			None,
			Bubble,
			Reply,
			Media,
			Link,
		};
		int index = -1;
		Kind kind = Kind::None;
		ClickHandlerPtr link;
	};

	[[nodiscard]] QString footerText(const Item &item, int now) const {
		const auto &data = item.data;
		auto result = QString();
		if (data.expiresAt > 0) {
			result += CountdownText(data.expiresAt - now) + "  ";
		} else if (data.ttl > 0) {
			result += TtlText(data.ttl) + "  ";
		}
		result += base::unixtime::parse(data.date).time().toString("HH:mm");
		if (data.outgoing) {
			switch (data.state) {
			case DeliveryState::Pending: result += "  ..."; break;
			case DeliveryState::Sent: result += "  ✓"; break;
			case DeliveryState::Read: result += "  ✓✓"; break;
			case DeliveryState::Failed: result += "  !"; break;
			}
		}
		return result;
	}

	int layoutItems(int width) {
		constexpr auto kSide = 10;
		constexpr auto kPadH = 10;
		constexpr auto kPadV = 6;
		const auto line = st::normalFont->height;
		const auto maxBubble = std::max(
			std::min(width - 2 * kSide - 36, 420),
			160);
		const auto inner = maxBubble - 2 * kPadH;
		auto top = 8;
		auto lastDay = QDate();
		for (auto &item : _items) {
			item.dayTop = -1;
			item.hasReply = false;
			const auto date = base::unixtime::parse(item.data.date);
			if (date.date() != lastDay) {
				lastDay = date.date();
				item.dayText = QLocale().toString(date.date(), "d MMMM");
				item.dayTop = top;
				top += line + 14;
			}
			item.top = top;
			if (item.data.special) {
				const auto text = Qs(item.data.text);
				const auto w = std::min(
					width - 2 * kSide,
					st::normalFont->width(text) + 2 * kPadH);
				item.bubble = QRect(
					(width - w) / 2,
					top,
					w,
					line + 2 * kPadV);
				item.height = item.bubble.height();
				top += item.height + 6;
				continue;
			}
			auto contentWidth = 0;
			const auto footer = footerText(item, base::unixtime::now());
			item.footerWidth = st::normalFont->width(footer) + 8;
			contentWidth = std::max(contentWidth, item.footerWidth);

			if (item.data.replyTo) {
				item.hasReply = true;
				const auto info = describe(item.data.replyTo);
				item.replyName = info.first.isEmpty()
					? QString("Message")
					: info.first;
				item.replyText = info.second;
				const auto w = std::min(
					inner,
					std::max(
						st::semiboldFont->width(item.replyName),
						st::normalFont->width(item.replyText)) + 10);
				contentWidth = std::max(contentWidth, w);
			}
			const auto mediaSize = MediaSize(item.data, inner);
			if (!mediaSize.isEmpty()) {
				contentWidth = std::max(contentWidth, mediaSize.width());
			}
			const auto hasText = !item.text.isEmpty();
			if (hasText) {
				contentWidth = std::max(
					contentWidth,
					std::min(inner, item.text.maxWidth()));
			}
			auto y = kPadV;
			if (item.hasReply) {
				item.replyRect = QRect(
					kPadH,
					y,
					std::min(contentWidth, inner),
					2 * line);
				y += 2 * line + 4;
			}
			if (!mediaSize.isEmpty()) {
				item.mediaRect = QRect(QPoint(kPadH, y), mediaSize);
				y += mediaSize.height() + 4;
			} else {
				item.mediaRect = QRect();
			}
			if (hasText) {
				const auto textHeight = item.text.countHeight(contentWidth);
				item.textRect = QRect(kPadH, y, contentWidth, textHeight);
				y += textHeight + 2;
			} else {
				item.textRect = QRect();
			}
			const auto footerTop = y;
			y += line;
			const auto bubbleWidth = contentWidth + 2 * kPadH;
			const auto bubbleHeight = y + kPadV;
			item.bubble = QRect(
				item.data.outgoing ? (width - kSide - bubbleWidth) : kSide,
				top,
				bubbleWidth,
				bubbleHeight);
			item.replyRect.translate(item.bubble.topLeft());
			item.mediaRect.translate(item.bubble.topLeft());
			item.textRect.translate(item.bubble.topLeft());
			item.footerTop = item.bubble.top() + footerTop;
			item.height = bubbleHeight;
			top += bubbleHeight + 6;
			if (!mediaSize.isEmpty() && IsImageType(item.data.media.type)) {
				ensurePixmap(item);
			}
		}
		return std::max(top + 8, st::boxWidth);
	}

	void ensurePixmap(Item &item) {
		const auto size = item.mediaRect.size();
		const auto path = Qs(item.data.media.path);
		if (item.cache.valid
			&& item.cache.size == size
			&& item.cache.blur == item.blur
			&& item.cache.path == path
			&& (!item.cache.pixmap.isNull() || !item.hasFile)) {
			return;
		}
		item.cache.pixmap = MakePixmap(item.data, size, item.blur);
		item.cache.size = size;
		item.cache.blur = item.blur;
		item.cache.path = path;
		item.cache.valid = true;
	}

	void activateMedia(const MessageData &data) {
		auto &manager = AyuSecret::Get(_session);
		if (!HasFile(data)) {
			if (manager.progress(_chatId, data.randomId) >= 0) {
				return;
			}
			if (data.ttl > 0 && !data.outgoing) {
				_openAfter.emplace(data.randomId);
			}
			manager.downloadMedia(_chatId, data.randomId);
			return;
		}
		if (!data.outgoing && data.ttl > 0 && !data.opened) {
			manager.openMessage(_chatId, data.randomId);
			if (IsImageType(data.media.type)
				&& data.media.type == MediaType::Photo) {
				return;
			}
		}
		File::Launch(Qs(data.media.path));
	}

	[[nodiscard]] Hit hitTest(QPoint point) const {
		auto result = Hit();
		for (auto i = 0; i != int(_items.size()); ++i) {
			const auto &item = _items[i];
			if (item.data.special || !item.bubble.contains(point)) {
				continue;
			}
			result.index = i;
			result.kind = Hit::Kind::Bubble;
			if (item.hasReply && item.replyRect.contains(point)) {
				result.kind = Hit::Kind::Reply;
			} else if (!item.mediaRect.isEmpty()
				&& item.mediaRect.contains(point)) {
				result.kind = Hit::Kind::Media;
			} else if (!item.textRect.isEmpty()
				&& item.textRect.contains(point)) {
				const auto state = item.text.getState(
					point - item.textRect.topLeft(),
					item.textRect.width(),
					Ui::Text::StateRequest());
				if (state.link) {
					result.kind = Hit::Kind::Link;
					result.link = state.link;
				}
			}
			break;
		}
		return result;
	}

	void paintCircle(
			QPainter &p,
			QPoint center,
			int diameter,
			const QBrush &bg,
			const QPen &fg,
			const QString &glyph) {
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		const auto rect = QRect(
			center.x() - diameter / 2,
			center.y() - diameter / 2,
			diameter,
			diameter);
		p.drawEllipse(rect);
		p.setFont(st::semiboldFont);
		p.setPen(fg);
		p.drawText(rect, Qt::AlignCenter, glyph);
	}

	[[nodiscard]] QString mediaGlyph(const Item &item) const {
		if (item.progress >= 0.) {
			return QString("%1%").arg(int(item.progress * 100));
		}
		if (!item.hasFile) {
			return QString::fromUtf8("↓");
		}
		switch (item.data.media.type) {
		case MediaType::Video:
		case MediaType::Animation:
		case MediaType::Voice:
		case MediaType::Audio: return QString::fromUtf8("▶");
		default: return QString::fromUtf8("↗");
		}
	}

	void paintItem(Painter &p, Item &item, int now) {
		if (item.dayTop >= 0) {
			const auto w = st::semiboldFont->width(item.dayText) + 20;
			const auto r = QRect(
				(width() - w) / 2,
				item.dayTop,
				w,
				st::normalFont->height + 4);
			PainterHighQualityEnabler hq(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgServiceBg);
			p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
			p.setFont(st::semiboldFont);
			p.setPen(st::msgServiceFg);
			p.drawText(r, Qt::AlignCenter, item.dayText);
		}
		const auto &data = item.data;
		if (data.special) {
			PainterHighQualityEnabler hq(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgServiceBg);
			p.drawRoundedRect(item.bubble, 10, 10);
			p.setFont(st::normalFont);
			p.setPen(st::msgServiceFg);
			p.drawText(
				item.bubble,
				Qt::AlignCenter,
				st::normalFont->elided(
					Qs(data.text),
					item.bubble.width() - 20));
			return;
		}
		const auto outgoing = data.outgoing;
		{
			PainterHighQualityEnabler hq(p);
			p.setPen(Qt::NoPen);
			p.setBrush(outgoing ? st::msgOutBg : st::windowBgOver);
			p.drawRoundedRect(item.bubble, 12, 12);
		}
		const auto line = st::normalFont->height;
		if (item.hasReply) {
			const auto r = item.replyRect;
			p.fillRect(
				QRect(r.left(), r.top(), 2, r.height()),
				outgoing
					? st::msgOutReplyBarColor
					: st::msgInReplyBarColor);
			p.setFont(st::semiboldFont);
			p.setPen(outgoing
				? st::msgOutReplyBarColor
				: st::msgInReplyBarColor);
			p.drawTextLeft(
				r.left() + 8,
				r.top(),
				width(),
				st::semiboldFont->elided(item.replyName, r.width() - 8));
			p.setFont(st::normalFont);
			p.setPen(outgoing
				? st::historyTextOutFg
				: st::historyTextInFg);
			p.drawTextLeft(
				r.left() + 8,
				r.top() + line,
				width(),
				st::normalFont->elided(item.replyText, r.width() - 8));
		}
		if (!item.mediaRect.isEmpty()) {
			paintMedia(p, item);
		}
		if (!item.textRect.isEmpty()) {
			p.setPen(outgoing ? st::historyTextOutFg : st::historyTextInFg);
			item.text.draw(p, {
				.position = item.textRect.topLeft(),
				.outerWidth = width(),
				.availableWidth = item.textRect.width(),
				.palette = outgoing
					? &st::outTextPalette
					: &st::inTextPalette,
				.spoiler = Ui::Text::DefaultSpoilerCache(),
				.now = crl::now(),
			});
		}
		const auto footer = footerText(item, now);
		p.setFont(st::normalFont);
		p.setPen(outgoing ? st::msgOutDateFg : st::msgInDateFg);
		p.drawText(
			QRect(
				item.bubble.left() + 10,
				item.footerTop,
				item.bubble.width() - 20,
				line),
			Qt::AlignRight | Qt::AlignVCenter,
			footer);
	}

	void paintMedia(Painter &p, Item &item) {
		const auto &data = item.data;
		const auto outgoing = data.outgoing;
		const auto r = item.mediaRect;
		if (IsImageType(data.media.type)) {
			ensurePixmap(item);
			PainterHighQualityEnabler hq(p);
			auto path = QPainterPath();
			path.addRoundedRect(QRectF(r), 8, 8);
			p.save();
			p.setClipPath(path);
			if (item.cache.pixmap.isNull()) {
				p.fillRect(r, st::windowShadowFg);
			} else {
				p.drawPixmap(r.topLeft(), item.cache.pixmap);
			}
			if (item.blur) {
				p.fillRect(r, QColor(0, 0, 0, 90));
			}
			p.restore();
			const auto isVideo = (data.media.type == MediaType::Video)
				|| (data.media.type == MediaType::Animation);
			const auto transferring = (item.progress >= 0.);
			if (item.blur) {
				p.setFont(st::semiboldFont);
				p.setPen(QColor(255, 255, 255));
				p.drawText(
					r,
					Qt::AlignCenter,
					tr::ayu_SecretTapToView(tr::now)
						+ "\n"
						+ TtlText(data.ttl));
			} else if (!item.hasFile || transferring || isVideo) {
				paintCircle(
					p,
					r.center(),
					48,
					QColor(0, 0, 0, 120),
					QColor(255, 255, 255),
					mediaGlyph(item));
			}
			return;
		}
		const auto circle = 40;
		paintCircle(
			p,
			QPoint(r.left() + circle / 2, r.top() + r.height() / 2),
			circle,
			outgoing ? st::msgFileOutBg : st::msgFileInBg,
			outgoing ? st::historyFileOutIconFg : st::historyFileInIconFg,
			mediaGlyph(item));
		const auto textLeft = r.left() + circle + 10;
		const auto textWidth = r.width() - circle - 10;
		auto name = Qs(data.media.fileName);
		if (name.isEmpty()) {
			switch (data.media.type) {
			case MediaType::Voice: name = "Voice message"; break;
			case MediaType::Audio: name = "Audio"; break;
			default: name = "File"; break;
			}
		}
		auto details = QString();
		if (data.media.type == MediaType::Voice
			|| data.media.type == MediaType::Audio) {
			details = DurationText(data.media.duration);
		}
		if (data.media.size > 0) {
			if (!details.isEmpty()) {
				details += ", ";
			}
			details += Ui::FormatSizeText(data.media.size);
		}
		p.setFont(st::semiboldFont);
		p.setPen(outgoing ? st::historyTextOutFg : st::historyTextInFg);
		p.drawTextLeft(
			textLeft,
			r.top() + 6,
			width(),
			st::semiboldFont->elided(name, textWidth));
		p.setFont(st::normalFont);
		p.setPen(outgoing ? st::msgOutDateFg : st::msgInDateFg);
		p.drawTextLeft(
			textLeft,
			r.top() + 6 + st::semiboldFont->height + 2,
			width(),
			st::normalFont->elided(details, textWidth));
	}

	const not_null<Main::Session*> _session;
	const int _chatId;
	Fn<void(int64_t)> _reply;
	Fn<void(int)> _scrollTo;
	std::vector<Item> _items;
	std::set<int64_t> _requested;
	std::set<int64_t> _openAfter;
	QString _peerName;
	bool _ready = false;
	bool _hasCountdown = false;
	Hit _pressed;
	base::Timer _tick;
	base::unique_qptr<Ui::PopupMenu> _menu;

};

class Composer final : public Ui::RpWidget {
public:
	Composer(QWidget *parent, Fn<void()> attach)
	: RpWidget(parent)
	, _attach(this, st::historyAttach)
	, _field(
		this,
		st::historyComposeField,
		Ui::InputField::Mode::MultiLine,
		tr::ayu_SecretChatInput()) {
		_attach->setClickedCallback(std::move(attach));
		_field->heightValue(
		) | rpl::on_next([=](int) {
			if (width() > 0) {
				resizeToWidth(width());
			}
		}, lifetime());
	}

	[[nodiscard]] not_null<Ui::InputField*> field() const {
		return _field.data();
	}

	void setAttachCallback(Fn<void()> attach) {
		_attach->setClickedCallback(std::move(attach));
	}

	void setReply(const QString &name, const QString &text) {
		const auto had = _hasReply;
		_hasReply = !name.isEmpty();
		_replyName = name;
		_replyText = text.simplified();
		if (had != _hasReply || _hasReply) {
			resizeToWidth(width());
			update();
		}
	}

	[[nodiscard]] rpl::producer<> replyCancelled() const {
		return _cancelled.events();
	}

	void setEnabledState(bool enabled) {
		_field->setVisible(enabled);
		_attach->setVisible(enabled);
		_enabled = enabled;
		if (width() > 0) {
			resizeToWidth(width());
		}
		update();
	}

protected:
	int resizeGetHeight(int newWidth) override {
		if (!_enabled) {
			return 0;
		}
		const auto skip = st::boxLittleSkip;
		const auto attachWidth = _attach->width();
		_field->resizeToWidth(newWidth - attachWidth - 2 * skip);
		const auto replyHeight = replyAreaHeight();
		const auto rowHeight = std::max(
			_field->height() + 2 * skip,
			_attach->height());
		_attach->moveToLeft(0, replyHeight + rowHeight - _attach->height());
		_field->moveToLeft(
			attachWidth + skip,
			replyHeight + (rowHeight - _field->height()) / 2);
		return replyHeight + rowHeight;
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.fillRect(e->rect(), st::historyComposeAreaBg);
		if (!_hasReply || !_enabled) {
			return;
		}
		const auto left = _attach->width();
		const auto line = st::normalFont->height;
		p.fillRect(QRect(left, 6, 2, 2 * line), st::windowActiveTextFg);
		p.setFont(st::semiboldFont);
		p.setPen(st::windowActiveTextFg);
		const auto available = width() - left - 40;
		p.drawText(
			left + 8,
			6 + st::semiboldFont->ascent,
			st::semiboldFont->elided(
				tr::ayu_SecretReplyTo(
					tr::now,
					lt_name,
					_replyName),
				available));
		p.setFont(st::normalFont);
		p.setPen(st::windowFg);
		p.drawText(
			left + 8,
			6 + line + st::normalFont->ascent,
			st::normalFont->elided(_replyText, available));
		p.setPen(st::windowSubTextFg);
		p.setFont(st::semiboldFont);
		p.drawText(
			QRect(width() - 32, 4, 28, 2 * line),
			Qt::AlignCenter,
			QString::fromUtf8("✕"));
	}

	void mousePressEvent(QMouseEvent *e) override {
		if (_hasReply
			&& e->button() == Qt::LeftButton
			&& e->pos().y() < replyAreaHeight()
			&& e->pos().x() > width() - 40) {
			_cancelled.fire({});
		}
	}

private:
	[[nodiscard]] int replyAreaHeight() const {
		return _hasReply ? (2 * st::normalFont->height + 12) : 0;
	}

	const object_ptr<Ui::IconButton> _attach;
	const object_ptr<Ui::InputField> _field;
	QString _replyName;
	QString _replyText;
	bool _hasReply = false;
	bool _enabled = true;
	rpl::event_stream<> _cancelled;

};

void FillTimerBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		int chatId,
		int current) {
	box->setTitle(tr::ayu_SecretTimer());
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	const auto group = std::make_shared<Ui::RadiobuttonGroup>(current);
	const auto values = std::vector<int>{
		0, 1, 2, 3, 4, 5, 6, 7, 15, 30, 60, 3600, 86400, 7 * 86400
	};
	auto found = false;
	for (const auto value : values) {
		found = found || (value == current);
	}
	auto options = values;
	if (!found && current > 0) {
		options.insert(options.begin() + 1, current);
	}
	for (const auto value : options) {
		box->addRow(
			object_ptr<Ui::Radiobutton>(
				box,
				group,
				value,
				TtlText(value),
				st::defaultCheckbox),
			st::boxRowPadding);
	}
	group->setChangedCallback([=](int value) {
		AyuSecret::Get(session).setTtl(chatId, value);
		box->closeBox();
	});
}

} // namespace

void FillSecretChatsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller) {
	box->setWidth(st::boxWideWidth);
	box->setTitle(tr::ayu_SecretChats());
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });

	const auto session = &controller->session();
	const auto list = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());

	const auto rebuild = box->lifetime().make_state<Fn<void()>>();
	*rebuild = [=] {
		list->clear();
		auto &manager = AyuSecret::Get(session);
		const auto chats = manager.chats();
		if (chats.empty()) {
			list->add(
				object_ptr<Ui::FlatLabel>(
					list,
					tr::ayu_SecretChatsEmpty(tr::now),
					st::boxDividerLabel),
				st::boxRowPadding);
		}
		for (const auto &chat : chats) {
			const auto row = list->add(
				object_ptr<ChatRow>(list, session, chat));
			const auto id = chat.id;
			const auto state = chat.state;
			const auto title = chat.title;
			row->setClickedCallback([=] {
				if (state == ChatState::Requested) {
					controller->show(Ui::MakeConfirmBox({
						.text = tr::ayu_SecretChatRequestAsk(
							tr::now,
							lt_name,
							title),
						.confirmed = [=](Fn<void()> close) {
							AyuSecret::Get(session).accept(id);
							close();
						},
						.cancelled = [=](Fn<void()> close) {
							AyuSecret::Get(session).decline(id);
							close();
						},
						.confirmText = tr::ayu_SecretChatAccept(),
						.cancelText = tr::ayu_SecretChatDecline(),
					}));
				} else {
					controller->show(Box(
						Ui::FillSecretChatBox,
						controller,
						id));
				}
			});
		}
		list->resizeToWidth(box->width());
	};
	AyuSecret::Get(session).changes(
	) | rpl::on_next([=] {
		(*rebuild)();
	}, box->lifetime());
	(*rebuild)();
}

void FillSecretChatBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		int chatId) {
	box->setWidth(kChatWidth);
	box->setMinHeight(kChatHeight);
	const auto session = &controller->session();
	auto &manager = AyuSecret::Get(session);
	manager.setOpenChat(chatId);
	manager.markRead(chatId);
	box->lifetime().add([=] {
		AyuSecret::Get(session).setOpenChat(0);
	});

	struct State {
		int64_t replyTo = 0;
		int count = 0;
		int ttl = 0;
		ChatState chatState = ChatState::Discarded;
		int64_t fingerprint = 0;
		rpl::variable<QString> subtitle;
		base::unique_qptr<Ui::PopupMenu> menu;
		base::Timer tick;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto info = manager.chat(chatId);
	box->setTitle(rpl::single(info ? info->title : QString()));
	box->setAdditionalTitle(state->subtitle.value());

	const auto view = box->addRow(
		object_ptr<MessagesView>(
			box,
			session,
			chatId,
			nullptr,
			[=](int y) { box->scrollToY(y); }),
		style::margins());
	if (info) {
		view->setPeerName(info->title);
	}

	const auto composer = box->setPinnedToBottomContent(
		object_ptr<Composer>(box, nullptr));
	const auto field = composer->field();

	const auto send = [=] {
		auto tags = field->getTextWithAppliedMarkdown();
		if (tags.text.trimmed().isEmpty()) {
			return;
		}
		auto text = TextWithEntities{
			tags.text,
			TextUtilities::ConvertTextTagsToEntities(tags.tags),
		};
		AyuSecret::Get(session).sendText(chatId, std::move(text), state->replyTo);
		state->replyTo = 0;
		composer->setReply(QString(), QString());
		field->setText(QString());
	};

	const auto attach = [=] {
		FileDialog::GetOpenPaths(
			box.get(),
			tr::ayu_SecretChoose(tr::now),
			FileDialog::AllFilesFilter(),
			crl::guard(box, [=](FileDialog::OpenResult &&result) {
				auto caption = field->getLastText().trimmed();
				for (const auto &path : result.paths) {
					AyuSecret::Get(session).sendFile(chatId, path, caption);
					caption.clear();
				}
				if (!result.paths.isEmpty()) {
					field->setText(QString());
				}
			}));
	};
	composer->setAttachCallback(attach);

	const auto setReply = [=](int64_t id) {
		state->replyTo = id;
		const auto described = view->describe(id);
		composer->setReply(
			described.first.isEmpty() ? QString("Message") : described.first,
			described.second);
		field->setFocusFast();
	};
	view->setReplyCallback(setReply);
	composer->replyCancelled(
	) | rpl::on_next([=] {
		state->replyTo = 0;
		composer->setReply(QString(), QString());
	}, composer->lifetime());

	const auto refreshHeader = [=](const ChatInfo &current_) {
		const auto current = &current_;
		state->ttl = current->ttl;
		state->chatState = current->state;
		state->fingerprint = current->fingerprint;
		if (current->state == ChatState::Ready) {
			if (current->typing) {
				state->subtitle = tr::ayu_SecretTyping(tr::now);
			} else if (current->ttl > 0) {
				state->subtitle = tr::ayu_SecretSelfDestruct(
					tr::now,
					lt_time,
					TtlText(current->ttl));
			} else {
				state->subtitle = tr::ayu_SecretEncrypted(tr::now);
			}
		} else {
			state->subtitle = StatusText(current->state);
		}
		composer->setEnabledState(current->state == ChatState::Ready);
	};
	const auto refresh = [=] {
		const auto current = AyuSecret::Get(session).chat(chatId);
		if (!current) {
			box->closeBox();
			return;
		}
		refreshHeader(*current);
		auto messages = AyuSecret::Get(session).messages(chatId);
		const auto count = int(messages.size());
		const auto grew = (count > state->count);
		state->count = count;
		view->setMessages(
			std::move(messages),
			current->state == ChatState::Ready);
		if (grew) {
			box->scrollToY(view->y() + view->height());
		}
	};

	const auto showTimer = [=] {
		controller->show(Box(
			FillTimerBox,
			session,
			chatId,
			state->ttl));
	};
	const auto showKey = [=] {
		controller->show(Box([=](not_null<Ui::GenericBox*> inner) {
			inner->setTitle(tr::ayu_SecretKey());
			inner->addButton(tr::lng_close(), [=] { inner->closeBox(); });
			const auto id = QString("%1").arg(
				quint64(state->fingerprint),
				16,
				16,
				QChar('0')).toUpper();
			inner->addRow(
				object_ptr<Ui::FlatLabel>(
					inner,
					tr::ayu_SecretKeyText(tr::now, lt_id, id),
					st::boxLabel),
				st::boxPadding);
		}));
	};
	const auto clear = [=] {
		controller->show(Ui::MakeConfirmBox({
			.text = tr::ayu_SecretClearAsk(),
			.confirmed = [=](Fn<void()> close) {
				AyuSecret::Get(session).clearHistory(chatId);
				close();
			},
		}));
	};
	const auto end = [=] {
		if (state->chatState == ChatState::Discarded) {
			AyuSecret::Get(session).remove(chatId);
			box->closeBox();
			return;
		}
		controller->show(Ui::MakeConfirmBox({
			.text = tr::ayu_SecretChatEndAsk(),
			.confirmed = [=](Fn<void()> close) {
				AyuSecret::Get(session).discard(chatId);
				close();
			},
		}));
	};
	const auto showOptions = [=] {
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			box,
			st::popupMenuWithIcons);
		const auto menu = state->menu.get();
		const auto ready = (state->chatState == ChatState::Ready);
		if (ready) {
			menu->addAction(
				tr::ayu_SecretTimer(tr::now)
					+ ": "
					+ TtlText(state->ttl),
				showTimer,
				&st::menuIconTTL);
			menu->addAction(
				tr::ayu_SecretKey(tr::now),
				showKey,
				&st::menuIconLock);
			menu->addAction(
				tr::ayu_SecretClear(tr::now),
				clear,
				&st::menuIconClear);
		}
		menu->addAction(
			(state->chatState == ChatState::Discarded)
				? tr::ayu_SecretChatDelete(tr::now)
				: tr::ayu_SecretChatEnd(tr::now),
			end,
			&st::menuIconDeleteAttention);
		menu->popup(QCursor::pos());
	};

	box->addButton(tr::ayu_SecretChatSend(), send);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	box->addLeftButton(tr::ayu_SecretOptions(), showOptions);

	AyuSecret::Get(session).changes(
	) | rpl::on_next([=] {
		AyuSecret::Get(session).markRead(chatId);
		refresh();
	}, box->lifetime());

	field->submits(
	) | rpl::on_next([=](Qt::KeyboardModifiers modifiers) {
		if (!(modifiers & Qt::ShiftModifier)) {
			send();
		}
	}, field->lifetime());
	field->changes(
	) | rpl::on_next([=] {
		if (!field->getLastText().isEmpty()) {
			AyuSecret::Get(session).setTyping(chatId);
		}
	}, field->lifetime());

	state->tick.setCallback([=] {
		if (const auto current = AyuSecret::Get(session).chat(chatId)) {
			refreshHeader(*current);
		}
	});
	state->tick.callEach(2000);

	box->setFocusCallback([=] { field->setFocusFast(); });
	refresh();
	box->scrollToY(view->y() + view->height());
}

} // namespace Ui
