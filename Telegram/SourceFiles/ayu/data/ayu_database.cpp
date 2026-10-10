// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/ayu_database.h"

#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_database_backup.h"
#include "ayu/data/entities.h"
#include "ayu/libs/sqlite/sqlite_orm.h"
#include "ayu/secret/secret_vault.h"
#include "ayu/utils/file_perms.h"
#include "ayu/utils/id_search.h"
#include "base/unixtime.h"
#include "crl/crl_async.h"

#include <QtCore/QByteArray>

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>

using namespace sqlite_orm;
auto storage = make_storage(
	"./tdata/ayudata.db",
	make_table<SchemaVersion>(
		"SchemaVersion",
		make_column("id", &SchemaVersion::id, primary_key()),
		make_column("version", &SchemaVersion::version)
	),
	make_index("idx_deleted_message_userId_dialogId_topicId_messageId",
			   column<DeletedMessage>(&DeletedMessage::userId),
			   column<DeletedMessage>(&DeletedMessage::dialogId),
			   column<DeletedMessage>(&DeletedMessage::topicId),
			   column<DeletedMessage>(&DeletedMessage::messageId)),
	make_index("idx_deleted_message_userId_dialogId_messageId",
			   column<DeletedMessage>(&DeletedMessage::userId),
			   column<DeletedMessage>(&DeletedMessage::dialogId),
			   column<DeletedMessage>(&DeletedMessage::messageId)),
	make_index("idx_edited_message_userId_dialogId_messageId",
			   column<EditedMessage>(&EditedMessage::userId),
			   column<EditedMessage>(&EditedMessage::dialogId),
			   column<EditedMessage>(&EditedMessage::messageId)),
	make_table<DeletedMessage>(
		"DeletedMessage",
		make_column("fakeId", &DeletedMessage::fakeId, primary_key().autoincrement()),
		make_column("userId", &DeletedMessage::userId),
		make_column("dialogId", &DeletedMessage::dialogId),
		make_column("groupedId", &DeletedMessage::groupedId),
		make_column("peerId", &DeletedMessage::peerId),
		make_column("fromId", &DeletedMessage::fromId),
		make_column("topicId", &DeletedMessage::topicId),
		make_column("messageId", &DeletedMessage::messageId),
		make_column("date", &DeletedMessage::date),
		make_column("flags", &DeletedMessage::flags),
		make_column("editDate", &DeletedMessage::editDate),
		make_column("views", &DeletedMessage::views),
		make_column("fwdFlags", &DeletedMessage::fwdFlags),
		make_column("fwdFromId", &DeletedMessage::fwdFromId),
		make_column("fwdName", &DeletedMessage::fwdName),
		make_column("fwdDate", &DeletedMessage::fwdDate),
		make_column("fwdPostAuthor", &DeletedMessage::fwdPostAuthor),
		make_column("replyFlags", &DeletedMessage::replyFlags),
		make_column("replyMessageId", &DeletedMessage::replyMessageId),
		make_column("replyPeerId", &DeletedMessage::replyPeerId),
		make_column("replyTopId", &DeletedMessage::replyTopId),
		make_column("replyForumTopic", &DeletedMessage::replyForumTopic),
		make_column("replySerialized", &DeletedMessage::replySerialized),
		make_column("entityCreateDate", &DeletedMessage::entityCreateDate),
		make_column("text", &DeletedMessage::text),
		make_column("textEntities", &DeletedMessage::textEntities),
		make_column("mediaPath", &DeletedMessage::mediaPath),
		make_column("hqThumbPath", &DeletedMessage::hqThumbPath),
		make_column("documentType", &DeletedMessage::documentType),
		make_column("documentSerialized", &DeletedMessage::documentSerialized),
		make_column("thumbsSerialized", &DeletedMessage::thumbsSerialized),
		make_column("documentAttributesSerialized", &DeletedMessage::documentAttributesSerialized),
		make_column("mimeType", &DeletedMessage::mimeType)
	),
	make_table<EditedMessage>(
		"EditedMessage",
		make_column("fakeId", &EditedMessage::fakeId, primary_key().autoincrement()),
		make_column("userId", &EditedMessage::userId),
		make_column("dialogId", &EditedMessage::dialogId),
		make_column("groupedId", &EditedMessage::groupedId),
		make_column("peerId", &EditedMessage::peerId),
		make_column("fromId", &EditedMessage::fromId),
		make_column("topicId", &EditedMessage::topicId),
		make_column("messageId", &EditedMessage::messageId),
		make_column("date", &EditedMessage::date),
		make_column("flags", &EditedMessage::flags),
		make_column("editDate", &EditedMessage::editDate),
		make_column("views", &EditedMessage::views),
		make_column("fwdFlags", &EditedMessage::fwdFlags),
		make_column("fwdFromId", &EditedMessage::fwdFromId),
		make_column("fwdName", &EditedMessage::fwdName),
		make_column("fwdDate", &EditedMessage::fwdDate),
		make_column("fwdPostAuthor", &EditedMessage::fwdPostAuthor),
		make_column("replyFlags", &EditedMessage::replyFlags),
		make_column("replyMessageId", &EditedMessage::replyMessageId),
		make_column("replyPeerId", &EditedMessage::replyPeerId),
		make_column("replyTopId", &EditedMessage::replyTopId),
		make_column("replyForumTopic", &EditedMessage::replyForumTopic),
		make_column("replySerialized", &EditedMessage::replySerialized),
		make_column("entityCreateDate", &EditedMessage::entityCreateDate),
		make_column("text", &EditedMessage::text),
		make_column("textEntities", &EditedMessage::textEntities),
		make_column("mediaPath", &EditedMessage::mediaPath),
		make_column("hqThumbPath", &EditedMessage::hqThumbPath),
		make_column("documentType", &EditedMessage::documentType),
		make_column("documentSerialized", &EditedMessage::documentSerialized),
		make_column("thumbsSerialized", &EditedMessage::thumbsSerialized),
		make_column("documentAttributesSerialized", &EditedMessage::documentAttributesSerialized),
		make_column("mimeType", &EditedMessage::mimeType)
	),
	make_table<DeletedDialog>(
		"DeletedDialog",
		make_column("fakeId", &DeletedDialog::fakeId, primary_key().autoincrement()),
		make_column("userId", &DeletedDialog::userId),
		make_column("dialogId", &DeletedDialog::dialogId),
		make_column("peerId", &DeletedDialog::peerId),
		make_column("folderId", &DeletedDialog::folderId),
		make_column("topMessage", &DeletedDialog::topMessage),
		make_column("lastMessageDate", &DeletedDialog::lastMessageDate),
		make_column("flags", &DeletedDialog::flags),
		make_column("entityCreateDate", &DeletedDialog::entityCreateDate)
	),
	make_table<KeptDialog>(
		"KeptDialog",
		make_column("fakeId", &KeptDialog::fakeId, primary_key().autoincrement()),
		make_column("userId", &KeptDialog::userId),
		make_column("dialogId", &KeptDialog::dialogId),
		make_column("kind", &KeptDialog::kind),
		make_column("title", &KeptDialog::title),
		make_column("username", &KeptDialog::username),
		make_column("accessHash", &KeptDialog::accessHash),
		make_column("folderId", &KeptDialog::folderId),
		make_column("lastMessageDate", &KeptDialog::lastMessageDate),
		make_column("lost", &KeptDialog::lost)
	),
	make_table<KeptTopic>(
		"KeptTopic",
		make_column("fakeId", &KeptTopic::fakeId, primary_key().autoincrement()),
		make_column("userId", &KeptTopic::userId),
		make_column("dialogId", &KeptTopic::dialogId),
		make_column("rootId", &KeptTopic::rootId),
		make_column("title", &KeptTopic::title),
		make_column("colorId", &KeptTopic::colorId),
		make_column("iconId", &KeptTopic::iconId),
		make_column("creatorId", &KeptTopic::creatorId),
		make_column("date", &KeptTopic::date),
		make_column("flags", &KeptTopic::flags)
	),
	make_index("idx_known_user_userId_peerId",
			   column<KnownUser>(&KnownUser::userId),
			   column<KnownUser>(&KnownUser::peerId)),
	make_index("idx_secret_chat_userId_chatId",
			   column<SecretChatRow>(&SecretChatRow::userId),
			   column<SecretChatRow>(&SecretChatRow::chatId)),
	make_index("idx_secret_message_userId_chatId_randomId",
			   column<SecretMessageRow>(&SecretMessageRow::userId),
			   column<SecretMessageRow>(&SecretMessageRow::chatId),
			   column<SecretMessageRow>(&SecretMessageRow::randomId)),
	make_table<KnownUser>(
		"KnownUser",
		make_column("fakeId", &KnownUser::fakeId, primary_key().autoincrement()),
		make_column("userId", &KnownUser::userId),
		make_column("peerId", &KnownUser::peerId),
		make_column("accessHash", &KnownUser::accessHash),
		make_column("firstName", &KnownUser::firstName),
		make_column("lastName", &KnownUser::lastName),
		make_column("username", &KnownUser::username),
		make_column("updatedAt", &KnownUser::updatedAt)
	),
	make_table<SecretChatRow>(
		"SecretChat",
		make_column("fakeId", &SecretChatRow::fakeId, primary_key().autoincrement()),
		make_column("userId", &SecretChatRow::userId),
		make_column("chatId", &SecretChatRow::chatId),
		make_column("accessHash", &SecretChatRow::accessHash),
		make_column("peerUserId", &SecretChatRow::peerUserId),
		make_column("creator", &SecretChatRow::creator),
		make_column("state", &SecretChatRow::state),
		make_column("keyData", &SecretChatRow::keyData),
		make_column("fingerprint", &SecretChatRow::fingerprint),
		make_column("myIn", &SecretChatRow::myIn),
		make_column("myOut", &SecretChatRow::myOut),
		make_column("hisIn", &SecretChatRow::hisIn),
		make_column("hisLayer", &SecretChatRow::hisLayer),
		make_column("date", &SecretChatRow::date),
		make_column("lastDate", &SecretChatRow::lastDate),
		make_column("unread", &SecretChatRow::unread)
	),
	make_table<SecretMessageRow>(
		"SecretMessage",
		make_column("fakeId", &SecretMessageRow::fakeId, primary_key().autoincrement()),
		make_column("userId", &SecretMessageRow::userId),
		make_column("chatId", &SecretMessageRow::chatId),
		make_column("randomId", &SecretMessageRow::randomId),
		make_column("outgoing", &SecretMessageRow::outgoing),
		make_column("date", &SecretMessageRow::date),
		make_column("kind", &SecretMessageRow::kind),
		make_column("seqIn", &SecretMessageRow::seqIn),
		make_column("seqOut", &SecretMessageRow::seqOut),
		make_column("text", &SecretMessageRow::text),
		make_column("payload", &SecretMessageRow::payload)
	),
	make_table<PeekedStatusRow>(
		"PeekedStatus",
		make_column("fakeId", &PeekedStatusRow::fakeId, primary_key().autoincrement()),
		make_column("userId", &PeekedStatusRow::userId),
		make_column("targetId", &PeekedStatusRow::targetId),
		make_column("kind", &PeekedStatusRow::kind),
		make_column("time", &PeekedStatusRow::time),
		make_column("checkedAt", &PeekedStatusRow::checkedAt)
	),
	make_table<PeekRestoreRow>(
		"PeekRestore",
		make_column("fakeId", &PeekRestoreRow::fakeId, primary_key().autoincrement()),
		make_column("userId", &PeekRestoreRow::userId),
		make_column("option", &PeekRestoreRow::option),
		make_column("flags", &PeekRestoreRow::flags),
		make_column("always", &PeekRestoreRow::always),
		make_column("never", &PeekRestoreRow::never)
	),
	make_table<SecretStateRow>(
		"SecretState",
		make_column("fakeId", &SecretStateRow::fakeId, primary_key().autoincrement()),
		make_column("userId", &SecretStateRow::userId),
		make_column("qts", &SecretStateRow::qts),
		make_column("date", &SecretStateRow::date)
	),
	make_table<RegexFilter>(
		"RegexFilter",
		make_column("id", &RegexFilter::id, primary_key()),
		make_column("text", &RegexFilter::text),
		make_column("enabled", &RegexFilter::enabled),
		make_column("reversed", &RegexFilter::reversed),
		make_column("caseInsensitive", &RegexFilter::caseInsensitive),
		make_column("dialogId", &RegexFilter::dialogId)
	),
	make_table<RegexFilterGlobalExclusion>(
		"RegexFilterGlobalExclusion",
		make_column("fakeId", &RegexFilterGlobalExclusion::fakeId, primary_key().autoincrement()),
		make_column("dialogId", &RegexFilterGlobalExclusion::dialogId),
		make_column("filterId", &RegexFilterGlobalExclusion::filterId)
	),
	make_table<SpyMessageRead>(
		"SpyMessageRead",
		make_column("fakeId", &SpyMessageRead::fakeId, primary_key().autoincrement()),
		make_column("userId", &SpyMessageRead::userId),
		make_column("dialogId", &SpyMessageRead::dialogId),
		make_column("messageId", &SpyMessageRead::messageId),
		make_column("entityCreateDate", &SpyMessageRead::entityCreateDate)
	),
	make_table<SpyMessageContentsRead>(
		"SpyMessageContentsRead",
		make_column("fakeId", &SpyMessageContentsRead::fakeId, primary_key().autoincrement()),
		make_column("userId", &SpyMessageContentsRead::userId),
		make_column("dialogId", &SpyMessageContentsRead::dialogId),
		make_column("messageId", &SpyMessageContentsRead::messageId),
		make_column("entityCreateDate", &SpyMessageContentsRead::entityCreateDate)
	)
);

