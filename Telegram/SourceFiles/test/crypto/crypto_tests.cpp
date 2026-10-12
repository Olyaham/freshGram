#include "crypto_fixture.h"

#include <QtCore/QJsonObject>

#include "ayu/data/ayu_archive.h"
#include "ayu/data/ayu_database_backup.h"
#include "ayu/data/ayu_database.h"
#include "ayu/data/ayu_encryption.h"
#include "base/timer.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>

#include <sqlite3.h>

#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <utility>

#if !defined(Q_OS_WIN)
#include <csignal>
#include <sys/resource.h>
#endif // !Q_OS_WIN

namespace {

using namespace CryptoTest;
using namespace std::string_literals;
namespace Vault = AyuSecret::Vault;
namespace Rows = AyuDatabase::Encryption;
namespace Database = AyuDatabase;
namespace Archive = AyuArchive;

void InlineRoundTrip() {
	const auto fixture = Fixture();
	for (const auto size : { 0, 1, 15, 16, 17, 1024, 65537 }) {
		const auto source = Pattern(size);
		const auto sealed = Vault::Seal(source, "row:binary\0suffix"s);
		Require(sealed.size() == source.size() + 32, "incorrect envelope size");
		Require(Vault::IsBound(sealed), "new envelope is not FGV2");
		auto opened = Bytes();
		Require(Vault::Open(sealed, opened, "row:binary\0suffix"s), "open failed");
		Require(opened == source, "round trip differs");
	}
}

void KnownVectors() {
	const auto fixture = Fixture();
	for (const auto bound : { false, true }) {
		auto opened = Bytes();
		Require(Vault::Open(Golden(bound), opened, "golden:context"),
			"independent golden vector rejected");
		Require(opened == GoldenPlain(), "golden plaintext differs");
		Require(Vault::IsBound(Golden(bound)) == bound, "version detection");
		const auto result = Vault::Open(Golden(bound), opened, "wrong-context");
		Require(result == !bound, "legacy/bound AAD contract broken");
	}
}

void Nonces() {
	const auto fixture = Fixture();
	const auto plain = Pattern(128);
	const auto first = Vault::Seal(plain, "nonce");
	for (auto i = 0; i != 32; ++i) {
		const auto next = Vault::Seal(plain, "nonce");
		Require(first != next, "encryption reused complete envelope");
		Require(!std::equal(first.begin() + 4, first.begin() + 16,
			next.begin() + 4), "nonce reused");
	}
}

void RejectCorruption() {
	const auto fixture = Fixture();
	const auto sealed = Vault::Seal(Pattern(257), "aad");
	for (auto i = size_t(0); i != sealed.size(); ++i) {
		auto bad = sealed;
		bad[i] ^= 1;
		auto plain = Bytes();
		Require(!Vault::Open(bad, plain, "aad"), "modified byte authenticated");
		Require(plain.empty(), "failed authentication exposed plaintext");
	}
	for (auto length = size_t(0); length < sealed.size(); ++length) {
		const auto shortEnvelope = Bytes(sealed.begin(), sealed.begin() + length);
		auto plain = Bytes();
		Require(!Vault::Open(shortEnvelope, plain, "aad"), "truncation accepted");
	}
	auto appended = sealed;
	appended.push_back(0);
	auto plain = Bytes();
	Require(!Vault::Open(appended, plain, "aad"), "appended data accepted");
}

void KeyAndContext() {
	const auto fixture = Fixture();
	const auto sealed = Vault::Seal(Pattern(4096), "table:user:dialog:message:text");
	auto plain = Bytes();
	Require(!Vault::Open(sealed, plain, "table:user:dialog:message:media"),
		"wrong context accepted");
	TestRuntime::ResetKey(31);
	Require(!Vault::Open(sealed, plain, "table:user:dialog:message:text"),
		"wrong key accepted");
	TestRuntime::app.storage.storage.available = false;
	Require(!Vault::Available(), "locked key reported available");
	Require(Vault::Seal(Pattern(32), "locked").empty(), "locked seal succeeded");
	Require(!Vault::Open(sealed, plain, "table:user:dialog:message:text"),
		"locked open succeeded");
}

void StreamBoundaries() {
	const auto fixture = Fixture();
	constexpr auto chunk = 1024 * 1024;
	for (const auto size : { 0, 1, 16, chunk - 1, chunk, chunk + 1, 2 * chunk + 7 }) {
		const auto source = Pattern(size);
		const auto input = fixture.path(u"input"_s);
		const auto sealed = fixture.path(u"sealed"_s);
		const auto output = fixture.path(u"output"_s);
		Write(input, source);
		// Fresh destination per iteration (see below): publishing
		// never replaces an existing file.
		QFile::remove(sealed);
		QFile::remove(output);
		Require(Vault::SealFileStreamed(input, sealed, "stream"), "stream seal");
		Require(QFileInfo(sealed).size() == size + 32, "stream size");
		Require(Vault::OpenFileStreamed(sealed, output, "stream"), "stream open");
		Require(Read(output) == source, "stream round trip differs");
		auto inlinePlain = Bytes();
		Require(Vault::Open(Read(sealed), inlinePlain, "stream"),
			"inline cannot open streamed envelope");
		Require(inlinePlain == source, "stream/inline disagreement");
		Write(sealed, Vault::Seal(source, "stream"));
		// Publishing requires a fresh destination (an atomic rename
		// must never destroy a previous file), so drop the output of
		// the first open before reusing the path.
		QFile::remove(output);
		Require(Vault::OpenFileStreamed(sealed, output, "stream"),
			"stream cannot open inline envelope");
		Require(Read(output) == source, "inline/stream disagreement");
		Require(!QFile::exists(sealed + u".sealing"_s), "seal temp leaked");
		Require(!QFile::exists(output + u".opening"_s), "open temp leaked");
	}
}

void StreamFailurePreservesDestination() {
	const auto fixture = Fixture();
	const auto source = fixture.path(u"sealed"_s);
	const auto output = fixture.path(u"existing"_s);
	const auto sentinel = Pattern(127);
	const auto sealed = Vault::Seal(Pattern(1024 * 1024 + 3), "stream");
	for (const auto position : { 0, 4, 16, 32, int(sealed.size() - 1) }) {
		auto bad = sealed;
		bad[position] ^= 1;
		Write(source, bad);
		Write(output, sentinel);
		Require(!Vault::OpenFileStreamed(source, output, "stream"),
			"corrupt stream opened");
		Require(Read(output) == sentinel, "failed open destroyed existing file");
		Require(!QFile::exists(output + u".opening"_s), "plaintext temp leaked");
	}
	Write(source, sealed);
	Require(!Vault::OpenFileStreamed(source, output, "wrong"), "wrong AAD opened");
	Require(Read(output) == sentinel, "wrong AAD destroyed existing file");
	Require(!Vault::SealFileStreamed(fixture.path(u"missing"_s), output, "stream"),
		"missing source sealed");
	Require(Read(output) == sentinel, "missing source destroyed existing file");
}

void FileApisAndLegacy() {
	const auto fixture = Fixture();
	const auto path = fixture.path(u"private/envelope"_s);
	const auto plain = Pattern(101);
	Require(Vault::SealToFile(path, plain, "file"), "SealToFile failed");
	auto opened = Bytes();
	auto legacy = true;
	Require(Vault::OpenFromFile(path, opened, "file", &legacy), "OpenFromFile failed");
	Require(opened == plain && !legacy, "file content/version");
	Write(path, Golden(false));
	Require(Vault::OpenFromFile(path, opened, "unused", &legacy), "legacy file failed");
	Require(legacy && opened == GoldenPlain(), "legacy file content/version");
	Require(Vault::OpenFileStreamed(path, fixture.path(u"legacy"_s), "unused"),
		"legacy streamed file failed");
	Require(Read(fixture.path(u"legacy"_s)) == GoldenPlain(), "legacy streamed bytes");
#if !defined(Q_OS_WIN)
	const auto mode = QFile::permissions(path);
	Require(!(mode & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
		| QFileDevice::ReadOther | QFileDevice::WriteOther)), "file is not private");
#endif // !Q_OS_WIN
}

