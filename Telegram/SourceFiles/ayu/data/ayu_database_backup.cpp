#include "ayu/data/ayu_database_backup.h"

#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_archive.h"
#include "ayu/data/ayu_database.h"
#include "ayu/libs/sqlite/sqlite3.h"
#include "ayu/secret/secret_vault.h"
#include "ayu/utils/file_perms.h"
#include "base/timer.h"
#include "logs.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QTemporaryFile>

#include <crl/crl_async.h>
#include <crl/crl_on_main.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace AyuDatabaseBackup {
namespace {

constexpr auto kHour = crl::time(60 * 60 * 1000);

std::atomic<bool> Running = false;
std::mutex BackupMutex;
// Nothing is rotated out until the settings are known.
std::atomic<bool> Configured = false;
std::atomic<int> KeepCount = 5;
std::atomic<int> IntervalHours = 6;
std::unique_ptr<base::Timer> PeriodicTimer;

struct Entry {
	QString path;
	qint64 rows = 0;
};

[[nodiscard]] QString MainPath() {
	return "./tdata/ayudata.enc";
}

[[nodiscard]] QString Directory() {
	return "./tdata/ayu_backups";
}

// Envelope context for sealed backups. The file name (timestamp + row
// count) is unique and stable, binding each blob to its own file.
[[nodiscard]] std::string BackupContext(const QString &path) {
	return "ayu-backup-v1:" + QFileInfo(path).fileName().toStdString();
}

struct Handle {
	sqlite3 *db = nullptr;

	~Handle() {
		if (db) {
			sqlite3_close(db);
		}
	}
};

[[nodiscard]] bool OpenMemory(Handle &handle, const QByteArray &plain) {
	if (sqlite3_open_v2(
			":memory:",
			&handle.db,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
			nullptr) != SQLITE_OK) {
		return false;
	}
	const auto size = sqlite3_int64(plain.size());
	const auto buffer = static_cast<unsigned char*>(sqlite3_malloc64(size));
	if (!buffer) {
		return false;
	}
	memcpy(buffer, plain.constData(), size_t(size));
	const auto result = sqlite3_deserialize(
		handle.db,
		"main",
		buffer,
		size,
		size,
		SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_RESIZEABLE);
	if (result != SQLITE_OK) {
		sqlite3_free(buffer);
		return false;
	}
	return true;
}

[[nodiscard]] bool Exec(sqlite3 *db, const QString &sql) {
	const auto utf8 = sql.toUtf8();
	return sqlite3_exec(db, utf8.constData(), nullptr, nullptr, nullptr)
		== SQLITE_OK;
}

[[nodiscard]] qint64 ScalarInt(sqlite3 *db, const QString &sql) {
	const auto utf8 = sql.toUtf8();
	sqlite3_stmt *statement = nullptr;
	if (sqlite3_prepare_v2(db, utf8.constData(), -1, &statement, nullptr)
		!= SQLITE_OK) {
		return -1;
	}
	auto result = qint64(-1);
	if (sqlite3_step(statement) == SQLITE_ROW) {
		result = sqlite3_column_int64(statement, 0);
	}
	sqlite3_finalize(statement);
	return result;
}

[[nodiscard]] bool QuickCheckOk(sqlite3 *db) {
	sqlite3_stmt *statement = nullptr;
	if (sqlite3_prepare_v2(db, "PRAGMA quick_check(1)", -1, &statement, nullptr)
		!= SQLITE_OK) {
		return false;
	}
	auto ok = false;
	if (sqlite3_step(statement) == SQLITE_ROW) {
		const auto text = sqlite3_column_text(statement, 0);
		ok = text && (QByteArray(reinterpret_cast<const char*>(text)) == "ok");
	}
	sqlite3_finalize(statement);
	return ok;
}

[[nodiscard]] bool TableExists(sqlite3 *db, const QString &name) {
	return ScalarInt(
		db,
		QString("SELECT count(*) FROM sqlite_master "
			"WHERE type='table' AND name='%1'").arg(name)) == 1;
}

[[nodiscard]] bool VerifyPlain(const QByteArray &plain, qint64 *rows = nullptr) {
	if (plain.size() < 15 || !plain.startsWith("SQLite format 3")) {
		return false;
	}
	auto handle = Handle();
	if (!OpenMemory(handle, plain) || !QuickCheckOk(handle.db)) {
		return false;
	}
	auto total = qint64(0);
	for (const auto &table : { "EditedMessage", "DeletedMessage" }) {
		if (!TableExists(handle.db, table)) {
			return false;
		}
		total += std::max<qint64>(
			0,
			ScalarInt(handle.db, QString("SELECT count(*) FROM %1").arg(table)));
	}
	if (rows) {
		*rows = total;
	}
	return true;
}

[[nodiscard]] bool ReplaceFile(
		const QString &sourcePath,
		const QString &destinationPath) {
	auto source = QFile(sourcePath);
	if (!source.open(QIODevice::ReadOnly)) {
		return false;
	}
	auto destination = QSaveFile(destinationPath);
	destination.setDirectWriteFallback(false);
	if (!destination.open(QIODevice::WriteOnly)) {
		return false;
	}
	auto buffer = QByteArray(1024 * 1024, Qt::Uninitialized);
	while (true) {
		const auto read = source.read(buffer.data(), buffer.size());
		if (read < 0) {
			destination.cancelWriting();
			return false;
		}
		if (!read) {
			break;
		}
		if (destination.write(buffer.constData(), read) != read) {
			destination.cancelWriting();
			return false;
		}
	}
	if (source.error() != QFileDevice::NoError || !destination.commit()) {
		return false;
	}
	AyuUtils::RestrictFile(destinationPath);
	return true;
}

[[nodiscard]] std::vector<Entry> List() {
	auto result = std::vector<Entry>();
	for (const auto &directory : { Directory(), QString("./tdata") }) {
		const auto names = QDir(directory).entryList(
			QStringList() << "ayudata_*.enc" << "ayudata_*.db",
			QDir::Files,
			QDir::Name);
		for (const auto &name : names) {
			auto entry = Entry();
			entry.path = directory + '/' + name;
			const auto parts = QFileInfo(name).completeBaseName().split('_');
			if (parts.size() >= 4) {
				entry.rows = parts[3].toLongLong();
			}
			result.push_back(std::move(entry));
		}
	}
	std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
		return QFileInfo(a.path).lastModified()
			> QFileInfo(b.path).lastModified();
	});
	return result;
}

