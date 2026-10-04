#include "ayu/data/sent_messages.h"

#include "ayu/data/ayu_database.h"
#include "ayu/data/messages_storage.h"
#include "ayu/utils/telegram_helpers.h"
#include "crl/crl_on_main.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace AyuSent {
namespace {

std::vector<SentMessage> Pending;
bool FlushScheduled = false;

void Flush() {
	FlushScheduled = false;
	if (Pending.empty()) {
		return;
	}
	auto batch = std::move(Pending);
	Pending.clear();
	AyuDatabase::addSentMessages(batch);
}

[[nodiscard]] bool Suitable(not_null<HistoryItem*> item) {
	return item->out()
		&& item->isRegular()
		&& !item->isService()
		&& !item->isScheduled()
		&& !item->isBusinessShortcut()
		&& !item->history()->peer->isNotificationsUser();
}

[[nodiscard]] std::string TextOf(not_null<HistoryItem*> item) {
	auto text = item->originalText().text;
	if (text.isEmpty()) {
		text = item->notificationText().text;
	}
	return text.toStdString();
}

} // namespace

void note(not_null<HistoryItem*> item) {
	if (!Suitable(item)) {
		return;
	}
	auto message = SentMessage();
	const auto peer = item->history()->peer;
	message.fakeId = 0;
	message.userId = AyuMessages::storageUserId(peer);
	message.dialogId = getDialogIdFromPeer(peer);
	message.messageId = item->id.bare;
	message.date = item->date();
	message.title = peer->name().toStdString();
	message.text = TextOf(item);
	if (message.text.empty()) {
		return;
	}
	Pending.push_back(std::move(message));
	if (!FlushScheduled) {
		FlushScheduled = true;
		crl::on_main(Flush);
	}
}

void edited(not_null<HistoryItem*> item) {
	if (!Suitable(item)) {
		return;
	}
	const auto text = TextOf(item);
	if (text.empty()) {
		return;
	}
	Flush();
	const auto peer = item->history()->peer;
	AyuDatabase::updateSentMessageText(
		AyuMessages::storageUserId(peer),
		getDialogIdFromPeer(peer),
		item->id.bare,
		text);
}

std::vector<SentMessage> load(
		not_null<Main::Session*> session,
		ID dialogId,
		const std::string &query,
		int offset,
		int limit) {
	Flush();
	const auto userId = ID(session->userId().bare & PeerId::kChatTypeMask);
	return AyuDatabase::getSentMessages(userId, dialogId, query, offset, limit);
}

PeerData *resolvePeer(not_null<Main::Session*> session, ID dialogId) {
	auto &owner = session->data();
	if (dialogId > 0) {
		return owner.peerLoaded(peerFromUser(UserId(dialogId)));
	}
	const auto bare = uint64(-dialogId);
	if (const auto channel = owner.channelLoaded(ChannelId(bare))) {
		return channel;
	}
	return owner.chatLoaded(ChatId(bare));
}

} // namespace AyuSent