namespace {

std::recursive_mutex DatabaseMutex;
bool DatabaseReady = false;
std::atomic<int> PurgeDays = 0;

constexpr auto kBusyTimeoutMs = 5000;
constexpr auto kSqliteBusy = 5;
constexpr auto kSqliteLocked = 6;
constexpr auto kSqliteCorrupt = 11;
constexpr auto kSqliteNotADatabase = 26;

template<typename Result, typename Callback>
Result run(const char *what, Result fallback, Callback &&callback) {
	std::lock_guard lock(DatabaseMutex);
	if (!DatabaseReady) {
		return fallback;
	}
	try {
		return callback();
	} catch (const std::exception &ex) {
		LOG(("[AyuGram] Database: failed to %1: %2").arg(QString::fromUtf8(what), QString::fromUtf8(ex.what())));
	} catch (...) {
		LOG(("[AyuGram] Database: failed to %1.").arg(QString::fromUtf8(what)));
	}
	return fallback;
}

template<typename Callback>
void runVoid(const char *what, Callback &&callback) {
	run<bool>(what, false, [&] {
		callback();
		return true;
	});
}

template<typename Callback>
void inTransaction(Callback &&callback) {
	storage.begin_transaction();
	try {
		callback();
		storage.commit();
	} catch (...) {
		try {
			storage.rollback();
		} catch (...) {
		}
		throw;
	}
}

// At-rest envelope for message content columns, using the existing Vault
// (AES-256-GCM over localKey, no new crypto). TEXT columns carry
// "ayu1:" + base64(sealed) so the schema is untouched; BLOB columns
// carry the sealed bytes directly. Absent markers mean legacy plaintext,
// which keeps working. A present-but-unopenable marker means corruption
// or a key mismatch and decodes to empty (logged), never to raw bytes.
constexpr auto kSealedTextPrefix = "ayu1:";

[[nodiscard]] std::string RowContext(
		const char *table,
		const AyuMessageBase &message,
		const char *field) {
	auto result = std::string("ayu-row-v1:");
	result += table;
	result += ':';
	result += std::to_string(message.userId);
	result += ':';
	result += std::to_string(message.dialogId);
	result += ':';
	result += std::to_string(message.messageId);
	result += ':';
	result += field;
	return result;
}

// Strict mode (settings): Vault-unavailable or seal failure refuses the
// write instead of silently falling back to plaintext. Refusals lose the
// record by design; the default stays compatible (plaintext fallback).
[[nodiscard]] bool StrictMode() {
	return AyuSettings::getInstance().requireEncryption();
}

[[nodiscard]] std::optional<std::string> SealText(
		const std::string &plain,
		const std::string &context) {
	if (plain.empty() || plain.rfind(kSealedTextPrefix, 0) == 0) {
		return plain;
	}
	const auto sealed = AyuSecret::Vault::Available()
		? AyuSecret::Vault::Seal(
			AyuSecret::Bytes(plain.begin(), plain.end()),
			context)
		: AyuSecret::Bytes();
	if (sealed.empty()) {
		if (StrictMode()) {
			LOG(("[AyuGram] Database: refusing plaintext text field."));
			return std::nullopt;
		}
		return plain;
	}
	const auto base64 = QByteArray(
		reinterpret_cast<const char*>(sealed.data()),
		int(sealed.size())).toBase64().toStdString();
	return kSealedTextPrefix + base64;
}

[[nodiscard]] std::string OpenText(
		const std::string &stored,
		const std::string &context) {
	if (stored.rfind(kSealedTextPrefix, 0) != 0) {
		return stored;
	}
	const auto raw = QByteArray::fromBase64(
		QByteArray::fromStdString(stored.substr(5)));
	const auto sealed = AyuSecret::Bytes(raw.begin(), raw.end());
	auto plain = AyuSecret::Bytes();
	if (!AyuSecret::Vault::IsSealed(sealed)
		|| !AyuSecret::Vault::Open(sealed, plain, context)) {
		LOG(("[AyuGram] Database: cannot open sealed text field (%1)."
			).arg(QString::fromStdString(context)));
		return std::string();
	}
	return std::string(plain.begin(), plain.end());
}

[[nodiscard]] std::optional<std::vector<char>> SealBlob(
		const std::vector<char> &plain,
		const std::string &context) {
	if (plain.empty()) {
		return plain;
	}
	const auto asBytes = AyuSecret::Bytes(plain.begin(), plain.end());
	if (AyuSecret::Vault::IsSealed(asBytes)) {
		return plain;
	}
	const auto sealed = AyuSecret::Vault::Available()
		? AyuSecret::Vault::Seal(asBytes, context)
		: AyuSecret::Bytes();
	if (sealed.empty()) {
		if (StrictMode()) {
			LOG(("[AyuGram] Database: refusing plaintext blob field."));
			return std::nullopt;
		}
		return plain;
	}
	return std::vector<char>(sealed.begin(), sealed.end());
}

[[nodiscard]] std::vector<char> OpenBlob(
		const std::vector<char> &stored,
		const std::string &context) {
	const auto sealed = AyuSecret::Bytes(stored.begin(), stored.end());
	if (!AyuSecret::Vault::IsSealed(sealed)) {
		return stored;
	}
	auto plain = AyuSecret::Bytes();
	if (!AyuSecret::Vault::Open(sealed, plain, context)) {
		LOG(("[AyuGram] Database: cannot open sealed blob field (%1)."
			).arg(QString::fromStdString(context)));
		return std::vector<char>();
	}
	return std::vector<char>(plain.begin(), plain.end());
}


template<typename Message>
[[nodiscard]] std::optional<Message> SealRowCopy(
		const char *table,
		const Message &message) {
	auto result = message;
	const auto context = [&](const char *field) {
		return RowContext(table, message, field);
	};
	const auto sealText = [&](std::string &field, const char *name) {
		auto sealed = SealText(field, context(name));
		if (!sealed) {
			return false;
		}
		field = std::move(*sealed);
		return true;
	};
	const auto sealBlob = [&](std::vector<char> &field, const char *name) {
		auto sealed = SealBlob(field, context(name));
		if (!sealed) {
			return false;
		}
		field = std::move(*sealed);
		return true;
	};
	if (!sealText(result.text, "text")
		|| !sealBlob(result.textEntities, "textEntities")
		|| !sealBlob(result.documentSerialized, "documentSerialized")
		|| !sealBlob(result.replySerialized, "replySerialized")
		|| !sealBlob(result.replyMarkupSerialized, "replyMarkupSerialized")
		|| !sealText(result.fwdName, "fwdName")
		|| !sealText(result.fwdPostAuthor, "fwdPostAuthor")
		|| !sealText(result.postAuthor, "postAuthor")
		|| !sealBlob(result.thumbsSerialized, "thumbsSerialized")
		|| !sealBlob(
			result.documentAttributesSerialized,
			"documentAttributesSerialized")
		|| !sealText(result.mediaPath, "mediaPath")
		|| !sealText(result.mimeType, "mimeType")
		|| !sealText(result.hqThumbPath, "hqThumbPath")) {
		return std::nullopt;
	}
	return result;
}

void OpenRow(const char *table, AyuMessageBase &message) {
	const auto context = [&](const char *field) {
		return RowContext(table, message, field);
	};
	message.text = OpenText(message.text, context("text"));
	message.textEntities = OpenBlob(message.textEntities, context("textEntities"));
	message.documentSerialized = OpenBlob(
		message.documentSerialized,
		context("documentSerialized"));
	message.replySerialized = OpenBlob(
		message.replySerialized,
		context("replySerialized"));
	message.replyMarkupSerialized = OpenBlob(
		message.replyMarkupSerialized,
		context("replyMarkupSerialized"));
	message.fwdName = OpenText(message.fwdName, context("fwdName"));
	message.fwdPostAuthor = OpenText(
		message.fwdPostAuthor,
		context("fwdPostAuthor"));
	message.postAuthor = OpenText(message.postAuthor, context("postAuthor"));
	message.thumbsSerialized = OpenBlob(
		message.thumbsSerialized,
		context("thumbsSerialized"));
	message.documentAttributesSerialized = OpenBlob(
		message.documentAttributesSerialized,
		context("documentAttributesSerialized"));
	message.mediaPath = OpenText(message.mediaPath, context("mediaPath"));
	message.mimeType = OpenText(message.mimeType, context("mimeType"));
	message.hqThumbPath = OpenText(message.hqThumbPath, context("hqThumbPath"));
}

template<typename Message>
[[nodiscard]] bool RowNeedsSeal(const Message &message) {
	const auto textNeeds = [&](const std::string &field) {
		return !field.empty() && field.rfind(kSealedTextPrefix, 0) != 0;
	};
	const auto blobNeeds = [&](const std::vector<char> &field) {
		return !field.empty()
			&& !AyuSecret::Vault::IsSealed(
				AyuSecret::Bytes(field.begin(), field.end()));
	};
	return textNeeds(message.text)
		|| blobNeeds(message.textEntities)
		|| blobNeeds(message.documentSerialized)
		|| blobNeeds(message.replySerialized)
		|| blobNeeds(message.replyMarkupSerialized)
		|| textNeeds(message.fwdName)
		|| textNeeds(message.fwdPostAuthor)
		|| textNeeds(message.postAuthor)
		|| blobNeeds(message.thumbsSerialized)
		|| blobNeeds(message.documentAttributesSerialized)
		|| textNeeds(message.mediaPath)
		|| textNeeds(message.mimeType)
		|| textNeeds(message.hqThumbPath);
}

constexpr auto kBackfillBatch = 500;

template<typename Message>
void backfillTable(const char *table, const char *what) {
	auto lastId = ID(0);
	while (true) {
		const auto batch = run<std::vector<Message>>(what, {}, [&] {
			return storage.get_all<Message>(
				where(column<Message>(&Message::fakeId) > lastId),
				order_by(column<Message>(&Message::fakeId)),
				limit(kBackfillBatch));
		});
		if (batch.empty()) {
			break;
		}
		try {
			inTransaction([&] {
				for (const auto &row : batch) {
					if (!RowNeedsSeal(row)) {
						continue;
					}
					if (const auto sealed = SealRowCopy(table, row)) {
						storage.update(*sealed);
					}
				}
			});
		} catch (...) {
			LOG(("[AyuGram] Database: backfill batch failed, will retry."));
			break;
		}
		lastId = batch.back().fakeId;
		if (int(batch.size()) < kBackfillBatch) {
			break;
		}
	}
}

// One-time (idempotent, resumable) migration of legacy plaintext rows.
// Runs in the background after startup; rows sealed by newer versions
// are skipped, so an interrupted run simply continues next time.
void backfillSealedRows() {
	if (!AyuSecret::Vault::Available()) {
		return;
	}
	backfillTable<DeletedMessage>("DeletedMessage", "backfill deleted");
	backfillTable<EditedMessage>("EditedMessage", "backfill edited");
}

QString databasePath() {
	return "./tdata/ayudata.db";
}

void renameDatabaseFiles(const QString &suffix) {
	for (const auto &extension : {QString(), QString("-shm"), QString("-wal")}) {
		const auto from = databasePath() + extension;
		if (QFile::exists(from)) {
			const auto to = QString("./tdata/ayudata_%1.db%2").arg(suffix, extension);
			if (QFile::rename(from, to)) {
				AyuUtils::RestrictFile(to);
			}
		}
	}
}

void copyDatabaseFiles(const QString &suffix) {
	for (const auto &extension : {QString(), QString("-shm"), QString("-wal")}) {
		const auto from = databasePath() + extension;
		if (QFile::exists(from)) {
			const auto to = QString("./tdata/ayudata_%1.db%2").arg(suffix, extension);
			if (QFile::copy(from, to)) {
				AyuUtils::RestrictFile(to);
			}
		}
	}
}

}

