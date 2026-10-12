#include "crypto_fixture.h"

#include "ayu/data/ayu_encryption.h"
#include "ayu/data/ayu_database.h"

#include <QtCore/QCoreApplication>
#include <sqlite3.h>

#include <array>
#include <iostream>
#include <memory>
#include <set>

struct sqlite3_api_routines;

namespace {

using namespace CryptoTest;
namespace Database = AyuDatabase;
namespace Rows = AyuDatabase::Encryption;

sqlite3 *TestDatabase = nullptr;

int captureTestDatabase(
		sqlite3 *db,
		char **,
		const sqlite3_api_routines *) {
	if (!TestDatabase) {
		TestDatabase = db;
	}
	return SQLITE_OK;
}

const auto kTestDatabaseCapture = [] {
	sqlite3_auto_extension(
		reinterpret_cast<void(*)(void)>(&captureTestDatabase));
	return true;
}();

class Sqlite {
public:
	Sqlite() {
		_db = TestDatabase;
		Require(_db != nullptr, "database is not initialized");
	}
	~Sqlite() = default;
	Sqlite(const Sqlite &) = delete;
	Sqlite &operator=(const Sqlite &) = delete;

	void exec(const char *sql) const {
		Require(sqlite3_exec(_db, sql, nullptr, nullptr, nullptr) == SQLITE_OK,
			std::string("sqlite exec: ") + sqlite3_errmsg(_db));
	}

