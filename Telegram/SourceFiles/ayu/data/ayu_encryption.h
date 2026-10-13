// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>

#include "ayu/data/entities.h"
#include "ayu/secret/secret_vault.h"
#include "logs.h"

#include <cstring>

namespace AyuDatabase::Encryption {

// At-rest envelope for message content columns, using the existing Vault
// (AES-256-GCM over localKey, no new crypto). TEXT columns carry
// "ayu1:" + base64(sealed) so the schema is untouched; BLOB columns
// carry the sealed bytes directly. Absent markers mean legacy plaintext,
// which keeps working. A present-but-unopenable marker means corruption
// or a key mismatch and decodes to empty (logged), never to raw bytes.
constexpr auto kSealedTextPrefix = "ayu1:";
constexpr auto kSealedTextWritePrefix = "ayu2:";

[[nodiscard]] inline std::string RowContext(
		const char *table,
		ID userId,
		ID dialogId,
		int messageId,
		const char *field) {
	auto result = std::string("ayu-row-v1:");
	result += table;
	result += ':';
	result += std::to_string(userId);
	result += ':';
	result += std::to_string(dialogId);
	result += ':';
	result += std::to_string(messageId);
	result += ':';
	result += field;
	return result;
}

[[nodiscard]] inline std::string RowContext(
		const char *table,
		const AyuMessageBase &message,
		const char *field) {
	return RowContext(
		table,
		message.userId,
		message.dialogId,
		message.messageId,
		field);
}

[[nodiscard]] inline bool StripTextEnvelope(
		const std::string &stored,
		AyuSecret::Bytes &sealed) {
	for (const auto *prefix : { kSealedTextPrefix, kSealedTextWritePrefix }) {
		const auto length = std::strlen(prefix);
		if (stored.rfind(prefix, 0) == 0) {
			const auto raw = QByteArray::fromBase64(
				QByteArray::fromStdString(stored.substr(length)));
			sealed = AyuSecret::Bytes(raw.begin(), raw.end());
			return true;
		}
	}
	return false;
}

[[nodiscard]] inline bool LooksSealedText(const std::string &field) {
	AyuSecret::Bytes sealed;
	return StripTextEnvelope(field, sealed)
		&& AyuSecret::Vault::IsSealed(sealed);
}

[[nodiscard]] inline std::optional<std::string> SealText(
		const std::string &plain,
		const std::string &context,
		bool strict) {
	if (plain.empty()) {
		return plain;
	}
	AyuSecret::Bytes stripped;
	if (StripTextEnvelope(plain, stripped)
		&& AyuSecret::Vault::IsSealed(stripped)) {
		// Envelope-looking content: authenticate before trusting it.
		// A user literal that merely collides (no magic) falls through
		// and is sealed fresh, so it keeps round-tripping.
		if (!AyuSecret::Vault::Available()) {
			// Cannot authenticate without the key: refuse in strict
			// mode, preserve bytes otherwise.
			if (strict) {
				LOG(("[freshGram] Database: refusing unverified envelope."));
				return std::nullopt;
			}
			return plain;
		}
		auto probe = AyuSecret::Bytes();
		if (AyuSecret::Vault::Open(stripped, probe, context)) {
			return plain;
		}
		// Foreign or corrupt envelope: seal it fresh so the bytes stay
		// preserved and readable instead of decoding to empty.
	}
	if (!AyuSecret::Vault::Available()) {
		if (strict) {
			LOG(("[freshGram] Database: refusing plaintext text field."));
			return std::nullopt;
		}
		return plain;
	}
	const auto sealed = AyuSecret::Vault::Seal(
		AyuSecret::Bytes(plain.begin(), plain.end()),
		context);
	if (sealed.empty()) {
		if (strict) {
			LOG(("[freshGram] Database: refusing plaintext text field."));
			return std::nullopt;
		}
		return plain;
	}
	const auto base64 = QByteArray(
		reinterpret_cast<const char*>(sealed.data()),
		int(sealed.size())).toBase64().toStdString();
	return kSealedTextWritePrefix + base64;
}

[[nodiscard]] inline std::string OpenText(
		const std::string &stored,
		const std::string &context) {
	AyuSecret::Bytes sealed;
	if (!StripTextEnvelope(stored, sealed)
		|| !AyuSecret::Vault::IsSealed(sealed)) {
		// Plain legacy text, including prefix collisions: return as-is.
		return stored;
	}
	auto plain = AyuSecret::Bytes();
	if (!AyuSecret::Vault::Open(sealed, plain, context)) {
		LOG(("[freshGram] Database: cannot open sealed text field (%1)."
			).arg(QString::fromStdString(context)));
		return std::string();
	}
	return std::string(plain.begin(), plain.end());
}

[[nodiscard]] inline std::optional<std::vector<char>> SealBlob(
		const std::vector<char> &plain,
		const std::string &context,
		bool strict) {
	if (plain.empty()) {
		return plain;
	}
	const auto asBytes = AyuSecret::Bytes(plain.begin(), plain.end());
	if (AyuSecret::Vault::IsSealed(asBytes)) {
		// Same verify-before-trust as SealText: authenticate when the
		// key is available, refuse unverified in strict mode.
		if (!AyuSecret::Vault::Available()) {
			if (strict) {
				LOG(("[freshGram] Database: refusing unverified envelope."));
				return std::nullopt;
			}
			return plain;
		}
		auto probe = AyuSecret::Bytes();
		if (AyuSecret::Vault::Open(asBytes, probe, context)) {
			return plain;
		}
		// Foreign or corrupt envelope: seal it fresh below so the
		// bytes stay preserved and readable.
	}
	if (!AyuSecret::Vault::Available()) {
		if (strict) {
			LOG(("[freshGram] Database: refusing plaintext blob field."));
			return std::nullopt;
		}
		return plain;
	}
	const auto sealed = AyuSecret::Vault::Seal(asBytes, context);
	if (sealed.empty()) {
		if (strict) {
			LOG(("[freshGram] Database: refusing plaintext blob field."));
			return std::nullopt;
		}
		return plain;
	}
	return std::vector<char>(sealed.begin(), sealed.end());
}

[[nodiscard]] inline std::vector<char> OpenBlob(
		const std::vector<char> &stored,
		const std::string &context) {
	const auto sealed = AyuSecret::Bytes(stored.begin(), stored.end());
	if (!AyuSecret::Vault::IsSealed(sealed)) {
		return stored;
	}
	auto plain = AyuSecret::Bytes();
	if (!AyuSecret::Vault::Open(sealed, plain, context)) {
		LOG(("[freshGram] Database: cannot open sealed blob field (%1)."
			).arg(QString::fromStdString(context)));
		return std::vector<char>();
	}
	return std::vector<char>(plain.begin(), plain.end());
}

template<typename Message>
[[nodiscard]] std::optional<Message> SealRowCopy(
		const char *table,
		const Message &message,
		bool strict) {
	auto result = message;
	const auto context = [&](const char *field) {
		return RowContext(table, message, field);
	};
	const auto sealText = [&](std::string &field, const char *name) {
		auto sealed = SealText(field, context(name), strict);
		if (!sealed) {
			return false;
		}
		field = std::move(*sealed);
		return true;
	};
	const auto sealBlob = [&](std::vector<char> &field, const char *name) {
		auto sealed = SealBlob(field, context(name), strict);
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

// Two-argument forms bind the table name, so generic code (backfill)
// works uniformly across message and metadata rows.
[[nodiscard]] inline std::optional<DeletedMessage> SealRowCopy(
		const DeletedMessage &message,
		bool strict) {
	return SealRowCopy("DeletedMessage", message, strict);
}

[[nodiscard]] inline std::optional<EditedMessage> SealRowCopy(
		const EditedMessage &message,
		bool strict) {
	return SealRowCopy("EditedMessage", message, strict);
}

inline void OpenRow(const char *table, AyuMessageBase &message) {
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

inline void OpenRow(DeletedMessage &message) {
	OpenRow("DeletedMessage", message);
}

inline void OpenRow(EditedMessage &message) {
	OpenRow("EditedMessage", message);
}

template<typename Message>
[[nodiscard]] bool RowNeedsSeal(const Message &message) {
	// Cheap gate for the backfill scan: seal everything except content
	// that already carries a valid current-generation envelope. Legacy
	// plaintext (including prefix collisions) and previous-generation
	// envelopes are all reprocessed; SealRowCopy itself verifies.
	const auto textNeeds = [&](const std::string &field) {
		return !field.empty() && !LooksSealedText(field);
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

// Newer archive tables and directory metadata, same envelope. Contexts
// reuse the row ids each table has; missing ones stay zero.

[[nodiscard]] inline std::string MarkupContext(
		const DeletedMarkup &row,
		const char *field) {
	return RowContext(
		"DeletedMarkup",
		row.userId,
		row.dialogId,
		row.messageId,
		field);
}

[[nodiscard]] inline std::optional<DeletedMarkup> SealRowCopy(
		const DeletedMarkup &row,
		bool strict) {
	auto result = row;
	const auto sealed = SealBlob(result.markup, MarkupContext(row, "markup"), strict);
	if (!sealed) {
		return std::nullopt;
	}
	result.markup = std::move(*sealed);
	return result;
}

inline void OpenRow(DeletedMarkup &row) {
	row.markup = OpenBlob(row.markup, MarkupContext(row, "markup"));
}

[[nodiscard]] inline bool RowNeedsSeal(const DeletedMarkup &row) {
	return !row.markup.empty()
		&& !AyuSecret::Vault::IsSealed(
			AyuSecret::Bytes(row.markup.begin(), row.markup.end()));
}

[[nodiscard]] inline std::string ExtraContext(
		const DeletedExtra &row,
		const char *field) {
	return RowContext(
		"DeletedExtra",
		row.userId,
		row.dialogId,
		row.messageId,
		field);
}

[[nodiscard]] inline std::optional<DeletedExtra> SealRowCopy(
		const DeletedExtra &row,
		bool strict) {
	auto result = row;
	const auto reactions = SealBlob(
		result.reactions,
		ExtraContext(row, "reactions"),
		strict);
	const auto repliers = SealText(
		result.repliers,
		ExtraContext(row, "repliers"),
		strict);
	if (!reactions || !repliers) {
		return std::nullopt;
	}
	result.reactions = std::move(*reactions);
	result.repliers = std::move(*repliers);
	return result;
}

inline void OpenRow(DeletedExtra &row) {
	row.reactions = OpenBlob(row.reactions, ExtraContext(row, "reactions"));
	row.repliers = OpenText(row.repliers, ExtraContext(row, "repliers"));
}

[[nodiscard]] inline bool RowNeedsSeal(const DeletedExtra &row) {
	return (!row.reactions.empty()
			&& !AyuSecret::Vault::IsSealed(AyuSecret::Bytes(
				row.reactions.begin(),
				row.reactions.end())))
		|| (!row.repliers.empty() && !LooksSealedText(row.repliers));
}

[[nodiscard]] inline std::string KeptDialogContext(
		const KeptDialog &row,
		const char *field) {
	return RowContext("KeptDialog", row.userId, row.dialogId, 0, field);
}

[[nodiscard]] inline std::optional<KeptDialog> SealRowCopy(
		const KeptDialog &row,
		bool strict) {
	auto result = row;
	const auto title = SealText(
		result.title,
		KeptDialogContext(row, "title"),
		strict);
	const auto username = SealText(
		result.username,
		KeptDialogContext(row, "username"),
		strict);
	if (!title || !username) {
		return std::nullopt;
	}
	result.title = std::move(*title);
	result.username = std::move(*username);
	return result;
}

inline void OpenRow(KeptDialog &row) {
	row.title = OpenText(row.title, KeptDialogContext(row, "title"));
	row.username = OpenText(row.username, KeptDialogContext(row, "username"));
}

[[nodiscard]] inline bool RowNeedsSeal(const KeptDialog &row) {
	return (!row.title.empty() && !LooksSealedText(row.title))
		|| (!row.username.empty() && !LooksSealedText(row.username));
}

[[nodiscard]] inline std::string KeptTopicContext(
		const KeptTopic &row,
		const char *field) {
	return RowContext(
		"KeptTopic",
		row.userId,
		row.dialogId,
		int(row.rootId),
		field);
}

[[nodiscard]] inline std::optional<KeptTopic> SealRowCopy(
		const KeptTopic &row,
		bool strict) {
	auto result = row;
	const auto title = SealText(
		result.title,
		KeptTopicContext(row, "title"),
		strict);
	if (!title) {
		return std::nullopt;
	}
	result.title = std::move(*title);
	return result;
}

inline void OpenRow(KeptTopic &row) {
	row.title = OpenText(row.title, KeptTopicContext(row, "title"));
}

[[nodiscard]] inline bool RowNeedsSeal(const KeptTopic &row) {
	return !row.title.empty() && !LooksSealedText(row.title);
}

[[nodiscard]] inline std::string KnownUserContext(
		const KnownUser &row,
		const char *field) {
	return RowContext(
		"KnownUser",
		row.userId,
		ID(row.peerId),
		0,
		field);
}

[[nodiscard]] inline std::optional<KnownUser> SealRowCopy(
		const KnownUser &row,
		bool strict) {
	auto result = row;
	const auto first = SealText(
		result.firstName,
		KnownUserContext(row, "firstName"),
		strict);
	const auto last = SealText(
		result.lastName,
		KnownUserContext(row, "lastName"),
		strict);
	const auto username = SealText(
		result.username,
		KnownUserContext(row, "username"),
		strict);
	if (!first || !last || !username) {
		return std::nullopt;
	}
	result.firstName = std::move(*first);
	result.lastName = std::move(*last);
	result.username = std::move(*username);
	return result;
}

inline void OpenRow(KnownUser &row) {
	row.firstName = OpenText(row.firstName, KnownUserContext(row, "firstName"));
	row.lastName = OpenText(row.lastName, KnownUserContext(row, "lastName"));
	row.username = OpenText(row.username, KnownUserContext(row, "username"));
}

[[nodiscard]] inline bool RowNeedsSeal(const KnownUser &row) {
	return (!row.firstName.empty() && !LooksSealedText(row.firstName))
		|| (!row.lastName.empty() && !LooksSealedText(row.lastName))
		|| (!row.username.empty() && !LooksSealedText(row.username));
}

[[nodiscard]] inline std::string RegexFilterContext(
		const RegexFilter &row,
		const char *field) {
	auto result = std::string("ayu-regex-filter-v2:");
	result += row.dialogId ? "dialog:" : "shared:";
	result += std::to_string(row.dialogId.value_or(0));
	result += ':';
	result += std::to_string(row.id.size());
	result += ':';
	result.append(row.id.begin(), row.id.end());
	result += ':';
	result += field;
	return result;
}

[[nodiscard]] inline std::string LegacyRegexFilterContext(
		const RegexFilter &row,
		const char *field) {
	return RowContext(
		"RegexFilter",
		0,
		row.dialogId.value_or(0),
		0,
		field);
}

[[nodiscard]] inline bool RegexFilterNeedsSeal(const RegexFilter &row) {
	if (row.text.empty()) {
		return false;
	}
	auto sealed = AyuSecret::Bytes();
	if (!StripTextEnvelope(row.text, sealed)
		|| !AyuSecret::Vault::IsSealed(sealed)) {
		return true;
	}
	if (!AyuSecret::Vault::IsBound(sealed)) {
		return true;
	}
	if (!AyuSecret::Vault::Available()) {
		return false;
	}
	auto plain = AyuSecret::Bytes();
	return !AyuSecret::Vault::Open(
		sealed,
		plain,
		RegexFilterContext(row, "text"))
		&& AyuSecret::Vault::Open(
			sealed,
			plain,
			LegacyRegexFilterContext(row, "text"));
}

[[nodiscard]] inline std::optional<RegexFilter> SealRowCopy(
		const RegexFilter &row,
		bool strict) {
	auto result = row;
	const auto text = SealText(
		result.text,
		RegexFilterContext(row, "text"),
		strict);
	if (!text) {
		return std::nullopt;
	}
	result.text = std::move(*text);
	return result;
}

inline void OpenRow(RegexFilter &row) {
	const auto current = OpenText(row.text, RegexFilterContext(row, "text"));
	if (current.empty() && !row.text.empty() && LooksSealedText(row.text)) {
		row.text = OpenText(row.text, LegacyRegexFilterContext(row, "text"));
	} else {
		row.text = current;
	}
}

[[nodiscard]] inline bool RowNeedsSeal(const RegexFilter &row) {
	return RegexFilterNeedsSeal(row);
}

} // namespace AyuDatabase::Encryption