namespace AyuMigrations {

void migrateToV1(decltype(storage) &storage) {
	// drop RegexFilter table as we've added primary_key()
	try {
		storage.drop_table_if_exists("RegexFilter");
		LOG(("Migration to V1 successful."));
	} catch (const std::exception &ex) {
		LOG(("Migration to V1 failed: %1").arg(ex.what()));
	}
}

void migrateToV2(decltype(storage) &storage) {
	storage.remove_all<DeletedMessage>(
		where(not_in(
			column<DeletedMessage>(&DeletedMessage::fakeId),
			select(
				max(column<DeletedMessage>(&DeletedMessage::fakeId)),
				group_by(
					column<DeletedMessage>(&DeletedMessage::userId),
					column<DeletedMessage>(&DeletedMessage::dialogId),
					column<DeletedMessage>(&DeletedMessage::messageId)
				)
			)
		))
	);
	LOG(("Migration to V2 successful."));
}

void migrateToV3(decltype(storage) &storage) {
	storage.remove_all<KeptDialog>(
		where(column<KeptDialog>(&KeptDialog::lost) == 0));
	LOG(("Migration to V3 successful."));
}

}

void runMigrations(decltype(storage) &storage) {
	constexpr int kLatestVersion = 3;

	const std::map<int, Fn<void(decltype(storage) &)>> migrations = {
		{1, AyuMigrations::migrateToV1},
		{2, AyuMigrations::migrateToV2},
		{3, AyuMigrations::migrateToV3},
	};

	int currentVersion = 0;
	try {
		if (auto versionRow = storage.get_pointer<SchemaVersion>(1)) {
			currentVersion = versionRow->version;
		} else {
			storage.insert(SchemaVersion{1, 0});
		}
	} catch (...) {
		LOG(("No SchemaVersion, assuming 0"));
		storage.insert(SchemaVersion{1, 0});
	}

	if (currentVersion >= kLatestVersion) {
		LOG(("Database is ok"));
		return;
	}

	LOG(("Database version: %1. Latest version: %2.").arg(currentVersion).arg(kLatestVersion));

	for (int v = currentVersion + 1; v <= kLatestVersion; ++v) {
		if (migrations.contains(v)) {
			try {
				LOG(("Migration for version: %1").arg(v));
				storage.begin_transaction();

				migrations.at(v)(storage);

				storage.update_all(set(c(&SchemaVersion::version) = v), where(c(&SchemaVersion::id) == 1));
				storage.commit();
				LOG(("Applied migration for version: %1.").arg(v));
			} catch (...) {
				try {
					storage.rollback();
				} catch (...) {
				}
				LOG(("Failed to apply migration for version: %1.").arg(v));
				AyuDatabase::backupCurrentDatabase();

				return;
			}
		}
	}
}

