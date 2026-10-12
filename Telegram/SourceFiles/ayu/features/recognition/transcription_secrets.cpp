#include "ayu/features/recognition/transcription_secrets.h"

#include "ayu/data/ayu_vault.h"
#include "ayu/secret/secret_vault.h"

namespace AyuRecognition {

QString SealTranscriptionSecret(
		const QString &plain,
		QString &stored,
		const std::string &context) {
	if (plain.isEmpty() || !AyuVault::HasKey() || !AyuSecret::Vault::Available()) {
		return stored;
	}
	const auto bytes = plain.toUtf8();
	const auto sealed = AyuSecret::Vault::Seal(
		AyuSecret::Bytes(bytes.begin(), bytes.end()), context);
	if (sealed.empty()) {
		return stored;
	}
	stored = QString::fromLatin1(QByteArray(
		reinterpret_cast<const char*>(sealed.data()), sealed.size()).toBase64());
	return stored;
}

QString OpenTranscriptionSecret(
		const QString &stored,
		const std::string &context) {
	if (stored.isEmpty() || !AyuVault::HasKey()) {
		return QString();
	}
	auto plain = QByteArray();
	const auto bytes = QByteArray::fromBase64(stored.toLatin1());
	const auto sealed = AyuSecret::Bytes(bytes.begin(), bytes.end());
	if (AyuSecret::Vault::IsSealed(sealed)) {
		auto opened = AyuSecret::Bytes();
		return AyuSecret::Vault::Open(sealed, opened, context)
			? QString::fromUtf8(reinterpret_cast<const char*>(opened.data()), opened.size())
			: QString();
	} else if (!AyuVault::Unseal(bytes, plain, true)) {
		return QString();
	}
	return QString::fromUtf8(plain);
}

} // namespace AyuRecognition