void StreamIoErrors() {
	const auto fixture = Fixture();
	const auto input = fixture.path(u"input"_s);
	Write(input, Pattern(31));
	const auto missingParent = fixture.path(u"missing/output"_s);
	Require(!Vault::SealFileStreamed(input, missingParent, "io"), "missing parent");
	const auto blocked = fixture.path(u"blocked"_s);
	Require(QDir().mkpath(blocked), "block destination");
	Require(!Vault::SealFileStreamed(input, blocked, "io"), "directory destination accepted");
	Require(QFileInfo(blocked).isDir(), "failed seal changed directory");
	const auto output = fixture.path(u"unique"_s);
	Require(QDir().mkpath(output + u".sealing"_s), "create unrelated directory");
	Require(Vault::SealFileStreamed(input, output, "io"), "unique temp name collision");
	Require(QFileInfo(output + u".sealing"_s).isDir(), "unrelated directory removed");
}

void OversizedInlineFile() {
	const auto fixture = Fixture();
	const auto path = fixture.path(u"oversized"_s);
	{
		auto file = QFile(path);
		Require(file.open(QIODevice::WriteOnly), "create sparse fixture");
		Require(file.resize(qint64(512) * 1024 * 1024 + 33), "resize sparse fixture");
	}
	auto opened = Pattern(17);
	auto legacy = true;
	Require(!Vault::OpenFromFile(path, opened, "oversized", &legacy),
		"oversized inline file accepted");
	Require(opened.empty() && !legacy, "failed inline read exposed stale output");
}

