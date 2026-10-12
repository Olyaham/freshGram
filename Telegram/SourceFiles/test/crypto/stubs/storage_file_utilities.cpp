#include "storage/details/storage_file_utilities.h"
#include "logs.h"

#include <QtCore/QtEndian>

#include <openssl/aes.h>
#include <openssl/crypto.h>
#include <openssl/sha.h>

#include <array>
#include <cstring>

namespace openssl {

template<size_t N, size_t M>
void Sha1To(
		std::array<uint8_t, N> &output,
		const std::array<uint8_t, M> &input) {
	SHA1(input.data(), input.size(), output.data());
}

} // namespace openssl

namespace Storage::details {
namespace {

using Sha1 = std::array<uint8_t, SHA_DIGEST_LENGTH>;

[[nodiscard]] Sha1 Hash(const QByteArray &data) {
	auto result = Sha1();
	SHA1(
		reinterpret_cast<const unsigned char*>(data.constData()),
		size_t(data.size()),
		result.data());
	return result;
}

void DeriveOldAes(
		const MTP::AuthKeyPtr &authKey,
		const uint8_t *msgKey,
		uint8_t *aesKey,
		uint8_t *aesIv) {
	const auto key = MTPint128([&] {
		auto result = MTPint128();
		std::memcpy(result.data(), msgKey, result.size());
		return result;
	}());
	auto outputKey = MTPint256();
	auto outputIv = MTPint256();
	authKey->prepareAES_oldmtp(key, outputKey, outputIv, false);
	std::memcpy(aesKey, outputKey.data(), outputKey.size());
	std::memcpy(aesIv, outputIv.data(), outputIv.size());
}

void Crypt(
		const QByteArray &input,
		QByteArray &output,
		const MTP::AuthKeyPtr &authKey,
		const uint8_t *msgKey,
		int direction) {
	auto aesKey = std::array<uint8_t, AES_BLOCK_SIZE * 2>();
	auto aesIv = std::array<uint8_t, AES_BLOCK_SIZE * 2>();
	DeriveOldAes(authKey, msgKey, aesKey.data(), aesIv.data());
	auto aes = AES_KEY();
	if (direction == AES_ENCRYPT) {
		AES_set_encrypt_key(aesKey.data(), 256, &aes);
	} else {
		AES_set_decrypt_key(aesKey.data(), 256, &aes);
	}
	output.resize(input.size());
	AES_ige_encrypt(
		reinterpret_cast<const unsigned char*>(input.constData()),
		reinterpret_cast<unsigned char*>(output.data()),
		unsigned(input.size()),
		&aes,
		aesIv.data(),
		direction);
}

} // namespace

EncryptedDescriptor::EncryptedDescriptor() = default;

EncryptedDescriptor::EncryptedDescriptor(uint32_t size) {
	auto fullSize = uint32_t(sizeof(uint32_t)) + size;
	if (fullSize & 0x0F) {
		fullSize += 0x10 - (fullSize & 0x0F);
	}
	data.reserve(qsizetype(fullSize));
	data.resize(sizeof(uint32_t));
	buffer.setBuffer(&data);
	buffer.open(QIODevice::WriteOnly);
	buffer.seek(sizeof(uint32_t));
	stream.setDevice(&buffer);
	stream.setVersion(QDataStream::Qt_5_1);
}

EncryptedDescriptor::~EncryptedDescriptor() {
	finish();
}

void EncryptedDescriptor::finish() {
	if (stream.device()) {
		stream.setDevice(nullptr);
	}
	if (buffer.isOpen()) {
		buffer.close();
	}
	buffer.setBuffer(nullptr);
}

QByteArray PrepareEncrypted(
		EncryptedDescriptor &descriptor,
		const MTP::AuthKeyPtr &key) {
	descriptor.finish();
	auto &plain = descriptor.data;
	auto size = uint32_t(plain.size());
	auto fullSize = size;
	if (fullSize & 0x0F) {
		fullSize += 0x10 - (fullSize & 0x0F);
		const auto previous = plain.size();
		plain.resize(fullSize);
		for (auto i = previous; i != plain.size(); ++i) {
			plain[i] = char((i * 37 + 19) & 255);
		}
	}
	const auto length = qToLittleEndian(size);
	std::memcpy(plain.data(), &length, sizeof(length));
	const auto digest = Hash(plain);
	auto encrypted = QByteArray(16, Qt::Uninitialized);
	std::memcpy(encrypted.data(), digest.data(), 16);
	auto cipher = QByteArray();
	Crypt(plain, cipher, key, digest.data(), AES_ENCRYPT);
	encrypted.append(cipher);
	return encrypted;
}

} // namespace Storage::details

#include "tdesktop_ayv1_decoder.inc"
