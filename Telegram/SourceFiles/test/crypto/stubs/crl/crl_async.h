#pragma once

#include "base/timer.h"

namespace crl {

template<typename Callback>
void async(Callback callback) {
	callback();
}

} // namespace crl