	[[nodiscard]] std::string scalar(const char *sql) const {
		sqlite3_stmt *statement = nullptr;
		Require(sqlite3_prepare_v2(_db, sql, -1, &statement, nullptr) == SQLITE_OK,
			"sqlite prepare");
		const auto guard = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>(
			statement, &sqlite3_finalize);
		Require(sqlite3_step(statement) == SQLITE_ROW, "sqlite scalar missing");
		const auto value = sqlite3_column_text(statement, 0);
		return value ? std::string(reinterpret_cast<const char*>(value)) : std::string();
	}

private:
	sqlite3 *_db = nullptr;

};

void Initialize() {
	Require(QDir().mkpath("tdata"), "create tdata");
	Database::initialize();
}

[[nodiscard]] DeletedMessage Message(int id, std::string text = "needle") {
	auto row = DeletedMessage();
	row.userId = 42;
	row.dialogId = 12;
	row.messageId = id;
	row.date = 100;
	row.text = std::move(text);
	row.fromId = 12345;
	return row;
}

[[nodiscard]] DeletedExtra Extra() {
	auto row = DeletedExtra();
	row.userId = 42;
	row.dialogId = 12;
	row.messageId = 1;
	row.reactions = { 'r', 0, 'x' };
	row.repliers = "12345,67890";
	return row;
}

[[nodiscard]] DeletedMarkup Markup() {
	auto row = DeletedMarkup();
	row.userId = 42;
	row.dialogId = 12;
	row.messageId = 1;
	row.markup = { 'm', 0, 'x' };
	return row;
}

[[nodiscard]] KeptDialog Dialog() {
	auto row = KeptDialog();
	row.userId = 42;
	row.dialogId = 12;
	row.title = "private title";
	row.username = "private username";
	return row;
}

[[nodiscard]] KeptTopic Topic() {
	auto row = KeptTopic();
	row.userId = 42;
	row.dialogId = 12;
	row.rootId = 7;
	row.title = "private topic";
	return row;
}

[[nodiscard]] KnownUser User() {
	auto row = KnownUser();
	row.userId = 42;
	row.peerId = 19;
	row.firstName = "first";
	row.lastName = "last";
	row.username = "username";
	return row;
}

void SaveMetadata() {
	Database::addDeletedExtras({ Extra() });
	Database::addDeletedMarkups({ Markup() });
	Database::saveKeptDialog(Dialog());
	Database::saveKeptTopics({ Topic() });
	Database::saveKnownUsers({ User() });
}

void CheckMetadata() {
	const auto extras = Database::getDeletedExtras(42, 12);
	Require(extras.size() == 1 && extras.front().reactions == Extra().reactions
		&& extras.front().repliers == Extra().repliers, "extra roundtrip");
	const auto markups = Database::getDeletedMarkups(42, 12);
	Require(markups.size() == 1 && markups.front().markup == Markup().markup,
		"markup roundtrip");
	const auto dialogs = Database::getKeptDialogs(42);
	Require(dialogs.size() == 1 && dialogs.front().title == Dialog().title
		&& dialogs.front().username == Dialog().username, "dialog roundtrip");
	const auto topics = Database::getKeptTopics(42, 12);
	Require(topics.size() == 1 && topics.front().title == Topic().title,
		"topic roundtrip");
	const auto user = Database::getKnownUser(42, 19);
	Require(user && user->firstName == User().firstName
		&& user->lastName == User().lastName && user->username == User().username,
		"known user roundtrip");
}

void Metadata() {
	const auto fixture = Fixture();
	Initialize();
	SaveMetadata();
	CheckMetadata();
	const auto sql = Sqlite();
	Require(sql.scalar("SELECT hex(markup) FROM DeletedMarkup").starts_with("46475632"),
		"markup stayed plaintext");
	Require(sql.scalar("SELECT hex(reactions) FROM DeletedExtra").starts_with("46475632"),
		"reactions stayed plaintext");
	for (const auto query : {
		"SELECT repliers FROM DeletedExtra", "SELECT title FROM KeptDialog",
		"SELECT username FROM KeptDialog", "SELECT title FROM KeptTopic",
		"SELECT firstName FROM KnownUser", "SELECT lastName FROM KnownUser",
		"SELECT username FROM KnownUser",
	}) {
		Require(sql.scalar(query).starts_with("ayu2:"), "metadata stayed plaintext");
	}
}

void StrictReplace() {
	const auto fixture = Fixture();
	Initialize();
	SaveMetadata();
	TestRuntime::app.storage.storage.available = false;
	AyuSettings::getInstance().strict = true;
	auto extra = Extra();
	extra.repliers = "replacement";
	Database::addDeletedExtras({ extra });
	auto markup = Markup();
	markup.markup = { 'n' };
	Database::addDeletedMarkups({ markup });
	auto dialog = Dialog();
	dialog.title = "replacement";
	Database::saveKeptDialog(dialog);
	auto topic = Topic();
	topic.title = "replacement";
	Database::saveKeptTopics({ topic });
	auto user = User();
	user.firstName = "replacement";
	Database::saveKnownUsers({ user });
	TestRuntime::ResetKey();
	CheckMetadata();
}

void SyncDialogs() {
	const auto fixture = Fixture();
	Initialize();
	Database::syncKeptDialogs({ Dialog() });
	const auto sql = Sqlite();
	const auto stored = sql.scalar("SELECT title FROM KeptDialog");
	Require(stored.starts_with("ayu2:"), "sync wrote plaintext");
	Require(sql.scalar("SELECT username FROM KeptDialog").starts_with("ayu2:"),
		"sync wrote plaintext username");
	TestRuntime::app.storage.storage.available = false;
	AyuSettings::getInstance().strict = true;
	auto dialog = Dialog();
	dialog.title = "replacement";
	Database::syncKeptDialogs({ dialog });
	Require(sql.scalar("SELECT title FROM KeptDialog") == stored,
		"strict sync deleted old row");
	TestRuntime::ResetKey();
	dialog.lost = 1;
	Database::saveKeptDialog(dialog);
	Database::syncKeptDialogs({ Dialog() });
	const auto retained = Database::getKeptDialogs(42).front();
	Require(retained.lost == 1 && retained.title == dialog.title
		&& retained.username == dialog.username, "sync reset lost dialog");
}

void SearchTies() {
	const auto fixture = Fixture();
	Initialize();
	auto rows = std::vector<DeletedMessage>();
	for (auto id = 1; id <= 1001; ++id) {
		rows.push_back(Message(id));
	}
	Database::addDeletedMessages(rows);
	const auto matches = Database::searchDeletedMessages(42, 0, 0, 0, "NEEDLE", 2000);
	Require(matches.size() == rows.size(), "same-date keyset lost or repeated rows");
	auto ids = std::set<int>();
	auto previous = 1002;
	for (const auto &row : matches) {
		Require(row.messageId < previous, "same-date order differs from cursor");
		previous = row.messageId;
		Require(row.text == "needle", "search returned ciphertext");
		ids.insert(row.messageId);
	}
	Require(ids.size() == rows.size(), "duplicate search rows");
	const auto sql = Sqlite();
	sql.exec("UPDATE DeletedMessage SET date=200 WHERE messageId>700");
	sql.exec("UPDATE DeletedMessage SET date=50 WHERE messageId<=300");
	const auto groups = Database::searchDeletedMessages(42, 0, 0, 0, "needle", -1);
	Require(groups.size() == rows.size(), "date boundary lost rows");
	auto previousDate = 201;
	previous = 1002;
	ids.clear();
	for (const auto &row : groups) {
		Require(row.date < previousDate
			|| (row.date == previousDate && row.messageId < previous), "date keyset order");
		previousDate = row.date;
		previous = row.messageId;
		ids.insert(row.messageId);
	}
	Require(ids.size() == rows.size(), "date boundary duplicate rows");
}

void SearchLimits() {
	const auto fixture = Fixture();
	Initialize();
	Database::addDeletedMessages({ Message(1), Message(2), Message(3) });
	for (const auto limit : { -1, 0, 1, 2, 9 }) {
		const auto expected = limit < 0 ? 3 : std::min(limit, 3);
		Require(int(Database::searchDeletedMessages(42, 0, 0, 0, "needle", limit).size())
			== expected, "search limit contract");
		Require(int(Database::getDeletedMessages(42, 12, 0, 0, 0, limit, "needle").size())
			== expected, "message search limit contract");
	}
}

void SearchFilters() {
	const auto fixture = Fixture();
	Initialize();
	auto rows = std::vector<DeletedMessage>();
	for (auto id = 1; id <= 16; ++id) {
		auto row = Message(id, "Юникод %_\\ literal");
		row.dialogId = id % 2 ? 12 : 13;
		row.topicId = id & 2 ? 7 : 8;
		row.fromId = id & 4 ? 12345 : 54321;
		row.documentType = id & 8 ? 1 : 0;
		rows.push_back(row);
	}
	Database::addDeletedMessages(rows);
	Require(Database::searchDeletedMessages(42, 0, 0, 0, "юникод", 99).size() == 16,
		"unicode search");
	Require(Database::searchDeletedMessages(42, 12, 7, 12345, "%_\\", 99, true).size() == 1,
		"search filters or literal SQL wildcard changed");
	Require(Database::searchDeletedMessages(42, 12, 0, 0, "", 99).size() == 8,
		"dialog filter");
	Require(Database::searchDeletedMessages(42, 0, 7, 0, "", 99).size() == 8,
		"topic filter");
	Require(Database::searchDeletedMessages(42, 0, 0, 12345, "", 99).size() == 8,
		"sender filter");
	Require(Database::searchDeletedMessages(42, 0, 0, 0, "", 99, true).size() == 8,
		"media filter");
	Require(Database::searchDeletedMessages(43, 0, 0, 0, "", 99).empty(), "user isolation");
	Require(Database::searchDeletedMessages(42, 0, 0, 0, "", 99).size() == 16,
		"empty-query search");
}

void MessageSearch() {
	const auto fixture = Fixture();
	Initialize();
	auto rows = std::vector<DeletedMessage>();
	for (auto id = 1; id <= 205; ++id) {
		auto row = Message(id, id == 1 ? "last-page-needle" : "other");
		row.fromId = id == 2 ? 12345 : 7;
		rows.push_back(row);
	}
	Database::addDeletedMessages(rows);
	const auto text = Database::getDeletedMessages(42, 12, 0, 0, 0, 9, "needle");
	Require(text.size() == 1 && text.front().messageId == 1, "message page boundary");
	const auto sender = Database::getDeletedMessages(42, 12, 0, 0, 0, 9, "12345");
	Require(sender.size() == 1 && sender.front().messageId == 2, "sender-id search");
	Require(Database::getDeletedMessages(42, 12, 0, 2, 8, 9, "needle").empty(),
		"min/max bounds");
}

void Backfill() {
	const auto fixture = Fixture();
	Initialize();
	TestRuntime::app.storage.storage.available = false;
	SaveMetadata();
	const auto sql = Sqlite();
	Require(sql.scalar("SELECT title FROM KeptDialog") == Dialog().title, "legacy fixture");
	TestRuntime::ResetKey();
	Database::initialize();
	CheckMetadata();
	Require(sql.scalar("SELECT title FROM KeptDialog").starts_with("ayu2:"),
		"metadata backfill skipped");
	Require(sql.scalar("SELECT hex(markup) FROM DeletedMarkup").starts_with("46475632"),
		"markup backfill skipped");
}

void LockedStart() {
	const auto fixture = Fixture();
	Require(QDir().mkpath("tdata/ayu_backups"), "create locked fixture");
	const auto pending = QString("tdata/ayu_backups/.pending-old.db");
	const auto sentinel = Pattern(23);
	Write(pending, sentinel);
	TestRuntime::app.storage.storage.available = false;
	Database::initialize();
	Require(!QFile::exists("tdata/ayudata.enc"), "locked startup created snapshot");
	Require(Read(pending) == sentinel, "locked startup swept existing files");
	Require(Database::getKeptDialogs(42).empty(), "locked database exposed data");
	TestRuntime::ResetKey();
	Initialize();
	SaveMetadata();
	CheckMetadata();
}

void RegexIdentity() {
	const auto fixture = Fixture();
	Initialize();
	auto filter = RegexFilter();
	filter.id = { 'a', 0, 'b' };
	filter.text = "private-regex";
	filter.enabled = true;
	auto dialog = filter;
	dialog.dialogId = 0;
	Require(Rows::RegexFilterContext(filter, "text") != Rows::RegexFilterContext(dialog, "text"),
		"shared and dialog zero contexts collide");
	Database::addRegexFilter(filter);
	Require(Database::getById(filter.id).text == filter.text, "regex roundtrip");
	const auto sql = Sqlite();
	sql.exec("UPDATE RegexFilter SET id=x'610063'");
	Require(Database::getById({ 'a', 0, 'c' }).text.empty(), "regex id transplant accepted");
}

void LegacyRegex() {
	const auto fixture = Fixture();
	Initialize();
	auto filter = RegexFilter();
	filter.id = { 'a', 0, 'b' };
	filter.text = "private-regex";
	filter.enabled = true;
	const auto oldContext = Rows::RowContext("RegexFilter", 0, 0, 0, "text");
	const auto old = Rows::SealText(filter.text, oldContext, true);
	Require(old.has_value(), "legacy regex fixture");
	const auto sql = Sqlite();
	const auto query = "INSERT INTO RegexFilter VALUES(x'610062','" + *old + "',1,0,0,NULL)";
	sql.exec(query.c_str());
	Require(Database::getById(filter.id).text == filter.text, "legacy regex read");
	Database::initialize();
	Require(Database::getById(filter.id).text == filter.text, "legacy regex backfill lost text");
	Require(sql.scalar("SELECT text FROM RegexFilter") != *old, "legacy regex not migrated");
	sql.exec("UPDATE RegexFilter SET id=x'610063'");
	Require(Database::getById({ 'a', 0, 'c' }).text.empty(), "migrated regex id not bound");
}

struct TestCase {
	const char *name;
	void (*run)();
};

const auto Cases = std::array{
	TestCase{ "metadata", Metadata }, TestCase{ "strict_replace", StrictReplace },
	TestCase{ "sync_dialogs", SyncDialogs }, TestCase{ "search_ties", SearchTies },
	TestCase{ "search_limits", SearchLimits }, TestCase{ "search_filters", SearchFilters },
	TestCase{ "message_search", MessageSearch }, TestCase{ "backfill", Backfill },
	TestCase{ "locked_start", LockedStart },
	TestCase{ "regex_identity", RegexIdentity }, TestCase{ "legacy_regex", LegacyRegex },
};

} // namespace

int main(int argc, char *argv[]) {
	const auto application = QCoreApplication(argc, argv);
	if (argc != 2) {
		return 2;
	}
	for (const auto &test : Cases) {
		if (std::string(argv[1]) == test.name) {
			try {
				test.run();
				std::cout << "PASS " << test.name << '\n';
				return 0;
			} catch (const std::exception &error) {
				std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
				return 1;
			}
		}
	}
	return 2;
}
