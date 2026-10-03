#include "ayu/data/deleted_restore.h"

#include "api/api_text_entities.h"
#include "ayu/ayu_settings.h"
#include "ayu/data/messages_storage.h"
#include "ayu/utils/ayu_mapper.h"
#include "ayu/utils/telegram_helpers.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"

namespace AyuRestore {
namespace {

constexpr auto kLoadLimit = 800;
constexpr auto kOutgoingFlag = 0x00000002;

struct DialogsEntry {
	bool loading = false;
	bool loaded = false;
	base::flat_set<ID> ids;
	std::vector<Fn<void(const base::flat_set<ID> &)>> waiting;
};

base::flat_map<ID, DialogsEntry> DialogsWithDeleted;

[[nodiscard]] bool Supported(not_null<PeerData*> peer) {
	return !peer->isForum() && !peer->isMonoforum();
}

void WithDeletedDialogs(
		ID userId,
		Fn<void(const base::flat_set<ID> &)> callback) {
	auto &entry = DialogsWithDeleted[userId];
	if (entry.loaded) {
		callback(entry.ids);
		return;
	}
	entry.waiting.push_back(std::move(callback));
	if (entry.loading) {
		return;
	}
	entry.loading = true;
	crl::async([=] {
		auto ids = std::vector<ID>();
		try {
			ids = AyuMessages::loadDeletedDialogIds(userId);
		} catch (...) {
			ids.clear();
		}
		crl::on_main([=, ids = std::move(ids)]() mutable {
			auto &entry = DialogsWithDeleted[userId];
			entry.loaded = true;
			entry.loading = false;
			entry.ids = base::flat_set<ID>(ids.begin(), ids.end());
			auto waiting = std::move(entry.waiting);
			entry.waiting.clear();
			for (const auto &callback : waiting) {
				callback(entry.ids);
			}
		});
	});
}

} // namespace

State::State(not_null<History*> history)
: _history(history) {
}

void State::checkLoaded() {
	if (_requested) {
		return;
	}
	_requested = true;

	const auto peer = _history->peer;
	if (!AyuSettings::getInstance().saveDeletedMessages()
		|| !Supported(peer)) {
		return;
	}

	const auto userId = AyuMessages::storageUserId(peer);
	const auto dialogId = getDialogIdFromPeer(peer);
	const auto weak = base::make_weak(this);
	WithDeletedDialogs(userId, [=](const base::flat_set<ID> &ids) {
		if (const auto strong = weak.get(); strong && ids.contains(dialogId)) {
			strong->load(userId, dialogId);
		}
	});
}

void State::disable() {
	_disabled = true;
	_requested = true;
	_loaded = false;
	_rows.clear();
}

void State::load(ID userId, ID dialogId) {
	const auto weak = base::make_weak(this);
	crl::async([=] {
		auto messages = std::vector<AyuMessageBase>();
		try {
			messages = AyuMessages::loadDeletedMessages(
				userId,
				dialogId,
				0,
				0,
				0,
				kLoadLimit,
				std::string());
		} catch (...) {
			messages.clear();
		}

		crl::on_main([=, messages = std::move(messages)]() mutable {
			const auto strong = weak.get();
			if (!strong || strong->_disabled) {
				return;
			}
			strong->_rows.reserve(messages.size());
			for (auto &message : messages) {
				strong->_rows.push_back({ std::move(message), MsgId() });
			}
			strong->_loaded = true;
			strong->_history->checkLocalMessages();
		});
	});
}

HistoryItem *State::materialize(TimeId from, TimeId till) {
	if (!_loaded || _materializing || _rows.empty()) {
		return nullptr;
	}
	_materializing = true;
	auto last = (HistoryItem*)nullptr;
	for (auto &row : _rows) {
		const auto date = row.message.date;
		if (date >= from && date < till) {
			try {
				if (const auto item = create(row)) {
					last = item;
				}
			} catch (...) {
				LOG(("AyuRestore: failed to restore a saved message"));
			}
		}
	}
	_materializing = false;
	return last;
}

HistoryItem *State::create(Row &row) {
	const auto peer = _history->peer;
	auto &owner = _history->owner();
	if (row.localId) {
		if (owner.message(peer, row.localId)) {
			return nullptr;
		}
		row.localId = MsgId();
	}
	const auto &message = row.message;
	if (owner.message(peer, MsgId(message.messageId))) {
		return nullptr;
	}

	PeerData *from = owner.userLoaded(message.fromId);
	if (!from) {
		from = owner.channelLoaded(message.fromId);
	}
	if (!from) {
		from = owner.chatLoaded(message.fromId);
	}

	auto flags = MessageFlags(MessageFlag::Local);
	const auto outgoing = (message.flags & kOutgoingFlag) != 0;
	if (outgoing) {
		flags |= MessageFlag::Outgoing;
	}
	if (peer->isChannel() && !peer->isMegagroup()) {
		flags |= MessageFlag::Post;
		from = nullptr;
	} else {
		if (!from && peer->isUser()) {
			from = outgoing ? peer->session().user().get() : peer.get();
		}
		if (from) {
			flags |= MessageFlag::HasFromId;
		}
	}
	if (!message.postAuthor.empty()) {
		flags |= MessageFlag::HasPostAuthor;
	}

	auto text = Ui::Text::WithEntities(QString::fromStdString(message.text));
	text.entities = Api::EntitiesFromMTP(
		&_history->session(),
		AyuMapper::deserializeTextWithEntities(message.textEntities).v);

	const auto item = _history->makeMessage({
		.id = owner.nextLocalMessageId(),
		.flags = flags,
		.from = from ? from->id : PeerId(),
		.date = message.date,
		.postAuthor = QString::fromStdString(message.postAuthor),
	}, std::move(text), AyuMapper::deserializeMedia(message.documentSerialized));

	AyuMessages::restoreSavedMedia(item, message);
	item->setDeleted();
	item->markDeletedAnimated();
	row.localId = item->id;
	return item;
}

} // namespace AyuRestore