namespace AyuDatabase {

void moveCurrentDatabase() {
	renameDatabaseFiles(QString::number(base::unixtime::now()));
}

void backupCurrentDatabase() {
	copyDatabaseFiles(QString("backup_%1").arg(base::unixtime::now()));
}

namespace {

void prepareStorage() {
	storage.sync_schema(true);

	runMigrations(storage);

	storage.sync_schema(true);

	storage.on_open = [](sqlite3 *db) {
		sqlite3_exec(db, "PRAGMA secure_delete = ON", nullptr, nullptr, nullptr);
	};
	storage.open_forever();
	try {
		storage.pragma.journal_mode(sqlite_orm::journal_mode::WAL);
		storage.pragma.synchronous(1);
		storage.busy_timeout(kBusyTimeoutMs);
	} catch (const std::exception &ex) {
		LOG(("[AyuGram] Database: failed to apply pragmas: %1").arg(ex.what()));
	}
}

}

void initialize() {
	std::lock_guard lock(DatabaseMutex);
	DatabaseReady = false;

	[[maybe_unused]] const auto backedUp = AyuDatabaseBackup::create();

	auto retriedBusy = false;
	auto resetCorrupted = false;
	auto restoreIndex = 0;
	while (true) {
		try {
			prepareStorage();
			for (const auto &extension : {QString(), QString("-shm"), QString("-wal")}) {
				AyuUtils::RestrictFile(databasePath() + extension);
			}
			DatabaseReady = true;
			AyuDatabaseBackup::startPeriodic();
			if (const auto days = PurgeDays.load()) {
				purgeOlderThan(days);
			}
			crl::async([] {
				try {
					backfillSealedRows();
				} catch (...) {
				}
			});
			return;
		} catch (const std::system_error &ex) {
			const auto code = ex.code().value() & 0xFF;
			const auto sqliteError = (ex.code().category() == sqlite_orm::get_sqlite_error_category());
			LOG(("[AyuGram] Database initialization failed (%1): %2").arg(code).arg(ex.what()));
			if (sqliteError && (code == kSqliteBusy || code == kSqliteLocked) && !retriedBusy) {
				retriedBusy = true;
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
				continue;
			}
			if (sqliteError && (code == kSqliteCorrupt || code == kSqliteNotADatabase)) {
				moveCurrentDatabase();
				if (AyuDatabaseBackup::restore(restoreIndex)) {
					continue;
				} else if (!resetCorrupted) {
					resetCorrupted = true;
					continue;
				}
			}
			break;
		} catch (const std::exception &ex) {
			LOG(("[AyuGram] Database initialization failed: %1").arg(ex.what()));
			break;
		}
	}

	LOG(("[AyuGram] Database is unavailable, messages will not be saved in this session."));
}

void purgeOlderThan(int days) {
	if (days <= 0) {
		PurgeDays = 0;
		return;
	}
	PurgeDays = days;
	crl::async([=] {
		const auto cutoff = int(base::unixtime::now() - int64(days) * 86400);
		runVoid("purge old messages", [&] {
			inTransaction([&] {
				storage.remove_all<DeletedMessage>(
					where(column<DeletedMessage>(&DeletedMessage::entityCreateDate) < cutoff));
				storage.remove_all<EditedMessage>(
					where(column<EditedMessage>(&EditedMessage::entityCreateDate) < cutoff));
			});
		});
	});
}

void addEditedMessage(const EditedMessage &message, int maxRevisions) {
	runVoid("save edited message", [&] {
		inTransaction([&] {
			const auto saved = storage.count<EditedMessage>(
				where(
					column<EditedMessage>(&EditedMessage::userId) == message.userId and
					column<EditedMessage>(&EditedMessage::dialogId) == message.dialogId and
					column<EditedMessage>(&EditedMessage::messageId) == message.messageId
				));
			if (!maxRevisions || saved < maxRevisions) {
				if (const auto sealed = SealRowCopy("EditedMessage", message)) {
					storage.insert(*sealed);
				} else {
					LOG(("[AyuGram] Database: strict mode refused an edited message."));
				}
			}
		});
	});
}

std::vector<EditedMessage> getEditedMessages(ID userId, ID dialogId, ID messageId, ID minId, ID maxId, int totalLimit) {
	return run<std::vector<EditedMessage>>("load edited messages", {}, [&] {
		auto result = storage.get_all<EditedMessage>(
			where(
				column<EditedMessage>(&EditedMessage::userId) == userId and
				column<EditedMessage>(&EditedMessage::dialogId) == dialogId and
				column<EditedMessage>(&EditedMessage::messageId) == messageId and
				(column<EditedMessage>(&EditedMessage::fakeId) > minId or minId == 0) and
				(column<EditedMessage>(&EditedMessage::fakeId) < maxId or maxId == 0)
			),
			order_by(column<EditedMessage>(&EditedMessage::fakeId)).desc(),
			limit(totalLimit)
		);
		for (auto &row : result) {
			OpenRow("EditedMessage", row);
		}
		return result;
	});
}

bool hasRevisions(ID userId, ID dialogId, ID messageId) {
	return run<bool>("check edited messages", false, [&] {
		return !storage.select(
			columns(column<EditedMessage>(&EditedMessage::messageId)),
			where(
				column<EditedMessage>(&EditedMessage::userId) == userId and
				column<EditedMessage>(&EditedMessage::dialogId) == dialogId and
				column<EditedMessage>(&EditedMessage::messageId) == messageId
			),
			limit(1)
		).empty();
	});
}

void addDeletedMessages(const std::vector<DeletedMessage> &messages) {
	if (messages.empty()) {
		return;
	}
	runVoid("save deleted messages", [&] {
		inTransaction([&] {
			for (const auto &message : messages) {
				const auto exists = storage.count<DeletedMessage>(
					where(
						column<DeletedMessage>(&DeletedMessage::userId) == message.userId and
						column<DeletedMessage>(&DeletedMessage::dialogId) == message.dialogId and
						column<DeletedMessage>(&DeletedMessage::messageId) == message.messageId
					)
				) > 0;
				if (!exists) {
					if (const auto sealed = SealRowCopy("DeletedMessage", message)) {
						storage.insert(*sealed);
					} else {
						LOG(("[AyuGram] Database: strict mode refused a deleted message."));
					}
				}
			}
		});
	});
}

void addDeletedMessage(const DeletedMessage &message) {
	addDeletedMessages({message});
}

std::vector<DeletedMessage> getDeletedMessages(ID userId, ID dialogId, ID topicId, ID minId, ID maxId, int totalLimit, const std::string &searchQuery) {
	return run<std::vector<DeletedMessage>>("load deleted messages", {}, [&] {
		if (searchQuery.empty()) {
			auto result = storage.get_all<DeletedMessage>(
				where(
					column<DeletedMessage>(&DeletedMessage::userId) == userId and
					column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
					(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0) and
					(column<DeletedMessage>(&DeletedMessage::messageId) > minId or minId == 0) and
					(column<DeletedMessage>(&DeletedMessage::messageId) < maxId or maxId == 0)
				),
				order_by(column<DeletedMessage>(&DeletedMessage::messageId)).desc(),
				limit(totalLimit)
			);
			for (auto &row : result) {
				OpenRow("DeletedMessage", row);
			}
			return result;
		}

		// Text is sealed at rest, so SQL LIKE cannot see it: scan the
		// indexed id range in pages, decrypt text only, and collect up
		// to totalLimit matches with the same case-insensitive substring
		// + sender-id semantics.
		if (totalLimit == 0) {
			return std::vector<DeletedMessage>();
		}
		const auto idQuery = AyuIdSearch::Parse(
			QString::fromStdString(searchQuery).trimmed());
		const auto senderId = idQuery.valid() ? ID(idQuery.id) : ID(-1);
		const auto needle = QString::fromStdString(searchQuery);
		constexpr auto kSearchPage = 200;
		auto result = std::vector<DeletedMessage>();
		auto bound = maxId;
		while (true) {
			auto page = storage.get_all<DeletedMessage>(
				where(
					column<DeletedMessage>(&DeletedMessage::userId) == userId and
					column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
					(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0) and
					(column<DeletedMessage>(&DeletedMessage::messageId) > minId or minId == 0) and
					(column<DeletedMessage>(&DeletedMessage::messageId) < bound or bound == 0)
				),
				order_by(column<DeletedMessage>(&DeletedMessage::messageId)).desc(),
				limit(kSearchPage)
			);
			if (page.empty()) {
				break;
			}
			for (auto &row : page) {
				const auto text = OpenText(
					row.text,
					RowContext("DeletedMessage", row, "text"));
				if (QString::fromStdString(text).contains(needle, Qt::CaseInsensitive)
					|| row.fromId == senderId) {
					OpenRow("DeletedMessage", row);
					result.push_back(row);
					if (int(result.size()) >= totalLimit) {
						return result;
					}
				}
			}
			if (int(page.size()) < kSearchPage) {
				break;
			}
			bound = page.back().messageId;
		}
		return result;
	});
}

bool hasDeletedMessages(ID userId, ID dialogId, ID topicId) {
	return run<bool>("check deleted messages", false, [&] {
		return !storage.select(
			columns(column<DeletedMessage>(&DeletedMessage::dialogId)),
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0)
			),
			limit(1)
		).empty();
	});
}

