#pragma once

#include <QtCore/QDir>
#include <QtCore/QFile>

namespace AyuUtils {

// Best-effort owner-only restriction for at-rest files.
// On Linux/macOS this is 0600; on Windows it maps to the closest
// read-only flag handling Qt provides. Never fails the caller.
inline void RestrictFile(const QString &path) {
	QFile::setPermissions(
		path,
		QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

// Best-effort owner-only restriction for directories (0700 on POSIX).
inline void RestrictDir(const QString &path) {
	QFile::setPermissions(
		path,
		QFileDevice::ReadOwner
			| QFileDevice::WriteOwner
			| QFileDevice::ExeOwner);
}

// mkpath + restrict. Returns false only if the directory is missing
// afterwards (mkpath itself failed).
[[nodiscard]] inline bool EnsurePrivateDir(const QString &path) {
	QDir().mkpath(path);
	RestrictDir(path);
	return QDir(path).exists();
}

} // namespace AyuUtils
