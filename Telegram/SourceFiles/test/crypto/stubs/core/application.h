#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

using uint32 = uint32_t;
using int32 = int32_t;
using uchar = unsigned char;
using MTPint128 = std::array<uint8_t, 16>;
using MTPint256 = std::array<uint8_t, 32>;

namespace bytes {

using type = uint8_t;

template<size_t N>
using array = std::array<uint8_t, N>;

} // namespace bytes

namespace MTP {

class AuthKey {
public:
	using Data = std::array<uint8_t, 256>;

	explicit AuthKey(Data value) : _key(std::move(value)) {
	}

	void prepareAES_oldmtp(
		const MTPint128 &msgKey,
		MTPint256 &aesKey,
		MTPint256 &aesIV,
		bool send) const;

	[[nodiscard]] const Data &data() const {
		return _key;
	}

private:
	Data _key = {};

};

using AuthKeyPtr = std::shared_ptr<AuthKey>;

} // namespace MTP

namespace TestRuntime {

struct Local {
	MTP::AuthKeyPtr key;
	bool available = true;

	[[nodiscard]] MTP::AuthKeyPtr localKey() const {
		return available ? key : nullptr;
	}
};

struct Domain {
	Local storage;

	[[nodiscard]] Local &local() {
		return storage;
	}

	[[nodiscard]] const Local &local() const {
		return storage;
	}

	[[nodiscard]] bool started() const {
		return storage.available;
	}
};

struct App {
	Domain storage;

	[[nodiscard]] Domain &domain() {
		return storage;
	}

	[[nodiscard]] const Domain &domain() const {
		return storage;
	}
};

inline App app;

inline void ResetKey(uint8_t seed = 17) {
	app.storage.storage.available = true;
	auto bytes = MTP::AuthKey::Data();
	for (auto i = size_t(0); i != bytes.size(); ++i) {
		bytes[i] = uint8_t(seed + i);
	}
	app.storage.storage.key = std::make_shared<MTP::AuthKey>(std::move(bytes));
}

} // namespace TestRuntime

namespace Core {

inline TestRuntime::App &App() {
	return TestRuntime::app;
}

} // namespace Core