void LimitedFileWrites() {
#if !defined(Q_OS_WIN)
	const auto fixture = Fixture();
	const auto source = fixture.path(u"input"_s);
	const auto sealed = fixture.path(u"sealed"_s);
	const auto destination = fixture.path(u"destination"_s);
	const auto bytes = Pattern(1024 * 1024 + 17);
	Write(source, bytes);
	Write(sealed, Vault::Seal(bytes, "limited"));
	const auto sentinel = Pattern(101);
	Write(destination, sentinel);
	auto previous = rlimit();
	Require(getrlimit(RLIMIT_FSIZE, &previous) == 0, "read file size limit");
	auto limited = previous;
	limited.rlim_cur = 65536;
	const auto oldSignal = std::signal(SIGXFSZ, SIG_IGN);
	Require(oldSignal != SIG_ERR, "ignore file size signal");
	const auto restoreSignal = gsl::finally([&] {
		if (std::signal(SIGXFSZ, oldSignal) == SIG_ERR) std::terminate();
	});
	Require(setrlimit(RLIMIT_FSIZE, &limited) == 0, "set file size limit");
	const auto restoreLimit = gsl::finally([&] {
		if (setrlimit(RLIMIT_FSIZE, &previous) != 0) std::terminate();
	});
	Require(!Vault::SealFileStreamed(source, destination, "limited"),
		"short stream write reported success");
	Require(Read(destination) == sentinel, "failed seal destroyed old destination");
	Require(!Vault::OpenFileStreamed(sealed, destination, "limited"),
		"short stream decrypt write reported success");
	Require(Read(destination) == sentinel, "failed decrypt destroyed old destination");
	Require(!Vault::SealToFile(destination, bytes, "limited"),
		"short inline write reported success");
	Require(Read(destination) == sentinel, "failed inline seal destroyed destination");
	Require(!QFile::exists(destination + u".opening"_s)
		&& !QFile::exists(destination + u".sealing"_s), "short-write temp leaked");
#endif // !Q_OS_WIN
}

#if !defined(Q_OS_WIN)
template<typename Callback>
void WithFileSizeLimit(rlim_t size, Callback callback) {
	auto previous = rlimit();
	Require(getrlimit(RLIMIT_FSIZE, &previous) == 0, "read file size limit");
	auto limited = previous;
	limited.rlim_cur = size;
	const auto oldSignal = std::signal(SIGXFSZ, SIG_IGN);
	Require(oldSignal != SIG_ERR, "ignore file size signal");
	const auto restoreSignal = gsl::finally([&] {
		if (std::signal(SIGXFSZ, oldSignal) == SIG_ERR) std::terminate();
	});
	Require(setrlimit(RLIMIT_FSIZE, &limited) == 0, "set file size limit");
	const auto restoreLimit = gsl::finally([&] {
		if (setrlimit(RLIMIT_FSIZE, &previous) != 0) std::terminate();
	});
	callback();
}

void BufferedSealWriteError() {
	const auto fixture = Fixture();
	const auto source = fixture.path(u"input"_s);
	const auto destination = fixture.path(u"sealed"_s);
	Write(source, Pattern(128));
	WithFileSizeLimit(16, [&] {
		const auto result = Vault::SealFileStreamed(source, destination, "buffered");
		Require(!result, "buffered seal flush failed but returned success; output size="
			+ std::to_string(QFileInfo(destination).size()));
		Require(!QFile::exists(destination), "failed seal published truncated envelope");
	});
}

void BufferedOpenWriteError() {
	const auto fixture = Fixture();
	const auto source = fixture.path(u"sealed"_s);
	const auto destination = fixture.path(u"opened"_s);
	Write(source, Vault::Seal(Pattern(128), "buffered"));
	WithFileSizeLimit(16, [&] {
		const auto result = Vault::OpenFileStreamed(source, destination, "buffered");
		Require(!result, "buffered open flush failed but returned success; output size="
			+ std::to_string(QFileInfo(destination).size()));
		Require(!QFile::exists(destination), "failed open published truncated plaintext");
	});
}
#endif // !Q_OS_WIN