std::vector<ID> getDeletedDialogIds(ID userId) {
	return run<std::vector<ID>>("load deleted dialogs", {}, [&] {
		return storage.select(
			distinct(column<DeletedMessage>(&DeletedMessage::dialogId)),
			where(column<DeletedMessage>(&DeletedMessage::userId) == userId));
	});
}

void removeDeletedMessage(ID userId, ID dialogId, ID messageId) {
	runVoid("remove deleted message", [&] {
		storage.remove_all<DeletedMessage>(
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				column<DeletedMessage>(&DeletedMessage::messageId) == messageId
			)
		);
	});
}

void clearDeletedMessages(ID userId, ID dialogId, ID topicId) {
	runVoid("clear deleted messages", [&] {
		storage.remove_all<DeletedMessage>(
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0)
			)
		);
	});
}

void saveKeptDialog(const KeptDialog &dialog) {
	runVoid("save kept dialog", [&] {
		inTransaction([&] {
			storage.remove_all<KeptDialog>(
				where(
					column<KeptDialog>(&KeptDialog::userId) == dialog.userId and
					column<KeptDialog>(&KeptDialog::dialogId) == dialog.dialogId
				)
			);
			storage.insert(dialog);
		});
	});
}

void saveKeptTopics(const std::vector<KeptTopic> &topics) {
	if (topics.empty()) {
		return;
	}
	runVoid("save kept topics", [&] {
		inTransaction([&] {
			for (const auto &topic : topics) {
				storage.remove_all<KeptTopic>(
					where(
						column<KeptTopic>(&KeptTopic::userId) == topic.userId and
						column<KeptTopic>(&KeptTopic::dialogId) == topic.dialogId and
						column<KeptTopic>(&KeptTopic::rootId) == topic.rootId
					)
				);
				storage.insert(topic);
			}
		});
	});
}

