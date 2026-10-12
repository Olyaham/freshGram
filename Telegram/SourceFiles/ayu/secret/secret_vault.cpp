#include "ayu/secret/secret_vault.h"

#include "ayu/utils/file_perms.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "mtproto/mtproto_auth_key.h"
#include "storage/storage_domain.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QTemporaryFile>

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <vector>

namespace AyuSecret::Vault {
namespace {

constexpr uint8_t kMagic[4] = { 'F', 'G', 'V', '1' };
constexpr uint8_t kMagicBound[4] = { 'F', 'G', 'V', '2' };
constexpr auto kNonceSize = 12;
constexpr auto kTagSize = 16;
constexpr auto kHeaderSize = 4 + kNonceSize + kTagSize;
std::atomic<bool> Ready = false;

[[nodiscard]] std::filesystem::path NativePath(const QString &path) {
#ifdef Q_OS_WIN
	return std::filesystem::path(path.toStdWString());
#else
	const auto encoded = path.toUtf8();
	return std::filesystem::path(encoded.constData());
#endif
}

[[nodiscard]] bool DeriveKey(std::array<uint8_t, 32> &key) {
	if (!Ready.load(std::memory_order_acquire)) {
		return false;
	}
	const auto local = Core::App().domain().local().localKey();
	if (!local) {
		return false;
	}
	const auto data = local->data();
	auto context = SHA256_CTX();
	SHA256_Init(&context);
	static const char kLabel[] = "freshGram/secret-chats/vault/v1";
	SHA256_Update(&context, kLabel, sizeof(kLabel) - 1);
	SHA256_Update(&context, data.data(), data.size());
	SHA256_Final(key.data(), &context);
	return true;
}

[[nodiscard]] bool SealBufferToFile(
		const QString &path,
		const uint8_t *plain,
		size_t size,
		const std::string &context) {
	constexpr auto kChunk = size_t(1024 * 1024);
	if (context.size() > size_t(std::numeric_limits<int>::max())) {
		return false;
	}
	auto key = std::array<uint8_t, 32>();
	if (!DeriveKey(key)) {
		return false;
	}
	const auto directory = QFileInfo(path).absolutePath();
	if (!QDir().mkpath(directory)) {
		return false;
	}
	AyuUtils::RestrictDir(directory);
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	auto header = std::array<uint8_t, kHeaderSize>();
	std::memcpy(header.data(), kMagicBound, sizeof(kMagicBound));
	RandomBytes(header.data() + 4, kNonceSize);
	if (file.write(
			reinterpret_cast<const char*>(header.data()),
			qint64(header.size())) != qint64(header.size())) {
		return false;
	}
	using Cipher = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
	auto cipher = Cipher(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
	if (!cipher) {
		return false;
	}
	auto ok = EVP_EncryptInit_ex(
		cipher.get(),
		EVP_aes_256_gcm(),
		nullptr,
		key.data(),
		header.data() + 4) == 1;
	auto length = 0;
	if (ok && !context.empty()) {
		ok = EVP_EncryptUpdate(
			cipher.get(),
			nullptr,
			&length,
			reinterpret_cast<const uint8_t*>(context.data()),
			int(context.size())) == 1;
	}
	auto output = std::vector<uint8_t>(kChunk + 16);
	for (auto offset = size_t(0); ok && offset < size; ) {
		const auto count = std::min(kChunk, size - offset);
		ok = EVP_EncryptUpdate(
			cipher.get(),
			output.data(),
			&length,
			plain + offset,
			int(count)) == 1;
		if (ok && file.write(
				reinterpret_cast<const char*>(output.data()),
				length) != length) {
			ok = false;
		}
		offset += count;
	}
	auto finalLength = 0;
	if (ok) {
		ok = EVP_EncryptFinal_ex(
			cipher.get(),
			output.data(),
			&finalLength) == 1;
	}
	if (ok && finalLength
		&& file.write(
			reinterpret_cast<const char*>(output.data()),
			finalLength) != finalLength) {
		ok = false;
	}
	if (ok) {
		ok = EVP_CIPHER_CTX_ctrl(
			cipher.get(),
			EVP_CTRL_GCM_GET_TAG,
			kTagSize,
			header.data() + 4 + kNonceSize) == 1;
	}
	if (!ok
		|| !file.seek(qint64(4 + kNonceSize))
		|| file.write(
			reinterpret_cast<const char*>(header.data() + 4 + kNonceSize),
			kTagSize) != kTagSize
		|| !file.commit()) {
		return false;
	}
	AyuUtils::RestrictFile(path);
	return true;
}

} // namespace

void SetReady(bool ready) {
	Ready.store(ready, std::memory_order_release);
}

bool Available() {
	return Ready.load(std::memory_order_acquire)
		&& Core::App().domain().local().localKey() != nullptr;
}

bool IsSealed(const Bytes &data) {
	return data.size() >= kHeaderSize
		&& (std::memcmp(data.data(), kMagic, sizeof(kMagic)) == 0
			|| std::memcmp(
				data.data(),
				kMagicBound,
				sizeof(kMagicBound)) == 0);
}

bool IsBound(const Bytes &data) {
	return data.size() >= kHeaderSize
		&& std::memcmp(
			data.data(),
			kMagicBound,
			sizeof(kMagicBound)) == 0;
}

Bytes Seal(const Bytes &plain, const std::string &context) {
	constexpr auto kMaxInline = size_t(512) * 1024 * 1024;
	auto key = std::array<uint8_t, 32>();
	if (plain.size() > kMaxInline || !DeriveKey(key)) {
		return {};
	}
	auto result = Bytes(kHeaderSize + plain.size());
	std::memcpy(result.data(), kMagicBound, sizeof(kMagicBound));
	RandomBytes(result.data() + 4, kNonceSize);
	const auto cipher = EVP_CIPHER_CTX_new();
	if (!cipher) {
		return {};
	}
	auto ok = EVP_EncryptInit_ex(
		cipher,
		EVP_aes_256_gcm(),
		nullptr,
		key.data(),
		result.data() + 4) == 1;
	auto length = 0;
	if (ok && !context.empty()) {
		ok = EVP_EncryptUpdate(
			cipher,
			nullptr,
			&length,
			reinterpret_cast<const uint8_t*>(context.data()),
			int(context.size())) == 1;
	}
	if (ok && !plain.empty()) {
		ok = EVP_EncryptUpdate(
			cipher,
			result.data() + kHeaderSize,
			&length,
			plain.data(),
			int(plain.size())) == 1;
	}
	auto finalLength = 0;
	if (ok) {
		auto tail = std::array<uint8_t, 16>();
		ok = EVP_EncryptFinal_ex(cipher, tail.data(), &finalLength) == 1;
	}
	if (ok) {
		ok = EVP_CIPHER_CTX_ctrl(
			cipher,
			EVP_CTRL_GCM_GET_TAG,
			kTagSize,
			result.data() + 4 + kNonceSize) == 1;
	}
	EVP_CIPHER_CTX_free(cipher);
	if (!ok) {
		return {};
	}
	return result;
}

bool Open(
		const Bytes &sealed,
		Bytes &plain,
		const std::string &context) {
	if (!IsSealed(sealed)) {
		return false;
	}
	constexpr auto kMaxInline = size_t(512) * 1024 * 1024;
	auto key = std::array<uint8_t, 32>();
	if (sealed.size() > kHeaderSize + kMaxInline || !DeriveKey(key)) {
		return false;
	}
	const auto bound = IsBound(sealed);
	const auto size = sealed.size() - kHeaderSize;
	plain.assign(size, 0);
	const auto cipher = EVP_CIPHER_CTX_new();
	if (!cipher) {
		return false;
	}
	auto ok = EVP_DecryptInit_ex(
		cipher,
		EVP_aes_256_gcm(),
		nullptr,
		key.data(),
		sealed.data() + 4) == 1;
	auto length = 0;
	if (ok) {
		auto tag = std::array<uint8_t, kTagSize>();
		std::memcpy(tag.data(), sealed.data() + 4 + kNonceSize, kTagSize);
		ok = EVP_CIPHER_CTX_ctrl(
			cipher,
			EVP_CTRL_GCM_SET_TAG,
			kTagSize,
			tag.data()) == 1;
	}
	if (ok && bound && !context.empty()) {
		ok = EVP_DecryptUpdate(
			cipher,
			nullptr,
			&length,
			reinterpret_cast<const uint8_t*>(context.data()),
			int(context.size())) == 1;
	}
	if (ok && size > 0) {
		ok = EVP_DecryptUpdate(
			cipher,
			plain.data(),
			&length,
			sealed.data() + kHeaderSize,
			int(size)) == 1;
	}
	if (ok) {
		auto tail = std::array<uint8_t, 16>();
		auto finalLength = 0;
		ok = EVP_DecryptFinal_ex(cipher, tail.data(), &finalLength) == 1;
	}
	EVP_CIPHER_CTX_free(cipher);
	if (!ok) {
		plain.clear();
		return false;
	}
	return true;
}

bool SealToFile(
		const QString &path,
		const Bytes &plain,
		const std::string &context) {
	return SealBufferToFile(path, plain.data(), plain.size(), context);
}

bool SealToFile(
		const QString &path,
		const QByteArray &plain,
		const std::string &context) {
	return SealBufferToFile(
		path,
		reinterpret_cast<const uint8_t*>(plain.constData()),
		size_t(plain.size()),
		context);
}

bool OpenFromFile(
		const QString &path,
		Bytes &plain,
		const std::string &context,
		bool *legacy) {
	plain.clear();
	if (legacy) {
		*legacy = false;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)
		|| file.size() < qint64(kHeaderSize)
		|| file.size() > qint64(kHeaderSize + size_t(512) * 1024 * 1024)) {
		return false;
	}
	const auto raw = file.readAll();
	if (file.error() != QFileDevice::NoError) {
		return false;
	}
	const auto sealed = Bytes(raw.begin(), raw.end());
	const auto oldFormat = !IsBound(sealed);
	if (!Open(sealed, plain, context)) {
		plain.clear();
		return false;
	}
	if (legacy) {
		*legacy = oldFormat;
	}
	return true;
}

bool SealFileStreamed(
		const QString &srcPath,
		const QString &dstPath,
		const std::string &context) {
	constexpr auto kChunk = size_t(1024 * 1024);
	auto key = std::array<uint8_t, 32>();
	if (!DeriveKey(key)) {
		return false;
	}
	auto src = QFile(srcPath);
	if (!src.open(QIODevice::ReadOnly)) {
		return false;
	}
	auto temp = QTemporaryFile(
		QFileInfo(dstPath).absolutePath() + QStringLiteral("/.sealingXXXXXX"));
	if (!temp.open()) {
		return false;
	}
	const auto tempPath = temp.fileName();
	AyuUtils::RestrictFile(tempPath);
	auto header = std::array<uint8_t, kHeaderSize>();
	std::memcpy(header.data(), kMagicBound, sizeof(kMagicBound));
	RandomBytes(header.data() + 4, kNonceSize);
	std::memset(header.data() + 4 + kNonceSize, 0, kTagSize);
	if (temp.write(
			reinterpret_cast<const char*>(header.data()),
			qint64(header.size())) != qint64(header.size())) {
		return false;
	}
	const auto cipher = EVP_CIPHER_CTX_new();
	if (!cipher) {
		return false;
	}
	auto ok = EVP_EncryptInit_ex(
		cipher,
		EVP_aes_256_gcm(),
		nullptr,
		key.data(),
		header.data() + 4) == 1;
	auto length = 0;
	if (ok && !context.empty()) {
		ok = EVP_EncryptUpdate(
			cipher,
			nullptr,
			&length,
			reinterpret_cast<const uint8_t*>(context.data()),
			int(context.size())) == 1;
	}
	auto chunk = std::vector<uint8_t>(kChunk);
	auto out = std::vector<uint8_t>(kChunk + 16);
	while (ok) {
		const auto read = src.read(
			reinterpret_cast<char*>(chunk.data()),
			qint64(chunk.size()));
		if (read < 0) {
			ok = false;
			break;
		}
		if (read == 0) {
			break;
		}
		ok = EVP_EncryptUpdate(
			cipher,
			out.data(),
			&length,
			chunk.data(),
			int(read)) == 1;
		if (ok && temp.write(
				reinterpret_cast<const char*>(out.data()),
				length) != length) {
			ok = false;
		}
	}
	auto finalLength = 0;
	if (ok) {
		auto tail = std::array<uint8_t, 16>();
		ok = EVP_EncryptFinal_ex(cipher, tail.data(), &finalLength) == 1;
	}
	if (ok) {
		ok = EVP_CIPHER_CTX_ctrl(
			cipher,
			EVP_CTRL_GCM_GET_TAG,
			kTagSize,
			header.data() + 4 + kNonceSize) == 1;
	}
	EVP_CIPHER_CTX_free(cipher);
	// Buffered writes can hide I/O errors: flush explicitly and check
	// before publishing, or a short file (header only) passes as done.
	if (ok && (!temp.flush() || temp.error() != QFileDevice::NoError)) {
		ok = false;
	}
	if (!ok) {
		return false;
	}
	if (!temp.seek(qint64(4 + kNonceSize))
		|| temp.write(
			reinterpret_cast<const char*>(header.data() + 4 + kNonceSize),
			kTagSize) != kTagSize
		|| !temp.flush()
		|| temp.error() != QFileDevice::NoError) {
		return false;
	}
	temp.close();
	return PublishFile(tempPath, dstPath);
}

bool PublishFile(
		const QString &tempPath,
		const QString &dstPath) {
	auto error = std::error_code();
	std::filesystem::create_hard_link(
		NativePath(tempPath),
		NativePath(dstPath),
		error);
	if (error) {
		QFile::remove(tempPath);
		return false;
	}
	if (!QFile::remove(tempPath)) {
		QFile::remove(dstPath);
		return false;
	}
	AyuUtils::RestrictFile(dstPath);
	return true;
}

bool OpenFileStreamed(
		const QString &srcPath,
		const QString &dstPath,
		const std::string &context) {
	constexpr auto kChunk = size_t(1024 * 1024);
	auto key = std::array<uint8_t, 32>();
	if (!DeriveKey(key)) {
		return false;
	}
	auto src = QFile(srcPath);
	if (!src.open(QIODevice::ReadOnly)) {
		return false;
	}
	auto header = std::array<uint8_t, kHeaderSize>();
	if (src.read(
			reinterpret_cast<char*>(header.data()),
			qint64(header.size())) != qint64(header.size())) {
		return false;
	}
	const auto probe = Bytes(header.begin(), header.end());
	if (!IsSealed(probe)) {
		return false;
	}
	const auto bound = IsBound(probe);
	const auto cipher = EVP_CIPHER_CTX_new();
	if (!cipher) {
		return false;
	}
	auto ok = EVP_DecryptInit_ex(
		cipher,
		EVP_aes_256_gcm(),
		nullptr,
		key.data(),
		header.data() + 4) == 1;
	auto length = 0;
	if (ok) {
		auto tag = std::array<uint8_t, kTagSize>();
		std::memcpy(tag.data(), header.data() + 4 + kNonceSize, kTagSize);
		ok = EVP_CIPHER_CTX_ctrl(
			cipher,
			EVP_CTRL_GCM_SET_TAG,
			kTagSize,
			tag.data()) == 1;
	}
	if (ok && bound && !context.empty()) {
		ok = EVP_DecryptUpdate(
			cipher,
			nullptr,
			&length,
			reinterpret_cast<const uint8_t*>(context.data()),
			int(context.size())) == 1;
	}
	// Decrypt into a sibling temp first: a failure must never
	// truncate a pre-existing good file at dstPath.
	auto temp = QTemporaryFile(
		QFileInfo(dstPath).absolutePath() + QStringLiteral("/.openingXXXXXX"));
	if (ok && !temp.open()) {
		ok = false;
	}
	if (ok) {
		const auto tempPath = temp.fileName();
		AyuUtils::RestrictFile(tempPath);
	}
	auto chunk = std::vector<uint8_t>(kChunk);
	auto out = std::vector<uint8_t>(kChunk + 16);
	while (ok) {
		const auto read = src.read(
			reinterpret_cast<char*>(chunk.data()),
			qint64(chunk.size()));
		if (read < 0) {
			ok = false;
			break;
		}
		if (read == 0) {
			break;
		}
		ok = EVP_DecryptUpdate(
			cipher,
			out.data(),
			&length,
			chunk.data(),
			int(read)) == 1;
		if (ok && temp.write(
				reinterpret_cast<const char*>(out.data()),
				length) != length) {
			ok = false;
		}
	}
	if (ok) {
		auto tail = std::array<uint8_t, 16>();
		auto finalLength = 0;
		ok = EVP_DecryptFinal_ex(cipher, tail.data(), &finalLength) == 1;
	}
	EVP_CIPHER_CTX_free(cipher);
	// Same flush check as on the seal path: Qt buffers writes.
	if (ok && (!temp.flush() || temp.error() != QFileDevice::NoError)) {
		ok = false;
	}
	if (!ok) {
		return false;
	}
	const auto tempPath = temp.fileName();
	temp.close();
	return PublishFile(tempPath, dstPath);
}

} // namespace AyuSecret::Vault