void TextAndBlob() {
	const auto fixture = Fixture();
	const auto samples = std::array{
		std::string(), "normal text"s, "\0binary\xff"s,
		"Привет 👋"s, "ayu1:hello"s, "ayu2:hello"s, "ayu1:%%%"s,
	};
	for (const auto &plain : samples) {
		const auto sealed = Rows::SealText(plain, "field", true);
		Require(sealed.has_value(), "text seal refused");
		Require(Rows::OpenText(*sealed, "field") == plain, "text round trip");
		if (!plain.empty()) {
			Require(sealed->rfind("ayu2:", 0) == 0, "new text prefix");
			Require(*sealed != plain, "plaintext prefix collision bypassed encryption");
			Require(Rows::SealText(*sealed, "field", true) == sealed,
				"sealing is not idempotent");
			Require(Rows::OpenText(*sealed, "other").empty(), "text AAD mismatch");
		}
	}
	const auto bytes = Pattern(513);
	const auto plain = std::vector<char>(bytes.begin(), bytes.end());
	const auto sealed = Rows::SealBlob(plain, "blob", true);
	Require(sealed.has_value(), "blob seal");
	Require(Rows::OpenBlob(*sealed, "blob") == plain, "blob round trip");
	Require(Rows::SealBlob(*sealed, "blob", true) == sealed, "blob idempotence");
	Require(Rows::OpenBlob(*sealed, "other").empty(), "blob AAD mismatch");
	Require(Rows::OpenText("legacy text", "field") == "legacy text", "legacy text");
	Require(Rows::OpenBlob(plain, "blob") == plain, "legacy blob");
}

void StrictAndCompatibility() {
	const auto fixture = Fixture();
	TestRuntime::app.storage.storage.available = false;
	const auto blob = std::vector<char>{ 's', 'e', 'c', 'r', 'e', 't' };
	Require(!Rows::SealText("secret", "field", true), "strict text fell back");
	Require(!Rows::SealBlob(blob, "field", true), "strict blob fell back");
	Require(Rows::SealText("secret", "field", false) == "secret",
		"compatibility text fallback");
	Require(Rows::SealBlob(blob, "field", false) == blob, "compatibility blob fallback");
	Require(Rows::SealText("", "field", true).has_value(), "empty text refused");
	Require(Rows::SealBlob({}, "field", true).has_value(), "empty blob refused");
}

[[nodiscard]] DeletedMessage SampleRow() {
	auto row = DeletedMessage();
	row.userId = 42;
	row.dialogId = 12;
	row.messageId = 99;
	row.text = "message";
	row.fwdName = "name";
	row.fwdPostAuthor = "forward author";
	row.postAuthor = "author";
	row.mediaPath = "/private/file";
	row.mimeType = "application/octet-stream";
	row.hqThumbPath = "/private/thumb";
	row.textEntities = { 0, 1, 2 };
	row.documentSerialized = { 3, 4 };
	row.replySerialized = { 5, 6 };
	row.replyMarkupSerialized = { 7, 8 };
	row.thumbsSerialized = { 9, 10 };
	row.documentAttributesSerialized = { 11, 12 };
	return row;
}

void AllRowFields() {
	const auto fixture = Fixture();
	const auto original = SampleRow();
	Require(Rows::RowNeedsSeal(original), "plaintext row skipped");
	const auto sealed = Rows::SealRowCopy("DeletedMessage", original, true);
	Require(sealed.has_value(), "row seal");
	Require(!Rows::RowNeedsSeal(*sealed), "sealed row not skipped");
	Require(original.text == "message", "input row mutated");
	auto opened = *sealed;
	Rows::OpenRow("DeletedMessage", opened);
	for (const auto member : {
		&AyuMessageBase::text, &AyuMessageBase::fwdName,
		&AyuMessageBase::fwdPostAuthor, &AyuMessageBase::postAuthor,
		&AyuMessageBase::mediaPath, &AyuMessageBase::mimeType,
		&AyuMessageBase::hqThumbPath,
	}) {
		Require(opened.*member == original.*member, "row text field differs");
		Require((*sealed).*member != original.*member, "row field stayed plaintext");
	}
	for (const auto member : {
		&AyuMessageBase::textEntities, &AyuMessageBase::documentSerialized,
		&AyuMessageBase::replySerialized, &AyuMessageBase::replyMarkupSerialized,
		&AyuMessageBase::thumbsSerialized, &AyuMessageBase::documentAttributesSerialized,
	}) {
		Require(opened.*member == original.*member, "row blob field differs");
		Require((*sealed).*member != original.*member, "row blob stayed plaintext");
	}
	Require(opened.userId == original.userId && opened.messageId == original.messageId,
		"row identity mutated");
	TestRuntime::app.storage.storage.available = false;
	Require(!Rows::SealRowCopy("DeletedMessage", original, true), "strict row fallback");
}

