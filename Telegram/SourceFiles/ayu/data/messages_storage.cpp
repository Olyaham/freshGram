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
#include "base/timer.h"
#include "base/unixtime.h"
#include "crl/crl_on_main.h"
#include "data/data_cloud_file.h"
#include "data/data_document.h"
#include "data/data_forum_topic.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "storage/cache/storage_cache_database.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace AyuMessages {

namespace {

constexpr auto kMaxCachedDocumentSize = int64(32) * 1024 * 1024;

constexpr auto kPhotoSaveAttempts = 60;

struct PhotoSaveTask {
	std::shared_ptr<Data::PhotoMedia> media;
	QString path;
	base::Timer timer;
	int attempts = 0;
};

std::vector<DeletedMessage> PendingDeleted;
bool FlushScheduled = false;
std::vector<std::unique_ptr<PhotoSaveTask>> PhotoSaveTasks;

[[nodiscard]] QString SavedMediaPath(
		ID userId,
		ID dialogId,
		int messageId) {
	return QString("./tdata/ayu_media/%1_%2_%3.bin")
		.arg(userId)
		.arg(dialogId)
		.arg(messageId);
}

[[nodiscard]] QString SavedMediaPath(not_null<HistoryItem*> item) {
	return SavedMediaPath(
		storageUserId(item->history()->peer),
		getDialogIdFromPeer(item->history()->peer),
		item->id.bare);
}

bool WritePhotoBytes(
		const std::shared_ptr<Data::PhotoMedia> &media,
		const QString &path) {
	if (!media->loaded()) {
		return false;
	}
	const auto bytes = media->imageBytes(Data::PhotoSize::Large);
	if (!bytes.isEmpty()) {
		QDir().mkpath(QFileInfo(path).absolutePath());
		auto file = QFile(path);
		if (file.open(QIODevice::WriteOnly)) {
			file.write(bytes);
		}
	}
	return true;
}

void SavePhotoBytes(
		not_null<PhotoData*> photo,
		FullMsgId origin,
		const QString &path) {
	if (QFile::exists(path)) {
		return;
	}
	auto media = photo->createMediaView();
	media->wanted(Data::PhotoSize::Large, origin);
	if (WritePhotoBytes(media, path)) {
		return;
	}
	auto task = std::make_unique<PhotoSaveTask>();
	const auto raw = task.get();
	raw->media = std::move(media);
	raw->path = path;
	raw->timer.setCallback([=] {
		++raw->attempts;
		if (WritePhotoBytes(raw->media, raw->path)
			|| raw->attempts >= kPhotoSaveAttempts) {
			raw->timer.cancel();
			crl::on_main([=] {
				PhotoSaveTasks.erase(
					std::remove_if(
						PhotoSaveTasks.begin(),
						PhotoSaveTasks.end(),
						[=](const auto &task) { return task.get() == raw; }),
					PhotoSaveTasks.end());
			});
		}
	});
	raw->timer.callEach(1000);
	PhotoSaveTasks.push_back(std::move(task));
}

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
	const auto media = item->media();
	const auto mediaFile = SavedMediaPath(item);
	message.mediaPath = ((media && media->photo()) || QFile::exists(mediaFile))
		? mediaFile.toStdString()
		: "/";
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
		SavePhotoBytes(photo, origin, SavedMediaPath(item));
	}
	if (const auto document = media->document()) {
		document->loadThumbnail(origin);
		if (document->size > 0 && document->size <= kMaxCachedDocumentSize) {
			document->save(origin, QString(), LoadFromCloudOrLocal, true);
		}
	}
}

void restoreSavedMedia(
		not_null<HistoryItem*> item,
		const AyuMessageBase &message) {
	const auto media = item->media();
	const auto photo = media ? media->photo() : nullptr;
	if (!photo || message.mediaPath.empty() || message.mediaPath == "/") {
		return;
	}
	auto file = QFile(QString::fromStdString(message.mediaPath));
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto bytes = file.readAll();
	const auto cacheKey = photo->location(
		Data::PhotoSize::Large).file().cacheKey();
	if (bytes.isEmpty() || !cacheKey) {
		return;
	}
	photo->owner().cache().putIfEmpty(
		cacheKey,
		Storage::Cache::Database::TaggedValue(
			QByteArray(bytes),
			Data::kImageCacheTag));
}

std::vector<ID> loadDeletedDialogIds(ID userId) {
	return AyuDatabase::getDeletedDialogIds(userId);
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