std::vector<KeptTopic> getKeptTopics(ID userId, ID dialogId) {
	return run<std::vector<KeptTopic>>("load kept topics", {}, [&] {
		return storage.get_all<KeptTopic>(
			where(
				column<KeptTopic>(&KeptTopic::userId) == userId and
				column<KeptTopic>(&KeptTopic::dialogId) == dialogId
			));
	});
}

std::vector<KeptTopic> getKeptTopicsFor(ID userId) {
	return run<std::vector<KeptTopic>>("load all kept topics", {}, [&] {
		return storage.get_all<KeptTopic>(
			where(column<KeptTopic>(&KeptTopic::userId) == userId));
	});
}

void removeKeptTopic(ID userId, ID dialogId, ID rootId) {
	runVoid("remove kept topic", [&] {
		storage.remove_all<KeptTopic>(
			where(
				column<KeptTopic>(&KeptTopic::userId) == userId and
				column<KeptTopic>(&KeptTopic::dialogId) == dialogId and
				column<KeptTopic>(&KeptTopic::rootId) == rootId
			)
		);
	});
}

void removeKeptTopics(ID userId, ID dialogId) {
	runVoid("remove kept topics", [&] {
		storage.remove_all<KeptTopic>(
			where(
				column<KeptTopic>(&KeptTopic::userId) == userId and
				column<KeptTopic>(&KeptTopic::dialogId) == dialogId
			)
		);
	});
}

