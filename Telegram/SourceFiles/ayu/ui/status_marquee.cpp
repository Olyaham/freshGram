#include "ayu/ui/status_marquee.h"

namespace AyuUi {
namespace {

constexpr auto kPause = crl::time(1200);
constexpr auto kSpeed = 40;

} // namespace

int StatusMarqueeOffset(crl::time elapsed, int overflow) {
	if (overflow <= 0) {
		return 0;
	}
	const auto travel = crl::time(overflow) * 1000 / kSpeed;
	const auto at = elapsed % (kPause + travel + kPause);
	if (at < kPause) {
		return 0;
	} else if (at < kPause + travel) {
		return int((at - kPause) * kSpeed / 1000);
	}
	return overflow;
}

} // namespace AyuUi
