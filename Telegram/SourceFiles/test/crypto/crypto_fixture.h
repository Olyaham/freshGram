#pragma once

#include "core/application.h"
#include "ayu/ayu_settings.h"
#include "ayu/secret/secret_vault.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace CryptoTest {

using AyuSecret::Bytes;
using namespace Qt::StringLiterals;

inline void Require(bool value, const std::string &message) {
	if (!value) {
		throw std::runtime_error(message);
	}
}

[[nodiscard]] inline Bytes Pattern(size_t size) {
	auto result = Bytes(size);
	for (auto i = size_t(0); i != size; ++i) {
		result[i] = uint8_t((i * 131 + i / 251) & 255);
	}
	return result;
}

[[nodiscard]] inline Bytes Read(const QString &path) {
	auto file = QFile(path);
	Require(file.open(QIODevice::ReadOnly), "cannot read fixture");
	const auto raw = file.readAll();
	Require(file.error() == QFileDevice::NoError, "fixture read failed");
	return Bytes(raw.begin(), raw.end());
}

inline void Write(const QString &path, const Bytes &data) {
	auto file = QFile(path);
	Require(file.open(QIODevice::WriteOnly), "cannot write fixture");
	Require(file.write(
		reinterpret_cast<const char*>(data.data()),
		qint64(data.size())) == qint64(data.size()), "fixture write failed");
	Require(file.flush(), "fixture flush failed");
}

[[nodiscard]] inline Bytes FromHex(const char *hex) {
	const auto raw = QByteArray::fromHex(hex);
	return Bytes(raw.begin(), raw.end());
}

[[nodiscard]] inline Bytes Golden(bool bound) {
	return FromHex(bound
		? "46475632000102030405060708090a0bdfc70a9a826175cbd5b90df8ea162b4c"
			"5aeb18694151358b87d3a7718a77b7742d784b"
		: "46475631000102030405060708090a0b1461c62116d836f1ee8c1c54c7e18499"
			"5aeb18694151358b87d3a7718a77b7742d784b");
}

[[nodiscard]] inline Bytes GoldenPlain() {
	return FromHex("6b6e6f776e007661756c74ff7061796c6f6164");
}

class Fixture {
public:
	Fixture() : _oldDirectory(QDir::currentPath()) {
		Require(_temporary.isValid(), "cannot create temporary directory");
		Require(QDir::setCurrent(_temporary.path()), "cannot enter fixture");
		TestRuntime::ResetKey();
		AyuSecret::Vault::SetReady(true);
		AyuSettings::getInstance().strict = false;
	}

	Fixture(const Fixture &) = delete;
	Fixture &operator=(const Fixture &) = delete;

	~Fixture() {
		if (!QDir::setCurrent(_oldDirectory)) {
			std::terminate();
		}
	}

	[[nodiscard]] QString path(const QString &name) const {
		return _temporary.filePath(name);
	}

private:
	QTemporaryDir _temporary;
	QString _oldDirectory;

};

} // namespace CryptoTest
