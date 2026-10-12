#include "ayu/ui/status_marquee.h"

#include "styles/style_marquee_label.h"

#include <QtGui/QLinearGradient>

#include <algorithm>

namespace AyuUi {
namespace {

constexpr auto kDelay = crl::time(500);
constexpr auto kMaxStep = crl::time(50);

} // namespace

void StatusMarquee::reset() {
	_offset = 0.;
	_delay = kDelay;
	_last = 0;
}

bool StatusMarquee::step(crl::time now, int natural, int available) {
	if (natural <= available || available <= 0) {
		reset();
		return false;
	}
	if (!_last) {
		_last = now;
		_delay = kDelay;
	}
	const auto dt = std::clamp(now - _last, crl::time(0), kMaxStep);
	_last = now;
	if (_delay > 0) {
		_delay -= dt;
		return true;
	}
	const auto total = float64(natural + st::marqueeLabelGap);
	const auto slowdown = float64(st::marqueeLabelSlowdown);
	const auto fast = float64(st::marqueeLabelSpeed);
	const auto slow = float64(st::marqueeLabelSpeedSlow);
	auto speed = fast;
	if (_offset < slowdown) {
		speed = slow + (fast - slow) * (_offset / slowdown);
	} else if (_offset >= total - slowdown) {
		const auto dist = _offset - (total - slowdown);
		speed = fast - (fast - slow) * (dist / slowdown);
	}
	_offset += (dt / 1000.) * speed;
	if (_offset > total) {
		_offset = 0.;
		_delay = kDelay;
	}
	return true;
}

const QImage &StatusMarquee::frame(
		QSize size,
		int natural,
		const Fn<void(Painter&, int)> &drawOne) {
	const auto ratio = style::DevicePixelRatio();
	if (_frame.size() != size * ratio) {
		_frame = QImage(size * ratio, QImage::Format_ARGB32_Premultiplied);
	}
	_frame.setDevicePixelRatio(ratio);
	_frame.fill(Qt::transparent);

	const auto gap = float64(st::marqueeLabelGap);
	const auto total = float64(natural) + gap;
	const auto offset = std::round(_offset * ratio) / ratio;
	const auto fade = st::marqueeLabelFade;
	const auto ramp = float64(st::marqueeLabelFadeRamp);
	{
		auto q = Painter(&_frame);
		drawOne(q, int(std::round(-offset)));
		if (offset + size.width() > total) {
			drawOne(q, int(std::round(total - offset)));
		}
		q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
		const auto leftOpacity = (offset < ramp)
			? (offset / ramp)
			: (offset > total - ramp)
			? (1. - (offset - (total - ramp)) / ramp)
			: 1.;
		if (leftOpacity > 0.) {
			auto gradient = QLinearGradient(0, 0, fade, 0);
			gradient.setColorAt(0., QColor(255, 255, 255, 255));
			gradient.setColorAt(1., QColor(255, 255, 255, 0));
			q.setOpacity(leftOpacity);
			q.fillRect(QRect(0, 0, fade, size.height()), gradient);
		}
		auto gradient = QLinearGradient(0, 0, fade, 0);
		gradient.setColorAt(0., QColor(255, 255, 255, 0));
		gradient.setColorAt(1., QColor(255, 255, 255, 255));
		q.setOpacity(1.);
		q.translate(size.width() - fade, 0);
		q.fillRect(QRect(0, 0, fade, size.height()), gradient);
	}
	return _frame;
}

} // namespace AyuUi