void syncKeptDialogs(const std::vector<KeptDialog> &dialogs) {
	if (dialogs.empty()) {
		return;
	}
	runVoid("sync kept dialogs", [&] {
		inTransaction([&] {
			for (const auto &dialog : dialogs) {
				const auto lost = storage.count<KeptDialog>(
					where(
						column<KeptDialog>(&KeptDialog::userId) == dialog.userId and
						column<KeptDialog>(&KeptDialog::dialogId) == dialog.dialogId and
						column<KeptDialog>(&KeptDialog::lost) == 1
					));
				if (lost) {
					continue;
				}
				storage.remove_all<KeptDialog>(
					where(
						column<KeptDialog>(&KeptDialog::userId) == dialog.userId and
						column<KeptDialog>(&KeptDialog::dialogId) == dialog.dialogId
					)
				);
				storage.insert(dialog);
			}
		});
	});
}

std::vector<KeptDialog> getKeptDialogs(ID userId) {
	return run<std::vector<KeptDialog>>("load kept dialogs", {}, [&] {
		return storage.get_all<KeptDialog>(
			where(column<KeptDialog>(&KeptDialog::userId) == userId));
	});
}

void removeKeptDialog(ID userId, ID dialogId) {
	runVoid("remove kept dialog", [&] {
		storage.remove_all<KeptDialog>(
			where(
				column<KeptDialog>(&KeptDialog::userId) == userId and
				column<KeptDialog>(&KeptDialog::dialogId) == dialogId
			)
		);
	});
}

void saveKnownUsers(const std::vector<KnownUser> &users) {
	if (users.empty()) {
		return;
	}
	runVoid("save known users", [&] {
		inTransaction([&] {
			for (const auto &user : users) {
				const auto same = [&](const KnownUser &row) {
					return row.accessHash == user.accessHash
						&& row.firstName == user.firstName
						&& row.lastName == user.lastName
						&& row.username == user.username;
				};
				const auto sameUser = where(
					column<KnownUser>(&KnownUser::userId) == user.userId and
					column<KnownUser>(&KnownUser::peerId) == user.peerId);
				const auto existing = storage.get_all<KnownUser>(sameUser, limit(1));
				if (!existing.empty() && same(existing.front())) {
					continue;
				}
				storage.remove_all<KnownUser>(sameUser);
				storage.insert(user);
			}
		});
	});
}

std::optional<KnownUser> getKnownUser(ID userId, ID peerId) {
	return run<std::optional<KnownUser>>("load known user", std::nullopt, [&] {
		auto rows = storage.get_all<KnownUser>(
			where(
				column<KnownUser>(&KnownUser::userId) == userId and
				column<KnownUser>(&KnownUser::peerId) == peerId
			),
			limit(1));
		return rows.empty()
			? std::optional<KnownUser>()
			: std::optional<KnownUser>(std::move(rows.front()));
	});
}

void saveSecretChat(const SecretChatRow &chat) {
	runVoid("save secret chat", [&] {
		inTransaction([&] {
			storage.remove_all<SecretChatRow>(
				where(
					column<SecretChatRow>(&SecretChatRow::userId) == chat.userId and
					column<SecretChatRow>(&SecretChatRow::chatId) == chat.chatId
				)
			);
			storage.insert(chat);
		});
	});
}

std::vector<SecretChatRow> getSecretChats(ID userId) {
	return run<std::vector<SecretChatRow>>("load secret chats", {}, [&] {
		return storage.get_all<SecretChatRow>(
			where(column<SecretChatRow>(&SecretChatRow::userId) == userId));
	});
}

void removeSecretChat(ID userId, int chatId) {
	runVoid("remove secret chat", [&] {
		inTransaction([&] {
			storage.remove_all<SecretChatRow>(
				where(
					column<SecretChatRow>(&SecretChatRow::userId) == userId and
					column<SecretChatRow>(&SecretChatRow::chatId) == chatId
				)
			);
			storage.remove_all<SecretMessageRow>(
				where(
					column<SecretMessageRow>(&SecretMessageRow::userId) == userId and
					column<SecretMessageRow>(&SecretMessageRow::chatId) == chatId
				)
			);
		});
	});
}

bool addSecretMessage(const SecretMessageRow &message) {
	return run<bool>("save secret message", false, [&] {
		const auto exists = storage.count<SecretMessageRow>(
			where(
				column<SecretMessageRow>(&SecretMessageRow::userId) == message.userId and
				column<SecretMessageRow>(&SecretMessageRow::chatId) == message.chatId and
				column<SecretMessageRow>(&SecretMessageRow::randomId) == message.randomId
			)
		) > 0;
		if (exists) {
			return false;
		}
		storage.insert(message);
		return true;
	});
}

void updateSecretMessage(const SecretMessageRow &message) {
	runVoid("update secret message", [&] {
		storage.update_all(
			set(
				assign(&SecretMessageRow::kind, message.kind),
				assign(&SecretMessageRow::outgoing, message.outgoing),
				assign(&SecretMessageRow::seqIn, message.seqIn),
				assign(&SecretMessageRow::seqOut, message.seqOut),
				assign(&SecretMessageRow::date, message.date),
				assign(&SecretMessageRow::text, message.text),
				assign(&SecretMessageRow::payload, message.payload)
			),
			where(
				column<SecretMessageRow>(&SecretMessageRow::userId) == message.userId and
				column<SecretMessageRow>(&SecretMessageRow::chatId) == message.chatId and
				column<SecretMessageRow>(&SecretMessageRow::randomId) == message.randomId
			)
		);
	});
}

std::vector<SecretMessageRow> getSecretMessages(ID userId, int chatId) {
	return run<std::vector<SecretMessageRow>>("load secret messages", {}, [&] {
		return storage.get_all<SecretMessageRow>(
			where(
				column<SecretMessageRow>(&SecretMessageRow::userId) == userId and
				column<SecretMessageRow>(&SecretMessageRow::chatId) == chatId
			),
			order_by(column<SecretMessageRow>(&SecretMessageRow::fakeId)).asc()
		);
	});
}

void removeSecretMessage(ID userId, int chatId, ID randomId) {
	runVoid("remove secret message", [&] {
		storage.remove_all<SecretMessageRow>(
			where(
				column<SecretMessageRow>(&SecretMessageRow::userId) == userId and
				column<SecretMessageRow>(&SecretMessageRow::chatId) == chatId and
				column<SecretMessageRow>(&SecretMessageRow::randomId) == randomId
			)
		);
	});
}

void clearSecretMessages(ID userId, int chatId) {
	runVoid("clear secret messages", [&] {
		storage.remove_all<SecretMessageRow>(
			where(
				column<SecretMessageRow>(&SecretMessageRow::userId) == userId and
				column<SecretMessageRow>(&SecretMessageRow::chatId) == chatId
			)
		);
	});
}

