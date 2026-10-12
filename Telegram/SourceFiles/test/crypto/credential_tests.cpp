#include "ayu/features/recognition/transcription_secrets.h"
#include "ayu/secret/secret_vault.h"
#include "ayu/data/ayu_vault.h"
#include "core/application.h"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <iostream>
#include <stdexcept>

int main(int argc, char *argv[]) {
	auto app = QCoreApplication(argc, argv);
	try {
		const auto check = [](bool ok, const char *message) {
			if (!ok) throw std::runtime_error(message);
		};
		using namespace AyuRecognition;
		AyuSecret::Vault::SetReady(true);
		TestRuntime::ResetKey(0);
		auto &runtime = TestRuntime::app.storage.storage;
		AyuVault::SetKey(runtime.key);
		const auto plain = QString::fromUtf8("PRIVATE key Привет");
		const auto context = std::string("freshgram/asr/groq-key/v1");
		auto stored = QString();
		const auto encrypted = SealTranscriptionSecret(plain, stored, context);
		check(!encrypted.isEmpty() && !encrypted.contains("PRIVATE"), "key must not be persisted as plaintext");
		check(QByteArray::fromBase64(encrypted.toLatin1()).startsWith("FGV2"), "must use the downstream vault");
		check(OpenTranscriptionSecret(encrypted, context) == plain, "key must round-trip");
		runtime.available = false;
		check(OpenTranscriptionSecret(encrypted, context).isEmpty(), "locked startup must not decrypt");
		check(SealTranscriptionSecret(QString(), stored, context) == encrypted, "saving before unlock must preserve ciphertext");
		runtime.available = true;
		check(OpenTranscriptionSecret(stored, context) == plain, "lazy load after unlock must recover the saved key");
		check(OpenTranscriptionSecret(stored, "freshgram/asr/gemini-key/v1").isEmpty(), "provider keys must be context-bound");
		const auto correct = runtime.key;
		TestRuntime::ResetKey(1);
		AyuVault::SetKey(runtime.key);
		check(OpenTranscriptionSecret(stored, context).isEmpty(), "wrong account key must fail closed");
		check(SealTranscriptionSecret(QString(), stored, context) == encrypted, "unreadable ciphertext must survive unrelated saves");
		stored.clear();
		check(SealTranscriptionSecret(QString(), stored, context).isEmpty(), "explicit key clearing must persist");
		check(OpenTranscriptionSecret("PRIVATE plaintext", context).isEmpty(), "legacy reader must require a sealed envelope");
		runtime.key = correct;
		AyuVault::SetKey(correct);
		stored = QString::fromLatin1(AyuVault::Seal(plain.toUtf8()).toBase64());
		check(QByteArray::fromBase64(stored.toLatin1()).startsWith("AYV1"), "legacy fixture must be AYV1");
		check(OpenTranscriptionSecret(stored, context) == plain, "real AYV1 reader must recover existing keys");
		const auto migrated = SealTranscriptionSecret(OpenTranscriptionSecret(stored, context), stored, context);
		check(QByteArray::fromBase64(migrated.toLatin1()).startsWith("FGV2"), "legacy key must migrate to FGV2 on save");
		check(OpenTranscriptionSecret(migrated, context) == plain, "migrated key must round-trip");
		AyuSecret::Vault::SetReady(false);
		check(OpenTranscriptionSecret(migrated, context).isEmpty(), "vault readiness must gate decryption");
		std::cout << "credential persistence passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
