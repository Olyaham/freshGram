#include "ayu/ui/boxes/secret_chats_box.h"

#include "ayu/secret/secret_manager.h"
#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtGui/QFontMetricsF>
#include <QtGui/QTextOption>

namespace Ui {
namespace {

using namespace AyuSecret;

class ChatRow final : public Ui::RippleButton {
public:
	ChatRow(
		QWidget *parent,
		const QString &title,
		const QString &subtitle,
		int unread)
	: RippleButton(parent, st::defaultRippleAnimation)
	, _title(title)
	, _subtitle(subtitle.simplified())
	, _unread(unread) {
	}

private:
	int resizeGetHeight(int newWidth) override {
		return st::semiboldFont->height + st::normalFont->height
			+ 2 * st::boxLittleSkip;
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		paintRipple(p, 0, 0);

		const auto left = st::boxRowPadding.left();
		const auto right = st::boxRowPadding.right();
		auto inner = width() - left - right;
		auto top = st::boxLittleSkip;

		if (_unread > 0) {
			const auto badge = QString::number(_unread);
			const auto badgeWidth = st::semiboldFont->width(badge);
			p.setFont(st::semiboldFont);
			p.setPen(st::windowBgActive);
			p.drawTextLeft(
				width() - right - badgeWidth,
				top,
				width(),
				badge);
			inner -= badgeWidth + left / 2;
		}

		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		p.drawTextLeft(
			left,
			top,
			width(),
			st::semiboldFont->elided(_title, inner));

		top += st::semiboldFont->height;
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawTextLeft(
			left,
			top,
			width(),
			st::normalFont->elided(_subtitle, inner));
	}

	QString _title;
	QString _subtitle;
	int _unread = 0;

};

class MessagesView final : public Ui::RpWidget {
public:
	explicit MessagesView(QWidget *parent) : RpWidget(parent) {
	}

	void setMessages(std::vector<MessageInfo> messages) {
		_messages = std::move(messages);
		resizeToWidth(width());
		update();
	}

private:
	static constexpr auto kPadding = 10;
	static constexpr auto kInner = 8;
	static constexpr auto kSkip = 6;

	[[nodiscard]] QFontMetricsF metrics() const {
		return QFontMetricsF(st::normalFont->f);
	}
	[[nodiscard]] QRectF textRect(const QString &text, int bubbleMax) const {
		return metrics().boundingRect(
			QRectF(0, 0, bubbleMax - 2 * kInner, 100000),
			Qt::TextWordWrap,
			text);
	}

	int resizeGetHeight(int newWidth) override {
		const auto bubbleMax = std::max(newWidth * 3 / 4, 100);
		auto height = kPadding;
		for (const auto &message : _messages) {
			const auto rect = textRect(message.text, bubbleMax);
			height += int(std::ceil(rect.height()))
				+ st::normalFont->height
				+ 2 * kInner
				+ kSkip;
		}
		return std::max(height + kPadding, st::boxWidth / 2);
	}

	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		PainterHighQualityEnabler hq(p);
		const auto bubbleMax = std::max(width() * 3 / 4, 100);
		auto top = kPadding;
		for (const auto &message : _messages) {
			const auto rect = textRect(message.text, bubbleMax);
			const auto textWidth = int(std::ceil(rect.width()));
			const auto textHeight = int(std::ceil(rect.height()));
			const auto time = base::unixtime::parse(message.date)
				.time().toString("HH:mm");
			const auto timeWidth = st::normalFont->width(time);
			const auto contentWidth = std::max(textWidth, timeWidth);
			const auto bubbleWidth = contentWidth + 2 * kInner;
			const auto bubbleHeight = textHeight
				+ st::normalFont->height
				+ 2 * kInner;
			const auto left = message.outgoing
				? (width() - kPadding - bubbleWidth)
				: kPadding;
			p.setPen(Qt::NoPen);
			p.setBrush(message.outgoing
				? st::windowBgActive
				: st::windowBgOver);
			p.drawRoundedRect(
				QRect(left, top, bubbleWidth, bubbleHeight),
				10,
				10);
			p.setFont(st::normalFont);
			p.setPen(message.outgoing
				? st::windowFgActive
				: st::windowFg);
			p.drawText(
				QRect(
					left + kInner,
					top + kInner,
					contentWidth,
					textHeight),
				Qt::TextWordWrap,
				message.text);
			p.setPen(message.outgoing
				? st::windowFgActive
				: st::windowSubTextFg);
			p.drawText(
				QRect(
					left + bubbleWidth - kInner - timeWidth,
					top + kInner + textHeight,
					timeWidth,
					st::normalFont->height),
				Qt::AlignRight,
				time);
			top += bubbleHeight + kSkip;
		}
	}

