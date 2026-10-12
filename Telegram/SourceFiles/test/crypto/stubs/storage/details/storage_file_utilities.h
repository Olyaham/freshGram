#pragma once

#include "mtproto/mtproto_auth_key.h"

#include <QtCore/QBuffer>
#include <QtCore/QDataStream>

namespace Storage::details {

struct EncryptedDescriptor final {
	EncryptedDescriptor();
	explicit EncryptedDescriptor(uint32_t size);
	~EncryptedDescriptor();

	void finish();

	QByteArray data;
	QBuffer buffer;
	QDataStream stream;
};

[[nodiscard]] QByteArray PrepareEncrypted(
	EncryptedDescriptor &data,
	const MTP::AuthKeyPtr &key);

[[nodiscard]] bool DecryptLocal(
	EncryptedDescriptor &result,
	const QByteArray &encrypted,
	const MTP::AuthKeyPtr &key);

} // namespace Storage::details