void RowIdentityBinding() {
	const auto fixture = Fixture();
	const auto row = SampleRow();
	const auto sealed = Rows::SealRowCopy("DeletedMessage", row, true);
	Require(sealed.has_value(), "row seal");
	for (auto i = 0; i != 4; ++i) {
		auto wrong = *sealed;
		if (i == 0) ++wrong.userId;
		if (i == 1) ++wrong.dialogId;
		if (i == 2) ++wrong.messageId;
		Rows::OpenRow(i == 3 ? "EditedMessage" : "DeletedMessage", wrong);
		Require(wrong.text.empty() && wrong.textEntities.empty(),
			"row transplanted across identity");
	}
	auto wrong = *sealed;
	std::swap(wrong.text, wrong.fwdName);
	Rows::OpenRow("DeletedMessage", wrong);
	Require(wrong.text.empty() && wrong.fwdName.empty(), "column transplant accepted");
}


[[nodiscard]] std::string DatabaseScalar(
		const QByteArray &snapshot,
		const char *sql) {
	sqlite3 *raw = nullptr;
	const auto result = sqlite3_open_v2(
		":memory:",
		&raw,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
		nullptr);
	const auto handle = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>(
		raw, sqlite3_close);
	Require(result == SQLITE_OK, "open in-memory SQLite database");
	const auto size = sqlite3_int64(snapshot.size());
	const auto buffer = static_cast<unsigned char*>(sqlite3_malloc64(size));
	Require(buffer != nullptr, "allocate SQLite snapshot");
	std::memcpy(buffer, snapshot.constData(), size_t(size));
	const auto deserialized = sqlite3_deserialize(
		handle.get(),
		"main",
		buffer,
		size,
		size,
		SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_RESIZEABLE);
	if (deserialized != SQLITE_OK) {
		sqlite3_free(buffer);
	}
	Require(deserialized == SQLITE_OK, "deserialize SQLite snapshot");
	sqlite3_stmt *statement = nullptr;
	const auto prepared = sqlite3_prepare_v2(handle.get(), sql, -1, &statement, nullptr);
	const auto query = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>(
		statement, sqlite3_finalize);
	Require(prepared == SQLITE_OK, "prepare restored database query");
	Require(sqlite3_step(query.get()) == SQLITE_ROW, "restored row missing");
	const auto text = sqlite3_column_text(query.get(), 0);
	Require(text != nullptr, "restored SQLite value is null");
	return std::string(reinterpret_cast<const char*>(text));
}

[[nodiscard]] QString BackupPath() {
	const auto files = QDir(u"tdata/ayu_backups"_s).entryList(
		{ u"ayudata_*.enc"_s }, QDir::Files);
	Require(files.size() == 1, "expected one backup");
	return u"tdata/ayu_backups/"_s + files.front();
}

[[nodiscard]] QByteArray MainSnapshot() {
	auto bytes = QByteArray();
	Require(Archive::ReadFile(
		u"tdata/ayudata.enc"_s,
		"ayu-snapshot-v1",
		bytes), "read main snapshot");
	return bytes;
}

void InitializeBackupDatabase() {
	Require(QDir().mkpath(u"tdata"_s), "create database fixture");
	Database::initialize();
	Database::clearEverything();
	QDir(u"tdata/ayu_backups"_s).removeRecursively();
	Require(QDir().mkpath(u"tdata/ayu_backups"_s), "create backup fixture");
}

