#pragma once

namespace crl {

template<typename Callback>
void on_main(Callback callback) {
	callback();
}

} // namespace crl