void ScrubSecrets(const QString &path) {
	auto plain = QByteArray();
	if (!AyuArchive::ReadFile(path, BackupContext(path), plain)) {
		return;
	}
	auto handle = Handle();
	if (!OpenMemory(handle, plain)) {
		return;
	}
	auto dirty = false;
	for (const auto &table : { "SecretMessage", "SecretChat", "SecretState" }) {
		if (TableExists(handle.db, table)
			&& ScalarInt(
				handle.db,
				QString("SELECT count(*) FROM %1").arg(table)) != 0) {
			dirty = true;
		}
	}
	if (!dirty) {
		return;
	}
	if (!Exec(handle.db, QString("PRAGMA secure_delete = ON"))) {
		return;
	}
	for (const auto &table : { "SecretMessage", "SecretChat", "SecretState" }) {
		if (TableExists(handle.db, table)
			&& !Exec(handle.db, QString("DELETE FROM %1").arg(table))) {
			return;
		}
	}
	if (!Exec(handle.db, QString("VACUUM"))) {
		return;
	}
	auto size = sqlite3_int64();
	const auto data = sqlite3_serialize(handle.db, "main", &size, 0);
	if (data) {
		const auto scrubbed = QByteArray(
			reinterpret_cast<const char*>(data),
			qsizetype(size));
		sqlite3_free(data);
		if (!AyuArchive::WriteFile(path, scrubbed, BackupContext(path))) {
			LOG(("[AyuGram] Database: failed to publish a scrubbed backup."));
		}
	}
}

void Rotate() {
	const auto list = List();
	auto richest = -1;
	for (auto i = 0; i != int(list.size()); ++i) {
		if (richest < 0 || list[i].rows > list[richest].rows) {
			richest = i;
		}
	}
	if (!Configured) {
		return;
	}
	for (auto i = KeepCount.load(); i < int(list.size()); ++i) {
		if (i != richest) {
			QFile::remove(list[i].path);
		} else {
			ScrubSecrets(list[i].path);
		}
	}
}

[[nodiscard]] bool UpToDate() {
	const auto list = List();
	if (list.empty()) {
		return false;
	}
	const auto newest = QFileInfo(list.front().path).lastModified();
	const auto info = QFileInfo(MainPath());
	return !info.exists() || info.lastModified() <= newest;
}

} // namespace

void removeStaleTemps() {
	// Orphaned temp files from interrupted runs. Hidden dotfiles need
	// QDir::Hidden to be listed at all. Idempotent: safe to run on
	// every create() call, including the up-to-date early exit. A
	// concurrent guarded create() may lose its temp to this sweep and
	// abort that one backup (no corruption: writers fail, never half
	// publish, and temp names are unique per run except .pending.db).
	for (const auto &stale : QDir(Directory()).entryList(
			{ ".pending*.db", "*.sealing", ".scratch*.db", ".decrypt*", "*.opening" },
			QDir::Files | QDir::Hidden)) {
		QFile::remove(Directory() + '/' + stale);
	}
}