void ArchiveFgv1Migration() {
	const auto fixture = Fixture();
	AyuVault::SetKey(Core::App().domain().local().localKey());
	const auto ayv1Path = fixture.path(u"legacy-ayv1.enc"_s);
	const auto legacyPlain = QByteArray("legacy AYV1 payload\0bytes", 25);
	const auto ayv1 = FromHex(
		"415956319846ce4ebd51aad566f13282fb52077314598c48c465490d63b53abc"
		"4aaf966868787b1dbb54cb44531b83b785eb19eb366ca01402dd22d19b17bc76"
		"869f489f");
	Write(ayv1Path, ayv1);
	auto openedLegacy = QByteArray();
	auto ayv1Format = Archive::Format::Plain;
	Require(Archive::ReadFile(
		ayv1Path,
		"legacy-context-is-unbound",
		openedLegacy,
		&ayv1Format), "read AYV1 archive with TDesktop AES-IGE");
	Require(ayv1Format == Archive::Format::AyuVault
		&& openedLegacy == legacyPlain, "AYV1 payload differs");
	Require(Archive::NeedsMigration(ayv1Format), "AYV1 not marked for migration");
	const auto generatedPath = fixture.path(u"generated-ayv1.enc"_s);
	const auto generated = AyuVault::Seal(legacyPlain);
	Require(AyuVault::IsSealed(generated), "AYV1 writer output has no legacy envelope");
	Write(generatedPath, Bytes(generated.begin(), generated.end()));
	openedLegacy.clear();
	Require(Archive::ReadFile(generatedPath, "writer-context", openedLegacy)
		&& openedLegacy == legacyPlain, "AYV1 writer output did not roundtrip");
	TestRuntime::ResetKey(77);
	AyuVault::SetKey(Core::App().domain().local().localKey());
	Require(!Archive::ReadFile(ayv1Path, "wrong-key", openedLegacy),
		"AYV1 archive opened with a different local key");
	TestRuntime::ResetKey();
	AyuVault::SetKey(Core::App().domain().local().localKey());
	const auto path = fixture.path(u"legacy.enc"_s);
	const auto old = Golden(false);
	Write(path, old);
	auto plain = QByteArray();
	auto format = Archive::Format::Plain;
	Require(Archive::ReadFile(path, "fixture-context", plain, &format),
		"read FGV1 archive");
	Require(format == Archive::Format::Fgv1, "FGV1 format not identified");
	Require(Bytes(plain.begin(), plain.end()) == GoldenPlain(), "FGV1 plaintext differs");
	Require(Archive::NeedsMigration(format), "FGV1 not marked for migration");
	const auto migrated = fixture.path(u"migrated.enc"_s);
	Require(Archive::WriteFile(migrated, plain, "fixture-context"),
		"write FGV2 migration");
	plain.clear();
	Require(Archive::ReadFile(migrated, "fixture-context", plain, &format),
		"read FGV2 migration");
	Require(format == Archive::Format::Fgv2 && !Archive::NeedsMigration(format),
		"migration did not produce FGV2");
	Require(Bytes(plain.begin(), plain.end()) == GoldenPlain(),
		"migrated plaintext differs");
	const auto transplanted = fixture.path(u"transplanted.enc"_s);
	Require(QFile::copy(migrated, transplanted), "copy FGV2 fixture");
	plain = "stale";
	Require(!Archive::ReadFile(transplanted, "other-context", plain),
		"FGV2 accepted a different context");
	Require(plain.isEmpty(), "failed FGV2 open exposed stale output");
	TestRuntime::app.storage.storage.available = false;
	Write(transplanted, Pattern(13));
	Require(!Archive::WriteFile(transplanted, QByteArray("secret"), "context"),
		"FGV2 writer accepted a missing local key");
	Require(Read(transplanted) == Pattern(13), "key failure replaced destination");
}

void BackupRoundTrip() {
	const auto fixture = Fixture();
	InitializeBackupDatabase();
	Database::addDeletedMessages({ SampleRow() });
	Database::flush();
	AyuSettings::getInstance().strict = true;
	Require(AyuDatabaseBackup::create(), "sealed backup creation");
	const auto backup = BackupPath();
	const auto sealed = Read(backup);
	Require(Vault::IsBound(sealed), "backup stored plaintext");
	auto original = QByteArray();
	Require(Archive::ReadFile(
		backup,
		"ayu-backup-v1:" + QFileInfo(backup).fileName().toStdString(),
		original), "cannot verify backup payload");
	Require(DatabaseScalar(original, "PRAGMA quick_check") == "ok",
		"backup SQLite database corrupt");
	Require(DatabaseScalar(original, "SELECT count(*) FROM DeletedMessage") == "1",
		"deleted-message row missing from backup");
	Write(u"tdata/ayudata.enc"_s, Pattern(50));
	auto index = 0;
	Require(AyuDatabaseBackup::restore(index), "sealed backup restore");
	const auto restored = MainSnapshot();
	Require(restored == original, "restored database differs");
	Require(DatabaseScalar(restored, "PRAGMA quick_check") == "ok",
		"restored SQLite database corrupt");
	Require(DatabaseScalar(restored, "SELECT count(*) FROM DeletedMessage") == "1",
		"deleted message lost during backup/recovery");
	Require(!QFile::exists(u"tdata/ayudata.enc.restoring"_s), "restore staging leaked");
	const auto leftovers = QDir(u"tdata/ayu_backups"_s).entryList(
		{ u".scratch*"_s, u"*.opening"_s }, QDir::Files | QDir::Hidden);
	Require(leftovers.empty(), "plaintext scratch leaked after successful restore");
}

void BackupWrongKeyAndStrict() {
	const auto fixture = Fixture();
	InitializeBackupDatabase();
	Require(AyuDatabaseBackup::create(), "backup creation");
	TestRuntime::ResetKey(77);
	const auto sentinel = Pattern(123);
	Write(u"tdata/ayudata.enc"_s, sentinel);
	auto index = 0;
	Require(!AyuDatabaseBackup::restore(index), "wrong-key backup restored");
	Require(Read(u"tdata/ayudata.enc"_s) == sentinel, "failed recovery destroyed main");
	Require(!QFile::exists(u"tdata/ayudata.enc.restoring"_s), "failed recovery temp leaked");
}