void savePeekedStatus(const PeekedStatusRow &row) {
	runVoid("save peeked status", [&] {
		inTransaction([&] {
			storage.remove_all<PeekedStatusRow>(
				where(
					column<PeekedStatusRow>(&PeekedStatusRow::userId) == row.userId and
					column<PeekedStatusRow>(&PeekedStatusRow::targetId) == row.targetId
				)
			);
			storage.insert(row);
		});
	});
}

std::vector<PeekedStatusRow> getPeekedStatus(ID userId, ID targetId) {
	return run<std::vector<PeekedStatusRow>>("load peeked status", {}, [&] {
		return storage.get_all<PeekedStatusRow>(
			where(
				column<PeekedStatusRow>(&PeekedStatusRow::userId) == userId and
				column<PeekedStatusRow>(&PeekedStatusRow::targetId) == targetId
			)
		);
	});
}

void savePeekRestore(const PeekRestoreRow &row) {
	runVoid("save peek restore", [&] {
		inTransaction([&] {
			storage.remove_all<PeekRestoreRow>(
				where(column<PeekRestoreRow>(&PeekRestoreRow::userId) == row.userId));
			storage.insert(row);
		});
	});
}

void saveSecretState(const SecretStateRow &row) {
	runVoid("save secret state", [&] {
		inTransaction([&] {
			storage.remove_all<SecretStateRow>(
				where(column<SecretStateRow>(&SecretStateRow::userId) == row.userId));
			storage.insert(row);
		});
	});
}

std::vector<SecretStateRow> getSecretState(ID userId) {
	return run<std::vector<SecretStateRow>>("load secret state", {}, [&] {
		return storage.get_all<SecretStateRow>(
			where(column<SecretStateRow>(&SecretStateRow::userId) == userId));
	});
}

std::vector<PeekRestoreRow> getPeekRestore(ID userId) {
	return run<std::vector<PeekRestoreRow>>("load peek restore", {}, [&] {
		return storage.get_all<PeekRestoreRow>(
			where(column<PeekRestoreRow>(&PeekRestoreRow::userId) == userId));
	});
}

void clearPeekRestore(ID userId) {
	runVoid("clear peek restore", [&] {
		storage.remove_all<PeekRestoreRow>(
			where(column<PeekRestoreRow>(&PeekRestoreRow::userId) == userId));
	});
}

template<typename T>
std::vector<T> getAllT() {
	return run<std::vector<T>>("load all", {}, [&] {
		return storage.get_all<T>();
	});
}

std::vector<RegexFilter> getAllRegexFilters() {
	return getAllT<RegexFilter>();
}

std::vector<RegexFilterGlobalExclusion> getAllFiltersExclusions() {
	return getAllT<RegexFilterGlobalExclusion>();
}

std::vector<RegexFilter> getExcludedByDialogId(ID dialogId) {
	return run<std::vector<RegexFilter>>("load excluded filters", {}, [&] {
		return storage.get_all<RegexFilter>(
			where(in(&RegexFilter::id,
					 storage.select(columns(&RegexFilterGlobalExclusion::filterId),
									where(is_equal(&RegexFilterGlobalExclusion::dialogId, dialogId))
					 )
			))
		);
	});
}

int getCount() {
	return run<int>("count filters", 0, [&] {
		return storage.count<RegexFilter>();
	});
}

RegexFilter getById(std::vector<char> id) {
	return run<RegexFilter>("load filter", RegexFilter{}, [&] {
		return storage.get<RegexFilter>(
			where(column<RegexFilter>(&RegexFilter::id) == std::move(id))
		);
	});
}

std::vector<RegexFilter> getShared() {
	return run<std::vector<RegexFilter>>("load shared filters", {}, [&] {
		return storage.get_all<RegexFilter>(
			where(is_null(column<RegexFilter>(&RegexFilter::dialogId)))
		);
	});
}

std::vector<RegexFilter> getByDialogId(ID dialogId) {
	return run<std::vector<RegexFilter>>("load dialog filters", {}, [&] {
		return storage.get_all<RegexFilter>(
			where(column<RegexFilter>(&RegexFilter::dialogId) == dialogId)
		);
	});
}

void addRegexFilter(const RegexFilter &filter) {
	runVoid("save regex filter", [&] {
		inTransaction([&] {
			storage.replace(filter); // we're using replace as we set std::vector<char> as primary key
		});
	});
}

void addRegexExclusion(const RegexFilterGlobalExclusion &exclusion) {
	runVoid("save regex filter exclusion", [&] {
		inTransaction([&] {
			storage.insert(exclusion);
		});
	});
}

void updateRegexFilter(const RegexFilter &filter) {
	runVoid("update regex filter", [&] {
		storage.update_all(
			set(
				c(&RegexFilter::text) = filter.text,
				c(&RegexFilter::enabled) = filter.enabled,
				c(&RegexFilter::reversed) = filter.reversed,
				c(&RegexFilter::caseInsensitive) = filter.caseInsensitive,
				c(&RegexFilter::dialogId) = filter.dialogId
			),
			where(c(&RegexFilter::id) == filter.id)
		);
	});
}

void deleteFilter(const std::vector<char> &id) {
	runVoid("delete regex filter", [&] {
		storage.remove_all<RegexFilter>(
			where(column<RegexFilter>(&RegexFilter::id) == id)
		);
	});
}

void deleteExclusionsByFilterId(const std::vector<char> &id) {
	runVoid("delete regex filter exclusions", [&] {
		storage.remove_all<RegexFilterGlobalExclusion>(
			where(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::filterId) == id)
		);
	});
}

void deleteExclusion(ID dialogId, std::vector<char> filterId) {
	runVoid("delete regex filter exclusion", [&] {
		storage.remove_all<RegexFilterGlobalExclusion>(
			where(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::filterId) == filterId and
				column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::dialogId) == dialogId
			)
		);
	});
}

void deleteAllFilters() {
	runVoid("delete all regex filters", [&] {
		storage.remove_all<RegexFilter>();
	});
}

void deleteAllExclusions() {
	runVoid("delete all regex filter exclusions", [&] {
		storage.remove_all<RegexFilterGlobalExclusion>();
	});
}

bool hasFilters() {
	return run<bool>("check regex filters", false, [&] {
		return !storage.select(
			columns(column<RegexFilter>(&RegexFilter::id)),
			limit(1)
		).empty();
	});
}

bool hasPerDialogFilters() {
	return run<bool>("check per dialog filters", false, [&] {
		return
			!storage.select(
				columns(column<RegexFilter>(&RegexFilter::id)),
				where(is_not_null(column<RegexFilter>(&RegexFilter::dialogId))),
				limit(1)
			).empty() ||
			!storage.select(
				columns(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::fakeId)),
				limit(1)
			).empty();
	});
}

}
