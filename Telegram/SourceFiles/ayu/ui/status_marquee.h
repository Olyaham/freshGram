#pragma once

#include "ui/painter.h"

#include <QtGui/QImage>

namespace AyuUi {

class StatusMarquee final {
public:
	void reset();
	[[nodiscard]] bool step(crl::time now, int natural, int available);
	[[nodiscard]] const QImage &frame(
		QSize size,
		int natural,
		const Fn<void(Painter&, int)> &drawOne);

private:
	QImage _frame;
	float64 _offset = 0.;
	crl::time _delay = 0;
	crl::time _last = 0;

};

} // namespace AyuUi
