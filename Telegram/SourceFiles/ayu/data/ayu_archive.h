// This file is part of freshGram.
#pragma once

#include "ayu/data/ayu_vault.h"
#include "ayu/secret/secret_vault.h"
#include "ayu/utils/file_perms.h"

#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>

namespace AyuArchive {

enum class Format {
	Plain,
	AyuVault,
	Fgv1,
	Fgv2,
};

[[nodiscard]] inline bool ReadFile(
		const QString &path,
		const std::string &context,
		QByteArray &plain,
		Format *format = nullptr) {
	plain.clear();
	if (format) {
		*format = Format::Plain;
	}
	auto source = QFile(path);
	if (!source.open(QIODevice::ReadOnly)) {
		return false;
	}
	const auto prefix = source.read(4);
	if (!source.seek(0)) {
		return false;
	}
	if (prefix == "FGV1" || prefix == "FGV2") {
		auto temp = QTemporaryDir(
			QFileInfo(path).absolutePath() + QStringLiteral("/.decryptXXXXXX"));
		if (!temp.isValid()) {
			return false;
		}
		AyuUtils::RestrictDir(temp.path());
		const auto scratch = temp.filePath(QStringLiteral("plain"));
		if (!AyuSecret::Vault::OpenFileStreamed(path, scratch, context)) {
			return false;
		}
		auto decrypted = QFile(scratch);
		if (!decrypted.open(QIODevice::ReadOnly)) {
			return false;
		}
		plain = decrypted.readAll();
		const auto ok = decrypted.error() == QFileDevice::NoError;
		decrypted.close();
		if (!ok) {
			plain.clear();
			return false;
		}
		if (format) {
			*format = prefix == "FGV2" ? Format::Fgv2 : Format::Fgv1;
		}
		return true;
	}
	const auto raw = source.readAll();
	if (source.error() != QFileDevice::NoError) {
		return false;
	}
	if (prefix == "AYV1") {
		if (!AyuVault::IsSealed(raw) || !AyuVault::Unseal(raw, plain)) {
			return false;
		}
		if (format) {
			*format = Format::AyuVault;
		}
		return true;
	}
	plain = raw;
	if (format) {
		*format = Format::Plain;
	}
	return true;
}

[[nodiscard]] inline bool WriteFile(
		const QString &path,
		const QByteArray &plain,
		const std::string &context) {
	return AyuSecret::Vault::SealToFile(path, plain, context);
}

[[nodiscard]] inline bool NeedsMigration(Format format) {
	return format != Format::Fgv2;
}

} // namespace AyuArchive