bool create() {
	auto expected = false;
	if (!Running.compare_exchange_strong(expected, true)) {
		return false;
	}
	const auto guard = gsl::finally([] { Running = false; });
	const auto lock = std::lock_guard(BackupMutex);
	removeStaleTemps();
	if (!QFile::exists(MainPath()) || UpToDate()) {
		return false;
	}
	if (!QDir().mkpath(Directory())) {
		return false;
	}
	AyuUtils::RestrictDir(Directory());
	if (!AyuSecret::Vault::Available()) {
		return false;
	}
	auto snapshot = QByteArray();
	auto rows = qint64(0);
	if (!AyuDatabase::exportSnapshot(snapshot, rows)) {
		return false;
	}
	const auto stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss");
	auto name = QString("%1/ayudata_%2_%3.enc")
		.arg(Directory())
		.arg(stamp)
		.arg(rows);
	for (auto dup = 1; QFile::exists(name); ++dup) {
		name = QString("%1/ayudata_%2_%3_%4.enc")
			.arg(Directory())
			.arg(stamp)
			.arg(rows)
			.arg(dup);
	}
	auto temporary = QTemporaryFile(Directory() + "/.pendingXXXXXX.db");
	temporary.setAutoRemove(true);
	if (!temporary.open()) {
		return false;
	}
	AyuUtils::RestrictFile(temporary.fileName());
	if (temporary.write(snapshot) != snapshot.size()
		|| !temporary.flush()) {
		return false;
	}
	const auto source = temporary.fileName();
	temporary.close();
	const auto written = AyuSecret::Vault::SealFileStreamed(
		source,
		name,
		BackupContext(name));
	if (!written) {
		LOG(("[AyuGram] Database: backup publication failed."));
		return false;
	}
	AyuUtils::RestrictFile(name);
	Rotate();
	return true;
}

void clear() {
	const auto lock = std::lock_guard(BackupMutex);
	QDir(Directory()).removeRecursively();
	const auto root = QDir("./tdata");
	for (const auto &name : root.entryList(
			{ "ayudata_*" },
			QDir::Files | QDir::Hidden)) {
		QFile::remove(root.absoluteFilePath(name));
	}
}

void recoverStaging() {
	// Crash recovery runs at startup (not only on the corrupt path, so
	// it cannot be skipped): a previous restore may have left a
	// verified staging copy while Main was already gone.
	const auto staging = MainPath() + ".restoring";
	const auto directory = QDir(QFileInfo(staging).path());
	const auto prefix = QFileInfo(staging).fileName();
	for (const auto &name : directory.entryList(
			{ prefix + ".*.opening", prefix + ".opening" },
			QDir::Files | QDir::Hidden)) {
		QFile::remove(directory.filePath(name));
	}
	if (!QFile::exists(staging)) {
		return;
	}
	auto plain = QByteArray();
	auto format = AyuArchive::Format::Plain;
	const auto valid = AyuArchive::ReadFile(
		staging,
		"ayu-snapshot-v1",
		plain,
		&format)
		&& VerifyPlain(plain);
	if (!QFile::exists(MainPath())
		&& valid
		&& (format == AyuArchive::Format::Fgv2
			|| AyuArchive::WriteFile(staging, plain, "ayu-snapshot-v1"))
		&& AyuSecret::Vault::PublishFile(staging, MainPath())) {
		LOG(("[AyuGram] Database restored from staging copy."));
	} else if (QFile::exists(MainPath())) {
		QFile::remove(staging);
	} else if (!valid) {
		QFile::remove(staging);
	}
}

bool restore(int &index) {
	const auto list = List();
	const auto strict = AyuSettings::RequireEncryptionForStorage();
	for (auto i = index; i < int(list.size()); ++i) {
		auto plain = QByteArray();
		auto format = AyuArchive::Format::Plain;
		if (!AyuArchive::ReadFile(
				list[i].path,
				BackupContext(list[i].path),
				plain,
				&format)
			|| !VerifyPlain(plain)) {
			continue;
		}
		if (strict && format == AyuArchive::Format::Plain) {
			LOG(("[AyuGram] Database: strict mode skips a plaintext backup."));
			continue;
		}
		const auto staging = MainPath() + ".restoring";
		QFile::remove(staging);
		if (!AyuArchive::WriteFile(staging, plain, "ayu-snapshot-v1")) {
			QFile::remove(staging);
			continue;
		}
		auto staged = QByteArray();
		if (!AyuArchive::ReadFile(
				staging,
				"ayu-snapshot-v1",
				staged)
			|| !VerifyPlain(staged)) {
			QFile::remove(staging);
			continue;
		}
		if (!ReplaceFile(staging, MainPath())) {
			QFile::remove(staging);
			continue;
		}
		QFile::remove(staging);
		index = i + 1;
		LOG(("[AyuGram] Database restored from '%1'.").arg(list[i].path));
		return true;
	}
	index = int(list.size());
	return false;
}

void startPeriodic() {
	if (PeriodicTimer) {
		return;
	}
	PeriodicTimer = std::make_unique<base::Timer>([] {
		if (!Configured) {
			const auto &settings = AyuSettings::getInstance();
			configure(
				settings.backupKeepCount(),
				settings.backupIntervalHours());
		}
		crl::async([=] {
			[[maybe_unused]] const auto created = create();
		});
	});
	PeriodicTimer->callEach(IntervalHours.load() * kHour);
}

void configure(int keepCount, int intervalHours) {
	Configured = true;
	KeepCount = keepCount;
	IntervalHours = intervalHours;
	if (PeriodicTimer) {
		PeriodicTimer->callEach(intervalHours * kHour);
	}
}

} // namespace AyuDatabaseBackup
