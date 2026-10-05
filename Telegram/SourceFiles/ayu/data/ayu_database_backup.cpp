#include "ayu/data/ayu_database_backup.h"

#include "ayu/libs/sqlite/sqlite3.h"
#include "base/timer.h"
#include "logs.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

#include <crl/crl_async.h>
#include <crl/crl_on_main.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

namespace AyuDatabaseBackup {
namespace {

constexpr auto kKeep = 5;
constexpr auto kPeriod = crl::time(6 * 60 * 60 * 1000);
constexpr auto kBusyTimeout = 5000;

std::atomic<bool> Running = false;

struct Entry {
	QString path;
	qint64 rows = 0;
};

[[nodiscard]] QString MainPath() {
	return "./tdata/ayudata.db";
}

[[nodiscard]] QString Directory() {
	return "./tdata/ayu_backups";
}

struct Handle {
	sqlite3 *db = nullptr;

	~Handle() {
		if (db) {
			sqlite3_close(db);
		}
	}
};

[[nodiscard]] bool Open(Handle &handle, const QString &path, int flags) {
	const auto utf8 = path.toUtf8();
	if (sqlite3_open_v2(utf8.constData(), &handle.db, flags, nullptr)
		!= SQLITE_OK) {
		return false;
	}
	sqlite3_busy_timeout(handle.db, kBusyTimeout);
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

[[nodiscard]] bool Verify(const QString &path, qint64 *rows = nullptr) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	if (file.read(15) != QByteArray("SQLite format 3")) {
		return false;
	}
	file.close();
	auto handle = Handle();
	if (!Open(handle, path, SQLITE_OPEN_READONLY)) {
		return false;
	} else if (!QuickCheckOk(handle.db)) {
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

[[nodiscard]] std::vector<Entry> List() {
	auto names = QDir(Directory()).entryList(
		QStringList() << "ayudata_*.db",
		QDir::Files,
		QDir::Name);
	std::reverse(names.begin(), names.end());
	auto result = std::vector<Entry>();
	for (const auto &name : names) {
		auto entry = Entry();
		entry.path = Directory() + '/' + name;
		const auto parts = name.chopped(3).split('_');
		if (parts.size() >= 4) {
			entry.rows = parts.back().toLongLong();
		}
		result.push_back(std::move(entry));
	}
	return result;
}

void ScrubSecrets(const QString &path) {
	auto handle = Handle();
	if (!Open(handle, path, SQLITE_OPEN_READWRITE)) {
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
	static_cast<void>(Exec(handle.db, QString("PRAGMA secure_delete = ON")));
	for (const auto &table : { "SecretMessage", "SecretChat", "SecretState" }) {
		if (TableExists(handle.db, table)) {
			static_cast<void>(
				Exec(handle.db, QString("DELETE FROM %1").arg(table)));
		}
	}
	static_cast<void>(Exec(handle.db, QString("VACUUM")));
}

void Rotate() {
	const auto list = List();
	auto richest = -1;
	for (auto i = 0; i != int(list.size()); ++i) {
		if (richest < 0 || list[i].rows > list[richest].rows) {
			richest = i;
		}
	}
	for (auto i = kKeep; i < int(list.size()); ++i) {
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
	for (const auto &extension : { QString(), QString("-wal") }) {
		const auto info = QFileInfo(MainPath() + extension);
		if (info.exists() && info.lastModified() > newest) {
			return false;
		}
	}
	return true;
}

} // namespace

bool create() {
	if (!QFile::exists(MainPath()) || UpToDate()) {
		return false;
	}
	auto expected = false;
	if (!Running.compare_exchange_strong(expected, true)) {
		return false;
	}
	const auto guard = gsl::finally([] { Running = false; });
	QDir().mkpath(Directory());
	const auto temporary = Directory() + "/.pending.db";
	QFile::remove(temporary);
	{
		auto handle = Handle();
		if (!Open(handle, MainPath(), SQLITE_OPEN_READWRITE)) {
			return false;
		}
		auto escaped = temporary;
		escaped.replace('\'', "''");
		if (!Exec(handle.db, QString("VACUUM INTO '%1'").arg(escaped))) {
			QFile::remove(temporary);
			return false;
		}
	}
	auto rows = qint64(0);
	if (!Verify(temporary, &rows)) {
		QFile::remove(temporary);
		return false;
	}
	const auto name = QString("%1/ayudata_%2_%3.db")
		.arg(Directory())
		.arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss"))
		.arg(rows);
	if (!QFile::rename(temporary, name)) {
		QFile::remove(temporary);
		return false;
	}
	QFile::setPermissions(
		name,
		QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	Rotate();
	return true;
}

bool restore(int &index) {
	const auto list = List();
	for (auto i = index; i < int(list.size()); ++i) {
		if (!Verify(list[i].path)) {
			continue;
		}
		QFile::remove(MainPath());
		QFile::remove(MainPath() + "-wal");
		QFile::remove(MainPath() + "-shm");
		if (QFile::copy(list[i].path, MainPath())) {
			index = i + 1;
			LOG(("[AyuGram] Database restored from '%1'.").arg(list[i].path));
			return true;
		}
	}
	index = int(list.size());
	return false;
}

void startPeriodic() {
	static auto timer = std::unique_ptr<base::Timer>();
	if (timer) {
		return;
	}
	timer = std::make_unique<base::Timer>([] {
		crl::async([] {
			[[maybe_unused]] const auto created = create();
		});
	});
	timer->callEach(kPeriod);
}

} // namespace AyuDatabaseBackup
