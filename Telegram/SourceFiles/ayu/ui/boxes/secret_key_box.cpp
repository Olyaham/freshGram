#include "ayu/ui/boxes/secret_key_box.h"

#include "ayu/ui/boxes/secret_key_emoji.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

#include "styles/style_boxes.h"
#include "styles/style_layers.h"

namespace Ui {
namespace {

constexpr auto kGrid = 12;
constexpr auto kPixel = 14;

[[nodiscard]] QImage MakeIdenticon(const std::vector<uint8_t> &hash) {
	static const QColor kColors[4] = {
		QColor(0xFF, 0xFF, 0xFF),
		QColor(0xD5, 0xE6, 0xF3),
		QColor(0x2D, 0x57, 0x75),
		QColor(0x2F, 0x99, 0xC9),
	};
	auto image = QImage(kGrid, kGrid, QImage::Format_RGB32);
	image.fill(Qt::white);
	auto bit = 0;
	for (auto y = 0; y != kGrid; ++y) {
		for (auto x = 0; x != kGrid; ++x) {
			const auto index = bit / 8;
			auto value = 0;
			if (index < int(hash.size())) {
				value = (hash[index] >> (bit % 8)) & 3;
			}
			image.setPixelColor(x, y, kColors[value]);
			bit += 2;
		}
	}
	return image;
}

[[nodiscard]] QString HexText(const std::vector<uint8_t> &hash) {
	auto result = QString();
	const auto count = std::min<int>(32, int(hash.size()));
	for (auto i = 0; i != count; ++i) {
		if (i != 0) {
			if (i % 8 == 0) {
				result += '\n';
			} else if (i % 4 == 0) {
				result += ' ';
			}
		}
		result += QString("%1").arg(hash[i], 2, 16, QChar('0')).toUpper();
		result += ' ';
	}
	return result.trimmed();
}

[[nodiscard]] QString EmojiText(const std::vector<uint8_t> &hash) {
	if (hash.size() < 36) {
		return QString();
	}
	const auto count = int(sizeof(kSecretEmojis) / sizeof(kSecretEmojis[0]));
	auto result = QString();
	for (auto a = 0; a != 5; ++a) {
		const auto num = ((int(hash[16 + a * 4]) & 0x7f) << 24)
			| ((int(hash[16 + a * 4 + 1]) & 0xff) << 16)
			| ((int(hash[16 + a * 4 + 2]) & 0xff) << 8)
			| (int(hash[16 + a * 4 + 3]) & 0xff);
		if (a != 0) {
			result += ' ';
		}
		result += QString::fromUtf8(kSecretEmojis[num % count]);
	}
	return result;
}

class IdenticonWidget final : public Ui::RpWidget {
public:
	IdenticonWidget(QWidget *parent, QImage image)
	: RpWidget(parent)
	, _image(std::move(image)) {
		resize(kGrid * kPixel, kGrid * kPixel);
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::SmoothPixmapTransform, false);
		p.drawImage(rect(), _image);
	}

private:
	QImage _image;

};

} // namespace

void FillSecretKeyBox(
		not_null<Ui::GenericBox*> box,
		std::vector<uint8_t> keyHash,
		int64 fingerprint) {
	const auto hex = HexText(keyHash);
	const auto emoji = EmojiText(keyHash);
	box->setTitle(tr::ayu_SecretKey());
	box->addButton(tr::lng_group_invite_context_copy(), [=] {
		QGuiApplication::clipboard()->setText(emoji.isEmpty()
			? hex
			: (hex + "\n" + emoji));
		box->closeBox();
	});
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::ayu_SecretKeyIntro(tr::now),
			st::boxLabel),
		st::boxPadding);

	const auto holder = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::boxPadding);
	holder->resize(holder->width(), kGrid * kPixel);
	const auto identicon = Ui::CreateChild<IdenticonWidget>(
		holder,
		MakeIdenticon(keyHash));
	holder->widthValue(
	) | rpl::on_next([=](int width) {
		identicon->move((width - identicon->width()) / 2, 0);
	}, identicon->lifetime());

	const auto hexLabel = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			hex,
			st::boxLabel),
		st::boxPadding);
	hexLabel->setSelectable(true);
	if (!emoji.isEmpty()) {
		const auto emojiLabel = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				emoji,
				st::boxLabel),
			st::boxPadding);
		emojiLabel->setSelectable(true);
	}
	const auto idLabel = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::ayu_SecretKeyText(
				tr::now,
				lt_id,
				QString::number(fingerprint)),
			st::boxLabel),
		st::boxPadding);
	idLabel->setSelectable(true);
}

} // namespace Ui