	std::vector<MessageInfo> _messages;

};

[[nodiscard]] QString StatusText(ChatState state) {
	switch (state) {
	case ChatState::Requested: return tr::ayu_SecretChatRequest(tr::now);
	case ChatState::Waiting: return tr::ayu_SecretChatWaiting(tr::now);
	case ChatState::Discarded: return tr::ayu_SecretChatEnded(tr::now);
	case ChatState::Ready: return QString();
	}
	return QString();
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
			const auto status = StatusText(chat.state);
			const auto subtitle = status.isEmpty()
				? chat.lastText
				: status;
			const auto row = list->add(
				object_ptr<ChatRow>(list, chat.title, subtitle, chat.unread));
			const auto id = chat.id;
			const auto state = chat.state;
			const auto title = chat.title;
			row->setClickedCallback([=] {
				auto &manager = AyuSecret::Get(session);
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
	box->setWidth(st::boxWideWidth);
	box->setMinHeight(st::boxWidth);
	const auto session = &controller->session();
	auto &manager = AyuSecret::Get(session);
	manager.setOpenChat(chatId);
	manager.markRead(chatId);
	box->lifetime().add([=] {
		AyuSecret::Get(session).setOpenChat(0);
	});

	const auto find = [=]() -> std::optional<ChatInfo> {
		for (const auto &chat : AyuSecret::Get(session).chats()) {
			if (chat.id == chatId) {
				return chat;
			}
		}
		return std::nullopt;
	};
	const auto info = find();
	box->setTitle(rpl::single(info ? info->title : QString()));

	const auto status = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			QString(),
			st::boxDividerLabel),
		st::boxRowPadding);
	const auto view = box->addRow(
		object_ptr<MessagesView>(box),
		style::margins());
	const auto field = box->setPinnedToBottomContent(
		object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			tr::ayu_SecretChatInput()));

	const auto send = [=] {
		const auto text = field->getLastText().trimmed();
		if (text.isEmpty()) {
			return;
		}
		AyuSecret::Get(session).send(chatId, text);
		field->setText(QString());
	};
	box->addButton(tr::ayu_SecretChatSend(), send);
	const auto endButton = box->addLeftButton(tr::ayu_SecretChatEnd(), [=] {
		const auto current = find();
		if (!current || current->state == ChatState::Discarded) {
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
	});

	const auto refresh = [=] {
		const auto current = find();
		if (!current) {
			box->closeBox();
			return;
		}
		const auto text = StatusText(current->state);
		status->setText(text);
		if (endButton) {
			endButton->setText(current->state == ChatState::Discarded
				? tr::ayu_SecretChatDelete()
				: tr::ayu_SecretChatEnd());
		}
		status->setVisible(!text.isEmpty());
		field->setVisible(current->state == ChatState::Ready);
		view->setMessages(AyuSecret::Get(session).messages(chatId));
		view->resizeToWidth(box->width());
		box->scrollToY(view->y() + view->height());
	};
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
	box->setFocusCallback([=] { field->setFocusFast(); });
	refresh();
}

} // namespace Ui
