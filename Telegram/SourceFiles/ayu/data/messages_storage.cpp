// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/messages_storage.h"

#include "ayu/data/ayu_database.h"
#include "ayu/utils/ayu_mapper.h"
#include "ayu/utils/telegram_helpers.h"
#include "base/unixtime.h"
#include "crl/crl_on_main.h"
#include "data/data_document.h"
#include "data/data_forum_topic.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"

namespace AyuMessages {

namespace {

constexpr auto kMaxCachedDocumentSize = int64(32) * 1024 * 1024;

std::vector<DeletedMessage> PendingDeleted;
bool FlushScheduled = false;

void flushPendingDeleted() {
	FlushScheduled = false;
	if (PendingDeleted.empty()) {
		return;
	}
	auto batch = std::move(PendingDeleted);
	PendingDeleted.clear();
	AyuDatabase::addDeletedMessages(batch);
}

}

template<typename DerivedMessage>
std::vector<AyuMessageBase> convertToBase(const std::vector<DerivedMessage> &messages) {
	std::vector<AyuMessageBase> based;
	based.reserve(messages.size());
	for (const auto &msg : messages) {
		based.push_back(static_cast<AyuMessageBase>(msg));
	}
	return based;
}

void map(not_null<HistoryItem*> item, AyuMessageBase &message) {
	const ID userId = item->history()->owner().session().userId().bare & PeerId::kChatTypeMask;

	message.userId = userId;
	message.dialogId = getDialogIdFromPeer(item->history()->peer);
	message.groupedId = item->groupId().raw();
	message.peerId = item->history()->peer->id.value & PeerId::kChatTypeMask;
	message.fromId = item->from()->id.value & PeerId::kChatTypeMask;
	if (item->topic()) {
		message.topicId = item->topicRootId().bare;
	} else {
		message.topicId = 0;
	}
	message.messageId = item->id.bare;
	message.date = item->date();
	message.flags = AyuMapper::mapItemFlagsToMTPFlags(item);

	if (const auto edited = item->Get<HistoryMessageEdited>()) {
		message.editDate = edited->date;
	} else {
		message.editDate = base::unixtime::now();
	}

	message.views = item->viewsCount();
	message.fwdFlags = 0;
	message.fwdFromId = 0;
	// message.fwdName
	message.fwdDate = 0;
	// message.fwdPostAuthor
	if (const auto msgsigned = item->Get<HistoryMessageSigned>()) {
		message.postAuthor = msgsigned->author.toStdString();
	}
	message.replyFlags = 0;
	message.replyMessageId = 0;
	message.replyPeerId = 0;
	message.replyTopId = 0;
	message.replyForumTopic = false;
	// message.replySerialized
	// message.replyMarkupSerialized
	message.entityCreateDate = base::unixtime::now();

	auto serializedText = AyuMapper::serializeTextWithEntities(item);
	message.text = serializedText.first;
	message.textEntities = serializedText.second;

	// todo: implement mapping
	message.documentSerialized = item->ayuSavedMedia();
	message.mediaPath = "/";
	// message.hqThumbPath
	message.documentType = message.documentSerialized.empty() ? 0 : 1;
	// message.documentSerialized
	// message.thumbsSerialized
	// message.documentAttributesSerialized
	// message.mimeType
}

void addEditedMessage(not_null<HistoryItem *> item) {
	EditedMessage message;
	map(item, message);

	if (message.text.empty()) {
		return;
	}

	AyuDatabase::addEditedMessage(message);
}

ID storageUserId(not_null<PeerData*> peer) {
	return peer->session().userId().bare & PeerId::kChatTypeMask;
}

std::vector<AyuMessageBase> loadEditedMessages(ID userId, ID dialogId, ID messageId, ID minId, ID maxId, int totalLimit) {
	return convertToBase(AyuDatabase::getEditedMessages(userId, dialogId, messageId, minId, maxId, totalLimit));
}

bool hasRevisions(not_null<HistoryItem*> item) {
	const ID userId = item->history()->owner().session().userId().bare & PeerId::kChatTypeMask;
	const auto dialogId = getDialogIdFromPeer(item->history()->peer);
	const auto msgId = item->id.bare;

	return AyuDatabase::hasRevisions(userId, dialogId, msgId);
}

void cacheDeletedMedia(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return;
	}
	const auto origin = item->fullId();
	if (const auto photo = media->photo()) {
		photo->load(origin, LoadFromCloudOrLocal, true);
	}
	if (const auto document = media->document()) {
		document->loadThumbnail(origin);
		if (document->size > 0 && document->size <= kMaxCachedDocumentSize) {
			document->save(origin, QString(), LoadFromCloudOrLocal, true);
		}
	}
}

void addDeletedMessage(not_null<HistoryItem*> item) {
	DeletedMessage message;
	map(item, message);

	if (message.text.empty() && message.documentSerialized.empty() && item->media()) {
		message.text = item->notificationText().text.toStdString();
	}
	if (message.text.empty() && message.documentSerialized.empty()) {
		return;
	}

	PendingDeleted.push_back(std::move(message));
	if (!FlushScheduled) {
		FlushScheduled = true;
		crl::on_main(flushPendingDeleted);
	}
}

std::vector<AyuMessageBase> loadDeletedMessages(
		ID userId,
		ID dialogId,
		ID topicId,
		ID minId,
		ID maxId,
		int totalLimit,
		const std::string &searchQuery) {
	return convertToBase(AyuDatabase::getDeletedMessages(userId, dialogId, topicId, minId, maxId, totalLimit, searchQuery));
}

bool hasDeletedMessages(not_null<PeerData*> peer, ID topicId) {
	flushPendingDeleted();
	return AyuDatabase::hasDeletedMessages(storageUserId(peer), getDialogIdFromPeer(peer), topicId);
}

void removeDeletedMessage(not_null<HistoryItem*> item) {
	flushPendingDeleted();
	const auto peer = item->history()->peer;
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	AyuDatabase::removeDeletedMessage(userId, getDialogIdFromPeer(peer), item->id.bare);
}

void clearDeletedMessages(not_null<PeerData*> peer, ID topicId) {
	flushPendingDeleted();
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	AyuDatabase::clearDeletedMessages(userId, getDialogIdFromPeer(peer), topicId);
}

}
