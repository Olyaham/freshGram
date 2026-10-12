#include "crypto_fixture.h"

#include <QtCore/QJsonObject>

#include "ayu/data/ayu_archive.h"
#include "ayu/data/ayu_database.h"
#include "ayu/data/ayu_database_backup.h"

#include <QtCore/QCoreApplication>
#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <iostream>
#include <thread>

namespace {

using namespace CryptoTest;
namespace Vault = AyuSecret::Vault;

void CreateBackupFixture() {
	Require(QDir().mkpath("tdata"), "create tdata");
	AyuDatabase::initialize();
	AyuDatabase::clearEverything();
	QDir("tdata/ayu_backups").removeRecursively();
}

void PublicationFailure(const std::string &operation) {
	const auto fixture = Fixture();
	const auto source = fixture.path(u"input"_s);
	const auto destination = fixture.path(u"old-backup.publish-target"_s);
	const auto sentinel = Pattern(97);
	const auto plain = Pattern(65537);
	Write(source, operation == "open" ? Vault::Seal(plain, "fault") : plain);
	qputenv("FRESHGRAM_CRYPTO_FAULT_MODE", operation == "tag" ? "tag" : "publish");
	for (const auto existing : { false, true }) {
		if (existing) {
			Write(destination, sentinel);
		}
		qunsetenv("FRESHGRAM_CRYPTO_FAULT_HIT");
		const auto succeeded = operation == "open"
			? Vault::OpenFileStreamed(source, destination, "fault")
			: Vault::SealFileStreamed(source, destination, "fault");
		Require(qgetenv("FRESHGRAM_CRYPTO_FAULT_HIT") == "1",
			"fault injection was not reached; run through CTest");
		Require(!succeeded, "failed publication/rewrite reported success");
		Require(QFile::exists(destination) == existing, "unexpected destination mutation");
		if (existing) {
			Require(Read(destination) == sentinel, "publication corrupted previous file");
		}
		Require(QDir(QFileInfo(destination).path()).entryList(
			{ "*.opening*", "*.sealing*" }, QDir::Files | QDir::Hidden).empty(),
			"temporary file leaked");
	}
}

void BackupRace() {
	const auto fixture = Fixture();
	CreateBackupFixture();
	using Waiting = bool (*)();
	using Release = void (*)();
	const auto waiting = reinterpret_cast<Waiting>(dlsym(RTLD_DEFAULT,
		"FreshgramPublicationWaiting"));
	const auto release = reinterpret_cast<Release>(dlsym(RTLD_DEFAULT,
		"FreshgramReleasePublication"));
	Require(waiting && release, "publication barrier missing");
	qputenv("FRESHGRAM_CRYPTO_FAULT_MODE", "barrier");
	auto first = false;
	auto error = std::exception_ptr();
	auto thread = std::thread([&] {
		try {
			first = AyuDatabaseBackup::create();
		} catch (...) {
			error = std::current_exception();
		}
	});
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!waiting() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	const auto reached = waiting();
	const auto directory = QDir("tdata/ayu_backups");
	const auto before = directory.entryList({ ".pending*.db", "*.sealing*" },
		QDir::Files | QDir::Hidden);
	const auto second = reached ? AyuDatabaseBackup::create() : false;
	const auto after = directory.entryList({ ".pending*.db", "*.sealing*" },
		QDir::Files | QDir::Hidden);
	release();
	thread.join();
	if (error) {
		std::rethrow_exception(error);
	}
	Require(reached, "backup never reached publication barrier");
	Require(!second, "concurrent backup entered guarded work");
	Require(before.size() == 2 && before == after, "second create swept active backup");
	Require(first, "first backup aborted after concurrent create");
	Require(directory.entryList({ ".pending*.db", "*.sealing*" },
		QDir::Files | QDir::Hidden).empty(), "backup left plaintext temporary");
}

void BackupFailure() {
	const auto fixture = Fixture();
	CreateBackupFixture();
	qputenv("FRESHGRAM_CRYPTO_FAULT_MODE", "backup_fail");
	qunsetenv("FRESHGRAM_CRYPTO_FAULT_HIT");
	Require(!AyuDatabaseBackup::create(), "keyed failure downgraded to plaintext");
	Require(qgetenv("FRESHGRAM_CRYPTO_FAULT_HIT") == "1", "backup injection not reached");
	Require(QDir("tdata/ayu_backups").entryList(QDir::Files | QDir::Hidden).empty(),
		"keyed failure left backup or plaintext temporary");
}

void BackupClearRace() {
	const auto fixture = Fixture();
	CreateBackupFixture();
	auto row = DeletedMessage();
	row.userId = 42;
	row.dialogId = 12;
	row.messageId = 7;
	row.text = "before clear";
	AyuDatabase::addDeletedMessage(row);
	AyuDatabase::flush();
	const auto rootBackup = QString("tdata/ayudata_old_%1.db")
		.arg(QCoreApplication::applicationPid());
	using Waiting = bool (*)();
	using Release = void (*)();
	const auto waiting = reinterpret_cast<Waiting>(dlsym(RTLD_DEFAULT,
		"FreshgramPublicationWaiting"));
	const auto release = reinterpret_cast<Release>(dlsym(RTLD_DEFAULT,
		"FreshgramReleasePublication"));
	Require(waiting && release, "publication barrier missing");
	qputenv("FRESHGRAM_CRYPTO_FAULT_MODE", "barrier");
	auto created = false;
	auto createError = std::exception_ptr();
	auto createThread = std::thread([&] {
		try {
			created = AyuDatabaseBackup::create();
		} catch (...) {
			createError = std::current_exception();
		}
	});
	const auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::seconds(5);
	while (!waiting() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (!waiting()) {
		release();
		createThread.join();
		if (createError) {
			std::rethrow_exception(createError);
		}
		Require(false, "backup never reached publication barrier");
	}
	Write(rootBackup, Bytes{ 'l', 'e', 'g', 'a', 'c', 'y' });
	auto cleared = std::atomic<bool>(false);
	auto clearError = std::exception_ptr();
	auto clearThread = std::thread([&] {
		try {
			AyuDatabase::clearEverything();
			cleared = true;
		} catch (...) {
			clearError = std::current_exception();
		}
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	const auto waitedForPublisher = !cleared.load();
	release();
	createThread.join();
	clearThread.join();
	if (createError) {
		std::rethrow_exception(createError);
	}
	if (clearError) {
		std::rethrow_exception(clearError);
	}
	Require(waitedForPublisher, "clear did not wait for an active publisher");
	Require(created && cleared.load(), "backup/clear race did not finish");
	Require(QDir("tdata/ayu_backups").entryList(
		QDir::Files | QDir::Hidden).empty(), "clear left an archived backup");
	Require(!QFile::exists(rootBackup), "clear left a legacy root backup");
}

[[nodiscard]] qint64 DeletedRowsOnDisk() {
	auto plain = QByteArray();
	Require(AyuArchive::ReadFile(
		"tdata/ayudata.enc",
		"ayu-snapshot-v1",
		plain), "read published snapshot");
	sqlite3 *database = nullptr;
	Require(sqlite3_open(":memory:", &database) == SQLITE_OK,
		"open snapshot verifier");
	const auto size = sqlite3_int64(plain.size());
	const auto buffer = static_cast<unsigned char*>(sqlite3_malloc64(size));
	Require(buffer != nullptr, "allocate snapshot verifier");
	std::memcpy(buffer, plain.constData(), size_t(size));
	const auto opened = sqlite3_deserialize(
		database,
		"main",
		buffer,
		size,
		size,
		SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_RESIZEABLE);
	if (opened != SQLITE_OK) {
		sqlite3_free(buffer);
	}
	Require(opened == SQLITE_OK, "deserialize published snapshot");
	sqlite3_stmt *statement = nullptr;
	Require(sqlite3_prepare_v2(
		database,
		"SELECT count(*) FROM DeletedMessage",
		-1,
		&statement,
		nullptr) == SQLITE_OK, "prepare snapshot row count");
	Require(sqlite3_step(statement) == SQLITE_ROW, "read snapshot row count");
	const auto result = sqlite3_column_int64(statement, 0);
	sqlite3_finalize(statement);
	sqlite3_close(database);
	return result;
}

void SnapshotClearRace() {
	const auto fixture = Fixture();
	CreateBackupFixture();
	auto row = DeletedMessage();
	row.userId = 42;
	row.dialogId = 12;
	row.messageId = 7;
	row.text = "before clear";
	AyuDatabase::addDeletedMessage(row);
	AyuDatabase::flush();
	using Waiting = bool (*)();
	using Release = void (*)();
	const auto waiting = reinterpret_cast<Waiting>(dlsym(RTLD_DEFAULT,
		"FreshgramPublicationWaiting"));
	const auto release = reinterpret_cast<Release>(dlsym(RTLD_DEFAULT,
		"FreshgramReleasePublication"));
	Require(waiting && release, "publication barrier missing");
	qputenv("FRESHGRAM_CRYPTO_FAULT_MODE", "snapshot_barrier");
	auto saved = false;
	auto saveError = std::exception_ptr();
	auto saveThread = std::thread([&] {
		try {
			AyuDatabase::flush();
			saved = true;
		} catch (...) {
			saveError = std::current_exception();
		}
	});
	const auto deadline = std::chrono::steady_clock::now()
		+ std::chrono::seconds(5);
	while (!waiting() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (!waiting()) {
		release();
		saveThread.join();
		if (saveError) {
			std::rethrow_exception(saveError);
		}
		Require(false, "snapshot never reached publication barrier");
	}
	AyuDatabase::clearAllDeleted();
	Require(AyuDatabase::getDeletedMessages(42, 12, 0, 0, 0, 10).empty(),
		"in-memory deletion did not finish before the older snapshot");
	auto flushStarted = std::atomic<bool>(false);
	auto flushed = false;
	auto flushError = std::exception_ptr();
	auto flushThread = std::thread([&] {
		try {
			flushStarted = true;
			AyuDatabase::flush();
			flushed = true;
		} catch (...) {
			flushError = std::current_exception();
		}
	});
	const auto flushDeadline = std::chrono::steady_clock::now()
		+ std::chrono::seconds(5);
	while (!flushStarted.load()
		&& std::chrono::steady_clock::now() < flushDeadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	Require(flushStarted.load(), "new snapshot writer did not start");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	release();
	saveThread.join();
	flushThread.join();
	if (saveError) {
		std::rethrow_exception(saveError);
	}
	if (flushError) {
		std::rethrow_exception(flushError);
	}
	Require(saved && flushed, "snapshot/clear race did not finish");
	Require(DeletedRowsOnDisk() == 0,
		"older snapshot overwrote the cleared database");
}

} // namespace

int main(int argc, char **argv) {
	const auto application = QCoreApplication(argc, argv);
	try {
		Require(argc == 2, "expected operation");
		const auto operation = std::string(argv[1]);
		if (operation == "backup_race") {
			BackupRace();
		} else if (operation == "backup_fail") {
			BackupFailure();
		} else if (operation == "backup_clear_race") {
			BackupClearRace();
		} else if (operation == "snapshot_clear_race") {
			SnapshotClearRace();
		} else if (operation == "seal" || operation == "open" || operation == "tag") {
			PublicationFailure(operation);
		} else {
			return 2;
		}
		std::cout << "PASS " << operation << '\n';
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