void BackupLockedCreation() {
	const auto fixture = Fixture();
	InitializeBackupDatabase();
	TestRuntime::app.storage.storage.available = false;
	Require(!AyuDatabaseBackup::create(), "locked backup fell back");
	Require(QDir(u"tdata/ayu_backups"_s).entryList(
		{ u"ayudata_*.enc"_s }, QDir::Files).empty(), "strict plaintext backup exists");
	Require(QDir(u"tdata/ayu_backups"_s).entryList(
		QDir::Files | QDir::Hidden).empty(), "locked plaintext backup exists");
}

void RecoverStaging() {
	const auto fixture = Fixture();
	Require(QDir().mkpath(u"tdata"_s), "create fixture");
	Database::initialize();
	Database::clearEverything();
	const auto staging = u"tdata/ayudata.enc.restoring"_s;
	const auto expected = MainSnapshot();
	Require(Archive::WriteFile(staging, expected, "ayu-snapshot-v1"),
		"write valid staging snapshot");
	QFile::remove(u"tdata/ayudata.enc"_s);
	Write(u"tdata/ayudata.enc.restoring.opening"_s, Pattern(20));
	Write(u"tdata/ayudata.enc.restoring.abcdef.opening"_s, Pattern(20));
	Write(u"tdata/unrelated.opening"_s, Pattern(20));
	AyuDatabaseBackup::recoverStaging();
	Require(MainSnapshot() == expected, "valid staging not recovered");
	Require(!QFile::exists(u"tdata/ayudata.enc.restoring.opening"_s), "opening left");
	Require(!QFile::exists(u"tdata/ayudata.enc.restoring.abcdef.opening"_s),
		"unique opening left");
	Require(QFile::exists(u"tdata/unrelated.opening"_s), "unrelated opening removed");
	Write(staging, Pattern(30));
	AyuDatabaseBackup::recoverStaging();
	Require(MainSnapshot() == expected, "staging replaced existing main");
	Require(!QFile::exists(staging), "stale staging retained");
}

void MalformedStaging() {
	const auto fixture = Fixture();
	Require(QDir().mkpath(u"tdata"_s), "create fixture");
	Write(u"tdata/ayudata.enc.restoring"_s, Pattern(100));
	AyuDatabaseBackup::recoverStaging();
	Require(!QFile::exists(u"tdata/ayudata.enc"_s), "invalid staging installed");
}

void LegacyPrefixReadRegression() {
	const auto fixture = Fixture();
	Require(Rows::OpenText("ayu1:hello", "legacy") == "ayu1:hello",
		"legacy literal ayu1: text is erased before backfill");
}

void BackfillPrefixRegression() {
	const auto fixture = Fixture();
	auto row = DeletedMessage();
	row.text = "ayu2:hello";
	Require(Rows::RowNeedsSeal(row), "ayu2: plaintext collision skipped by backfill");
}

void EnvelopeLookingPlaintextRegression() {
	const auto fixture = Fixture();
	const auto bytes = Golden(true);
	const auto encoded = QByteArray(reinterpret_cast<const char*>(bytes.data()),
		int(bytes.size())).toBase64().toStdString();
	const auto literal = "ayu2:" + encoded;
	const auto sealed = Rows::SealText(literal, "new-row", true);
	Require(sealed.has_value(), "literal envelope seal refused");
	Require(Rows::OpenText(*sealed, "new-row") == literal,
		"user text matching an envelope was trusted without authentication");
}

void LockedEnvelopeLookingPlaintext() {
	const auto fixture = Fixture();
	const auto bytes = Golden(true);
	const auto literal = "ayu2:" + QByteArray(
		reinterpret_cast<const char*>(bytes.data()), int(bytes.size()))
		.toBase64().toStdString();
	TestRuntime::app.storage.storage.available = false;
	Require(!Rows::SealText(literal, "different-row", true),
		"strict locked write trusted an unverified foreign envelope literal");
}

void SealingTempCleanup() {
	const auto fixture = Fixture();
	InitializeBackupDatabase();
	const auto stale = u"tdata/ayu_backups/old.sealing"_s;
	Write(stale, Pattern(50));
	Require(AyuDatabaseBackup::create(), "backup creation");
	Require(!QFile::exists(stale), "stale sealing temp survives sweep");
}

void ScratchCleanupRegression() {
	const auto fixture = Fixture();
	InitializeBackupDatabase();
	const auto pending = u"tdata/ayu_backups/.pending-old.db"_s;
	const auto scratch = u"tdata/ayu_backups/.scratchABC123.db"_s;
	const auto opening = u"tdata/ayu_backups/old.opening"_s;
	Write(pending, Pattern(50));
	Write(scratch, Pattern(50));
	Write(opening, Pattern(50));
	Require(AyuDatabaseBackup::create(), "backup creation");
	Require(!QFile::exists(pending)
		&& !QFile::exists(scratch)
		&& !QFile::exists(opening),
		"hidden plaintext scratch survives cleanup");
}

