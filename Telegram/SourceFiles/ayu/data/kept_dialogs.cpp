#include "ayu/data/kept_dialogs.h"

#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_database.h"
#include "ayu/data/messages_storage.h"
#include "ayu/utils/telegram_helpers.h"
#include "base/flat_set.h"
#include "crl/crl_async.h"
#include "crl/crl_on_main.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_folder.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"

#include <set>

namespace AyuKept {
namespace {

constexpr auto kSnapshotLimit = 100;

enum class Kind : int {
	User = 0,
	Chat = 1,
	Broadcast = 2,
	Megagroup = 3,
};

struct SessionState {
	bool requested = false;
	bool loaded = false;
	std::vector<KeptDialog> rows;
	base::flat_set<ID> done;
};

std::set<std::pair<ID, ID>> Noted;
std::set<PeerData*> Leaving;

[[nodiscard]] bool Supported(not_null<PeerData*> peer) {
	if (peer->isForum() || peer->isMonoforum() || peer->migrateTo()) {
		return false;
	}
	if (const auto channel = peer->asChannel()) {
		return !channel->isCommunity();
	}
	return peer->isUser() || peer->isChat();
}

[[nodiscard]] int KindOf(not_null<PeerData*> peer) {
	if (peer->isUser()) {
		return int(Kind::User);
	} else if (peer->isChat()) {
		return int(Kind::Chat);
	} else if (const auto channel = peer->asChannel()) {
		return int(channel->isMegagroup()
			? Kind::Megagroup
			: Kind::Broadcast);
	}
	return -1;
}

[[nodiscard]] KeptDialog MakeRow(not_null<History*> history, bool lost) {
	const auto peer = history->peer;
	auto row = KeptDialog();
	row.fakeId = 0;
	row.userId = AyuMessages::storageUserId(peer);
	row.dialogId = getDialogIdFromPeer(peer);
	row.kind = KindOf(peer);
	row.title = peer->name().toStdString();
	row.username = peer->username().toStdString();
	row.accessHash = 0;
	if (const auto user = peer->asUser()) {
		row.accessHash = ID(user->accessHash());
	} else if (const auto channel = peer->asChannel()) {
		row.accessHash = ID(channel->accessHash());
	}
	row.folderId = history->folder() ? history->folder()->id() : 0;
	row.lastMessageDate = history->chatListTimeId();
	row.lost = lost ? 1 : 0;
	return row;
}

void SaveSnapshot(not_null<History*> history) {
	auto saved = 0;
	for (const auto &block : ranges::views::reverse(history->blocks)) {
		for (const auto &view : ranges::views::reverse(block->messages)) {
			if (saved >= kSnapshotLimit) {
				return;
			}
			const auto item = view->data();
			if (item->isDeleted() || item->isService()) {
				continue;
			}
			AyuMessages::addDeletedMessage(item);
			++saved;
		}
	}
}

[[nodiscard]] PeerId PeerIdOf(const KeptDialog &row) {
	const auto bare = uint64(row.dialogId < 0 ? -row.dialogId : row.dialogId);
	if (!bare) {
		return PeerId();
	}
	switch (Kind(row.kind)) {
	case Kind::User: return peerFromUser(UserId(bare));
	case Kind::Chat: return peerFromChat(ChatId(bare));
	case Kind::Broadcast:
	case Kind::Megagroup: return peerFromChannel(ChannelId(bare));
	}
	return PeerId();
}

PeerData *EnsurePeer(not_null<Data::Session*> owner, const KeptDialog &row) {
	const auto peerId = PeerIdOf(row);
	if (!peerId) {
		return nullptr;
	}
	const auto title = QString::fromStdString(row.title);
	switch (Kind(row.kind)) {
	case Kind::User: {
		const auto user = owner->peer(peerId)->asUser();
		if (user && !user->isLoaded()) {
			user->setAccessHash(uint64(row.accessHash));
			user->setName(
				title,
				QString(),
				QString(),
				QString::fromStdString(row.username));
			user->setLoadedStatus(PeerData::LoadedStatus::Normal);
		}
		return user;
	}
	case Kind::Chat: {
		if (!owner->chatLoaded(peerToChat(peerId))) {
			owner->processChat(MTP_chatForbidden(
				MTP_long(peerToChat(peerId).bare),
				MTP_string(title)));
		}
		return owner->chatLoaded(peerToChat(peerId));
	}
	case Kind::Broadcast:
	case Kind::Megagroup: {
		if (!owner->channelLoaded(peerToChannel(peerId))) {
			const auto megagroup = (Kind(row.kind) == Kind::Megagroup);
			owner->processChat(MTP_channelForbidden(
				MTP_flags(megagroup
					? MTPDchannelForbidden::Flag::f_megagroup
					: MTPDchannelForbidden::Flag::f_broadcast),
				MTP_long(peerToChannel(peerId).bare),
				MTP_long(row.accessHash),
				MTP_string(title),
				MTPint()));
		}
		return owner->channelLoaded(peerToChannel(peerId));
	}
	}
	return nullptr;
}

void Restore(not_null<Main::Session*> session, const KeptDialog &row) {
	auto &owner = session->data();
	const auto peerId = PeerIdOf(row);
	if (!peerId) {
		return;
	}
	if (const auto existing = owner.historyLoaded(peerId)) {
		if (existing->ayuKept()) {
			return;
		} else if (existing->inChatList()) {
			if (row.lost) {
				AyuDatabase::removeKeptDialog(row.userId, row.dialogId);
			}
			return;
		}
	}
	const auto peer = EnsurePeer(&owner, row);
	if (!peer || !Supported(peer)) {
		return;
	}
	if (const auto channel = peer->asChannel(); channel && channel->amIn()) {
		return;
	} else if (const auto chat = peer->asChat(); chat && chat->amIn()) {
		return;
	}
	const auto history = owner.history(peer);
	history->setAyuKept(true);
	if (!history->folderKnown()) {
		history->clearFolder();
	}
	history->restoreAyuKept();
}

[[nodiscard]] bool Ready(
		not_null<Data::Session*> owner,
		const KeptDialog &row) {
	if (row.lost) {
		return true;
	}
	if (row.folderId == Data::Folder::kId) {
		const auto folder = owner->folderLoaded(Data::Folder::kId);
		return folder && owner->chatsListLoaded(folder);
	}
	return true;
}

void Process(
		not_null<Main::Session*> session,
		const std::shared_ptr<SessionState> &state) {
	auto &owner = session->data();
	if (!state->loaded || !owner.chatsListLoaded()) {
		return;
	}
	for (const auto &row : state->rows) {
		if (state->done.contains(row.dialogId) || !Ready(&owner, row)) {
			continue;
		}
		state->done.emplace(row.dialogId);
		Restore(session, row);
	}
}

void Check(
		not_null<Main::Session*> session,
		const std::shared_ptr<SessionState> &state) {
	if (!session->data().chatsListLoaded()) {
		return;
	}
	if (state->loaded) {
		Process(session, state);
		return;
	}
	if (state->requested) {
		return;
	}
	state->requested = true;
	const auto userId = ID(session->userId().bare & PeerId::kChatTypeMask);
	crl::async([=] {
		auto rows = AyuDatabase::getKeptDialogs(userId);
		crl::on_main(session, [=, rows = std::move(rows)]() mutable {
			state->rows = std::move(rows);
			state->loaded = true;
			Process(session, state);
		});
	});
}

} // namespace

void setup(not_null<Data::Session*> owner, rpl::lifetime &lifetime) {
	const auto state = std::make_shared<SessionState>();
	const auto session = &owner->session();
	owner->chatsListLoadedEvents(
	) | rpl::on_next([=](Data::Folder*) {
		Check(session, state);
	}, lifetime);
}

void markLost(not_null<History*> history) {
	const auto peer = history->peer;
	if (!AyuSettings::getInstance().saveDeletedMessages()
		|| !Supported(peer)
		|| history->ayuKept()
		|| !history->lastMessage()) {
		return;
	}
	history->setAyuKept(true);
	SaveSnapshot(history);
	AyuDatabase::saveKeptDialog(MakeRow(history, true));
}

bool keepOnDelete(not_null<History*> history) {
	if (history->ayuKept() || !history->inChatList()) {
		return false;
	}
	markLost(history);
	return history->ayuKept();
}

void note(not_null<History*> history) {
	const auto peer = history->peer;
	if (!Supported(peer) || history->ayuKept()) {
		return;
	}
	const auto key = std::make_pair(
		AyuMessages::storageUserId(peer),
		getDialogIdFromPeer(peer));
	if (!Noted.emplace(key).second) {
		return;
	}
	AyuDatabase::saveKeptDialog(MakeRow(history, false));
}

void forget(not_null<PeerData*> peer) {
	if (const auto history = peer->owner().historyLoaded(peer)) {
		history->setAyuKept(false);
	}
	const auto userId = AyuMessages::storageUserId(peer);
	const auto dialogId = getDialogIdFromPeer(peer);
	Noted.erase(std::make_pair(userId, dialogId));
	AyuDatabase::removeKeptDialog(userId, dialogId);
}

void userLeaving(not_null<PeerData*> peer) {
	Leaving.emplace(peer.get());
}

bool takeUserLeaving(not_null<PeerData*> peer) {
	return Leaving.erase(peer.get()) > 0;
}

void chatAmInChanged(not_null<ChatData*> chat) {
	const auto history = chat->owner().historyLoaded(chat);
	if (chat->amIn()) {
		takeUserLeaving(chat);
		if (history && history->ayuKept()) {
			forget(chat);
		}
		return;
	}
	const auto leaving = takeUserLeaving(chat);
	if (history && history->inChatList() && !leaving) {
		markLost(history);
	}
}

} // namespace AyuKept
