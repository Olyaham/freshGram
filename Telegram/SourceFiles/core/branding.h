#pragma once

#include "base/basic_types.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>

namespace Core::Branding {

inline QString SettingsPath(const QString &workingDir) {
	return workingDir + u"tdata/freshgram_settings.json"_q;
}

inline QString SettingsPathForRead(const QString &workingDir) {
	const auto current = SettingsPath(workingDir);
	const auto legacy = workingDir + u"tdata/ayu_settings.json"_q;
	return QFile::exists(current) || !QFile::exists(legacy) ? current : legacy;
}

inline bool IsLegacyDownloadPath(const QString &path) {
	const auto downloads = QStandardPaths::writableLocation(
		QStandardPaths::DownloadLocation);
	if (path.isEmpty() || downloads.isEmpty()) {
		return false;
	}
	const auto normalized = QDir::cleanPath(QDir::fromNativeSeparators(path));
	for (const auto &legacy : {
		u"AyuGram Desktop"_q,
		u"AyuGramDesktop"_q,
		u"AyuGram"_q,
	}) {
		if (normalized == QDir::cleanPath(downloads + '/' + legacy)) {
			return true;
		}
	}
	return false;
}

} // namespace Core::Branding
