#pragma once

#include <cstdint>
#include <functional>
#include <utility>

namespace gsl {

template<typename Callback>
class FinalAction {
public:
	explicit FinalAction(Callback callback) : _callback(std::move(callback)) {
	}

	FinalAction(const FinalAction &) = delete;
	FinalAction &operator=(const FinalAction &) = delete;

	~FinalAction() {
		_callback();
	}

private:
	Callback _callback;

};

template<typename Callback>
auto finally(Callback callback) {
	return FinalAction<Callback>(std::move(callback));
}

} // namespace gsl

namespace crl {

using time = int64_t;

} // namespace crl

namespace base {

class Timer {
public:
	explicit Timer(std::function<void()> callback)
	: _callback(std::move(callback)) {
	}

	void callEach(crl::time) {
	}

	void callOnce(crl::time) {
	}

private:
	std::function<void()> _callback;

};

} // namespace base
