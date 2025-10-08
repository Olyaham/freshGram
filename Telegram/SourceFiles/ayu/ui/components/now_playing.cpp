// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2025
#include "now_playing.h"

#include "info/profile/info_profile_music_button.h"

#include <QSvgRenderer>

#include "ayu/ayu_settings.h"
#include "ayu/ui/utils/color_utils.h"
#include "ayu/ui/utils/palette.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "styles/palette.h"
#include "styles/style_info.h"
#include "ui/painter.h"
#include "ui/image/image.h"
#include "ui/widgets/labels.h"
#include "window/themes/window_theme.h"

class Image;

namespace Info::Profile {

namespace {

QColor performerColor(255, 255, 255, 153); // white 60%

QRgb AdjustHsl(QRgb color, float luminance, float saturation = -1.0f) {
	auto hsl = Ayu::Ui::ColorUtils::colorToHSL(color);

	if (saturation > 0.0f) {
		hsl[1] = std::min(hsl[1] * saturation, 1.0f);
	}

	hsl[2] = std::min(hsl[2] * luminance, 1.0f);

	return Ayu::Ui::ColorUtils::HSLToRGB(hsl);
}

QRgb BlendARGB(QRgb color1, QRgb color2, float ratio) {
	const auto inverseRatio = 1.0f - ratio;
	const auto r = static_cast<int>(qRed(color1) * inverseRatio + qRed(color2) * ratio);
	const auto g = static_cast<int>(qGreen(color1) * inverseRatio + qGreen(color2) * ratio);
	const auto b = static_cast<int>(qBlue(color1) * inverseRatio + qBlue(color2) * ratio);
	const auto a = static_cast<int>(qAlpha(color1) * inverseRatio + qAlpha(color2) * ratio);
	return qRgba(r, g, b, a);
}

QColor GetNoCoverBgColor() {
	return Window::Theme::IsNightMode() ? st::windowBgOver->c.darker(170) : st::windowBgOver->c;
}

struct Cover
{
	QPixmap pixToDraw;
	QPixmap pixToBg;
	bool noCover;
};

QPixmap MakeNoCoverImage(const QSize &size) {
	static QPixmap result;
	static auto resultTheme = Window::Theme::Background()->id();
	if (!result.isNull() && result.size() == size && resultTheme == Window::Theme::Background()->id()) {
		return result;
	}
	resultTheme = Window::Theme::Background()->id();

	auto image = QImage(size, QImage::Format_ARGB32);
	{
		auto p = Painter(&image);
		auto hq = PainterHighQualityEnabler(p);

		const auto bgColor = Window::Theme::IsNightMode()
								 ? st::windowBoldFg->c.darker()
								 : st::windowBoldFg->c.lighter();
		image.fill(bgColor);

		auto svgIcon = QSvgRenderer(u":/gui/icons/ayu/nocover.svg"_q);
		p.setPen(st::windowBoldFg->p);
		svgIcon.render(&p, QRect(0, 0, size.width(), size.height()));
	}
	const auto img = Image(std::move(image));
	result = img.pix(size, Images::PrepareArgs{.options = Images::Option::RoundSmall});
	return result;
}

} // namespace

Cover GetCurrentCover(
	const std::shared_ptr<Data::DocumentMedia> &dataMedia,
	const QSize &size) {
	if (!dataMedia) {
		return {
			.pixToDraw = MakeNoCoverImage(size),
			.pixToBg = MakeNoCoverImage(size),
			.noCover = true
		};
	}

	auto cover = QPixmap();
	const auto scaled = [&](not_null<Image*> image)
	{
		const auto aspectRatio = Qt::KeepAspectRatioByExpanding;
		return image->size().scaled(size, aspectRatio);
	};
	const auto args = Images::PrepareArgs{
		.options = Images::Option::RoundSmall,
		.outer = size,
	};
	if (const auto normal = dataMedia->thumbnail()) {
		return {
			.pixToDraw = normal->pixNoCache(scaled(normal), args),
			.pixToBg = normal->pixNoCache(),
			.noCover = false
		};
	} /*else if (const auto blurred = dataMedia->thumbnailInline()) {
		return {
			.pix = blurred->pixSingle(scaled(blurred), args.blurred()),
			.noCover = false
		};
	}*/

	return {
		.pixToDraw = MakeNoCoverImage(size),
		.pixToBg = MakeNoCoverImage(size),
		.noCover = true
	};
}

QRgb ExtractColorFromCover(const QPixmap &cover) {
	const auto palette = Ayu::Ui::Palette::from(cover).generate();

	const auto *swatch = palette.darkVibrantSwatch();
	if (!swatch) {
		swatch = palette.mutedSwatch();
	}
	if (!swatch) {
		swatch = palette.darkMutedSwatch();
	}
	if (!swatch) {
		swatch = palette.dominantSwatch();
	}

	if (!swatch) {
		return GetNoCoverBgColor().rgb();
	}

	auto extractedColor = swatch->rgb();

	const auto whiteColor = qRgb(255, 255, 255);
	const auto contrast = Ayu::Ui::ColorUtils::calculateContrast(whiteColor, extractedColor);

	auto adjustedColor = extractedColor;
	if (contrast > 15.0f) {
		adjustedColor = AdjustHsl(extractedColor, 2.0f);
	} else if (static_cast<int>(contrast) < 10) {
		adjustedColor = AdjustHsl(extractedColor, 0.5f);
	}

	if (Ayu::Ui::ColorUtils::calculateContrast(whiteColor, adjustedColor) < 3.0f) {
		adjustedColor = BlendARGB(adjustedColor, qRgb(0, 0, 0), 0.3f);
	}

	return adjustedColor;
}

AyuMusicButton::AyuMusicButton(
	QWidget *parent,
	MusicButtonData data,
	Fn<void()> handler)
	: RippleButton(parent, st::infoMusicButtonRipple)
	  , _performer(std::make_unique<Ui::FlatLabel>(
		  this,
		  data.performer,
		  st::infoMusicButtonPerformer))
	  , _title(std::make_unique<Ui::FlatLabel>(
		  this,
		  data.title,
		  st::infoMusicButtonTitle))
	  , _mediaView(data.mediaView) {
	rpl::combine(
		_title->naturalWidthValue(),
		_performer->naturalWidthValue()
	) | rpl::start_with_next([=]
							 {
								 resizeToWidth(widthNoMargins());
							 },
							 lifetime());

	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	_performer->setAttribute(Qt::WA_TransparentForMouseEvents);

	if (_mediaView && _mediaView->owner()->isSongWithCover() && !_mediaView->thumbnail()) {
		_mediaView->thumbnailWanted(Data::FileOrigin(data.msgId));
		_mediaView->owner()->owner().session().downloaderTaskFinished(
		) | rpl::take_while([=]
		{
			if (_mediaView->thumbnail()) {
				makeCover();
			}
			return !_mediaView->thumbnail();
		}) | rpl::start(lifetime());
	} else {
		makeCover();
	}

	setClickedCallback(std::move(handler));
}

AyuMusicButton::~AyuMusicButton() = default;

void AyuMusicButton::updateData(MusicButtonData data) {
	_performer->setText(data.performer);
	_title->setText(data.title);
	_mediaView = data.mediaView;

	if (_mediaView && _mediaView->owner()->isSongWithCover() && !_mediaView->thumbnail()) {
		_mediaView->thumbnailWanted(Data::FileOrigin(data.msgId));
		_mediaView->owner()->owner().session().downloaderTaskFinished(
		) | rpl::take_while([=]
		{
			if (_mediaView->thumbnail()) {
				makeCover();
			}
			return !_mediaView->thumbnail();
		}) | rpl::start(lifetime());
	} else {
		makeCover();
	}

	resizeToWidth(widthNoMargins());
}

void AyuMusicButton::makeCover() {
	const auto weak = base::make_weak(this);
	crl::async([=, mediaView = _mediaView]
	{
		const auto &settings = AyuSettings::getInstance();
		const auto &font = st::infoMusicButtonTitle.style.font;
		const auto skip = st::normalFont->spacew / 2;
		const auto size = font->height + skip + font->height;

		const auto cover = GetCurrentCover(mediaView, QSize(size, size));
		QColor bgColor;
		if (cover.noCover || !settings.adaptiveCoverColor) {
			bgColor = GetNoCoverBgColor();
		} else {
			bgColor = QColor::fromRgb(ExtractColorFromCover(cover.pixToBg));
		}

		const auto strong = weak.get();
		if (!strong) {
			return;
		}

		strong->_currentCover = {
			.pix = cover.pixToDraw,
			.bg = bgColor,
			.noCover = cover.noCover
		};

		crl::on_main([=]
		{
			const auto strong2 = weak.get();
			if (!strong2) {
				return;
			}

			strong2->repaint();
			strong2->_title->repaint();
			strong2->_performer->repaint();

			strong2->_onReady.fire({});
		});
	});
}

void AyuMusicButton::paintEvent(QPaintEvent *e) {
	if (!_currentCover) {
		return;
	}

	auto p = Painter(this);

	const auto &font = st::infoMusicButtonTitle.style.font;
	const auto skip = st::normalFont->spacew / 2;
	const auto size = font->height + skip + font->height;

	const auto cover = _currentCover.value();
	if (cover.noCover) {
		p.fillRect(e->rect(), cover.bg);
		paintRipple(p, QPoint());
	} else {
		QRadialGradient gradient(rect().topRight(), rect().width() / 2.0);
		gradient.setColorAt(0, cover.bg);
		gradient.setColorAt(1, QColor::fromRgb(AdjustHsl(cover.bg.rgb(), 1.5f)));
		p.fillRect(rect(), gradient);
	}

	if (!cover.pix.isNull()) {
		const auto &settings = AyuSettings::getInstance();
		if (!cover.noCover && settings.adaptiveCoverColor) {
			_title->setTextColorOverride(Qt::white);
			_performer->setTextColorOverride(performerColor);
		} else {
			_title->setTextColorOverride(std::nullopt);
			_performer->setTextColorOverride(std::nullopt);
		}

		auto hq = PainterHighQualityEnabler(p);
		const auto coverRect = QRect(st::infoMusicButtonPadding.left(), st::infoMusicButtonPadding.top(), size, size);
		p.drawPixmap(coverRect.topLeft(), cover.pix);
	} else {
		_title->setTextColorOverride(std::nullopt);
		_performer->setTextColorOverride(std::nullopt);
	}
}

int AyuMusicButton::resizeGetHeight(int newWidth) {
	const auto padding = st::infoMusicButtonPadding;
	const auto &font = st::infoMusicButtonTitle.style.font;

	const auto top = padding.top();
	const auto skip = st::normalFont->spacew / 2;

	const auto coverSize = font->height + skip + font->height;

	const auto available = newWidth - padding.left() - padding.right() - coverSize;
	_title->resizeToNaturalWidth(available);
	_title->moveToLeft(st::infoMusicButtonPadding.left() + padding.left() + coverSize, top);
	_performer->resizeToNaturalWidth(available);
	_performer->moveToLeft(st::infoMusicButtonPadding.left() + padding.left() + coverSize, top + font->height + skip);

	return padding.top() + font->height + skip + font->height + padding.bottom();
}

} // namespace Info::Profile