void ScratchUpToDateRegression() {
	const auto fixture = Fixture();
	InitializeBackupDatabase();
	Require(AyuDatabaseBackup::create(), "backup creation");
	auto backup = QFile(BackupPath());
	Require(backup.open(QIODevice::ReadOnly), "open backup timestamp");
	Require(backup.setFileTime(QDateTime::currentDateTimeUtc().addSecs(60),
		QFileDevice::FileModificationTime), "set backup timestamp");
	backup.close();
	const auto scratch = u"tdata/ayu_backups/.scratchABC123.db"_s;
	Write(scratch, Pattern(50));
	const auto created = AyuDatabaseBackup::create();
	Require(!created, "up-to-date fixture unexpectedly created a backup");
	Require(!QFile::exists(scratch), "up-to-date early return skipped cleanup");
}

struct TestCase {
	const char *name = nullptr;
	void (*run)() = nullptr;
};

const auto Cases = std::array{
	TestCase{ "vault/inline-roundtrip", InlineRoundTrip },
	TestCase{ "vault/known-vectors", KnownVectors },
	TestCase{ "vault/nonces", Nonces },
	TestCase{ "vault/corruption", RejectCorruption },
	TestCase{ "vault/key-and-context", KeyAndContext },
	TestCase{ "vault/stream-boundaries", StreamBoundaries },
	TestCase{ "vault/failure-preserves-destination", StreamFailurePreservesDestination },
	TestCase{ "vault/files-and-legacy", FileApisAndLegacy },
	TestCase{ "vault/io-errors", StreamIoErrors },
	TestCase{ "vault/oversized-inline-file", OversizedInlineFile },
#if !defined(Q_OS_WIN)
	TestCase{ "vault/limited-file-writes", LimitedFileWrites },
	TestCase{ "vault/buffered-seal-write-error", BufferedSealWriteError },
	TestCase{ "vault/buffered-open-write-error", BufferedOpenWriteError },
#endif // !Q_OS_WIN
	TestCase{ "rows/text-and-blob", TextAndBlob },
	TestCase{ "rows/strict-and-compatibility", StrictAndCompatibility },
	TestCase{ "rows/all-fields", AllRowFields },
	TestCase{ "rows/identity-binding", RowIdentityBinding },
	TestCase{ "archive/ayv1-and-fgv1-migration", ArchiveFgv1Migration },
	TestCase{ "backup/roundtrip", BackupRoundTrip },
	TestCase{ "backup/wrong-key", BackupWrongKeyAndStrict },
	TestCase{ "backup/locked-creation", BackupLockedCreation },
	TestCase{ "backup/recover-staging", RecoverStaging },
	TestCase{ "backup/malformed-staging", MalformedStaging },
	TestCase{ "regression/legacy-prefix-read", LegacyPrefixReadRegression },
	TestCase{ "regression/backfill-prefix", BackfillPrefixRegression },
	TestCase{ "regression/envelope-looking-plaintext", EnvelopeLookingPlaintextRegression },
	TestCase{ "regression/locked-envelope-looking-plaintext", LockedEnvelopeLookingPlaintext },
	TestCase{ "regression/sealing-temp-cleanup", SealingTempCleanup },
	TestCase{ "regression/scratch-cleanup", ScratchCleanupRegression },
	TestCase{ "regression/scratch-up-to-date", ScratchUpToDateRegression },
};

} // namespace

int main(int argc, char **argv) {
	const auto application = QCoreApplication(argc, argv);
	auto filter = std::string();
	auto exact = false;
	if (argc == 2 && std::string(argv[1]) == "--list") {
		for (const auto &test : Cases) std::cout << test.name << '\n';
		return 0;
	}
	if (argc == 3) {
		const auto option = std::string(argv[1]);
		if (option != "--case" && option != "--suite") return 2;
		exact = option == "--case";
		filter = argv[2];
		if (!exact) filter += '/';
	} else if (argc != 1) {
		std::cerr << "Usage: crypto_tests [--list | --suite NAME | --case NAME]\n";
		return 2;
	}
	auto selected = 0;
	auto failed = 0;
	for (const auto &test : Cases) {
		const auto name = std::string(test.name);
		if (exact ? name != filter : name.rfind(filter, 0) != 0) continue;
		++selected;
		try {
			test.run();
			std::cout << "PASS " << name << std::endl;
		} catch (const std::exception &error) {
			++failed;
			std::cout << "FAIL " << name << ": " << error.what() << std::endl;
		}
	}
	std::cout << selected - failed << '/' << selected << " passed\n";
	return selected == 0 ? 2 : failed ? 1 : 0;
}
