#include "ayu/secret/secret_manager.h"

#include "apiwrap.h"
#include "ayu/data/ayu_database.h"
#include "ayu/secret/secret_crypto.h"
#include "ayu/secret/secret_protocol.h"
#include "ayu/secret/secret_tl.h"
#include "base/openssl_help.h"
#include "base/unixtime.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "mtproto/mtproto_dh_utils.h"
#include "ui/toast/toast.h"

#include <map>

namespace AyuSecret {
namespace {

constexpr auto kAttachmentNote = "[attachment is not supported in freshGram]";

[[nodiscard]] Bytes FromArray(const QByteArray &data) {
	return Bytes(
		reinterpret_cast<const uint8_t*>(data.constData()),
		reinterpret_cast<const uint8_t*>(data.constData()) + data.size());
}

[[nodiscard]] QByteArray ToArray(const Bytes &data) {
	return QByteArray(
		reinterpret_cast<const char*>(data.data()),
		int(data.size()));
}

[[nodiscard]] Bytes FromChars(const std::vector<char> &data) {
	return Bytes(data.begin(), data.end());
}

[[nodiscard]] std::vector<char> ToChars(const Bytes &data) {
	return std::vector<char>(data.begin(), data.end());
}

[[nodiscard]] bytes::const_span Span(const Bytes &data) {
	return bytes::const_span(
		reinterpret_cast<const gsl::byte*>(data.data()),
		data.size());
}

[[nodiscard]] Bytes FromBytesVector(const bytes::vector &data) {
	return Bytes(
		reinterpret_cast<const uint8_t*>(data.data()),
		reinterpret_cast<const uint8_t*>(data.data()) + data.size());
}

[[nodiscard]] int64_t RandomId() {
	auto result = int64_t(0);
	while (!result) {
		RandomBytes(reinterpret_cast<uint8_t*>(&result), sizeof(result));
	}
	return result;
}

struct DhConfig {
	int g = 0;
	Bytes p;
	Bytes random;
};

struct Chat {
	SecretChatRow row;
	Bytes key;
	bool loaded = false;
	bool working = false;
	int resendRequestedTill = -1;
	std::vector<MessageInfo> messages;
	std::map<int, std::pair<Inbound, int>> pending;
	std::vector<std::pair<int, QByteArray>> early;
};

} // namespace

struct Manager::Impl {
	explicit Impl(not_null<Main::Session*> session)
	: session(session)
	, userId(ID(session->userId().bare & PeerId::kChatTypeMask)) {
		for (auto &&row : AyuDatabase::getSecretChats(userId)) {
			auto chat = Chat();
			chat.row = std::move(row);
			if (chat.row.state == int(ChatState::Ready)) {
				chat.key = FromChars(chat.row.keyData);
			}
			chats.emplace(chat.row.chatId, std::move(chat));
		}
	}

	const not_null<Main::Session*> session;
	const ID userId;
	std::map<int, Chat> chats;
	int openChat = 0;
	rpl::event_stream<> changes;
	rpl::event_stream<int> messageChanges;

	[[nodiscard]] Chat *find(int chatId) {
		const auto i = chats.find(chatId);
		return (i != chats.end()) ? &i->second : nullptr;
	}
	[[nodiscard]] bool creator(const Chat &chat) const {
		return chat.row.creator != 0;
	}
	[[nodiscard]] int x(const Chat &chat) const {
		return creator(chat) ? 0 : 1;
	}
	[[nodiscard]] QString title(const Chat &chat) const {
		if (const auto user = session->data().userLoaded(
				UserId(uint64(chat.row.peerUserId)))) {
			return user->name();
		}
		return QString("User %1").arg(chat.row.peerUserId);
	}
	void save(const Chat &chat) {
		AyuDatabase::saveSecretChat(chat.row);
	}
	void notify(int chatId = 0) {
		changes.fire({});
		if (chatId) {
			messageChanges.fire_copy(chatId);
		}
	}
	void toast(const QString &text) {
		Ui::Toast::Show(text);
	}
	[[nodiscard]] MTPInputEncryptedChat input(const Chat &chat) const {
		return MTP_inputEncryptedChat(
			MTP_int(chat.row.chatId),
			MTP_long(chat.row.accessHash));
	}

	void requestDh(
		Fn<void(const DhConfig &)> done,
		Fn<void()> fail);

	void applyChat(const MTPEncryptedChat &chat);
	void onRequested(const MTPDencryptedChatRequested &data);
	void onChat(const MTPDencryptedChat &data);
	void onDiscarded(int chatId, bool historyDeleted);
	void finishCreator(int chatId, const QByteArray &gB, int64 fingerprint);
	void becomeReady(Chat &chat, const Bytes &key);

	void accept(int chatId);
	void discard(int chatId);
	void start(not_null<UserData*> user);

	void handleEncrypted(const MTPEncryptedMessage &message);
	void onEncrypted(int chatId, int date, const QByteArray &bytes);
	void process(Chat &chat, const Inbound &inbound, int date);
	void drainPending(Chat &chat);
	void requestResend(Chat &chat, int fromIndex, int tillIndex);
	void answerResend(Chat &chat, int start, int end);
	void addMessage(
		Chat &chat,
		int64_t randomId,
		bool outgoing,
		int date,
		MessageKind kind,
		const QString &text,
		int seqIn = 0,
		int seqOut = 0,
		const Bytes &payload = Bytes());
	void removeMessage(Chat &chat, int64_t randomId);
	void loadMessages(Chat &chat);

	void sendObject(
		Chat &chat,
		const Bytes &message,
		int64_t randomId,
		bool service,
		Fn<void()> done = nullptr);
	void sendRaw(
		Chat &chat,
		const Bytes &layerObject,
		int64_t randomId,
		bool service,
		Fn<void()> done);
	void sendNotifyLayer(Chat &chat);
	void sendText(Chat &chat, const QString &text);
	void ackQts(int qts);
};

void Manager::Impl::requestDh(
		Fn<void(const DhConfig &)> done,
		Fn<void()> fail) {
	session->api().request(MTPmessages_GetDhConfig(
		MTP_int(0),
		MTP_int(256)
	)).done([=](const MTPmessages_DhConfig &result) {
		result.match([&](const MTPDmessages_dhConfig &data) {
			auto config = DhConfig();
			config.g = data.vg().v;
			config.p = FromArray(data.vp().v);
			config.random = FromArray(data.vrandom().v);
			if (config.random.size() != 256
				|| !MTP::IsPrimeAndGood(Span(config.p), config.g)) {
				fail();
				return;
			}
			done(config);
		}, [&](const MTPDmessages_dhConfigNotModified &) {
			fail();
		});
	}).fail([=](const MTP::Error &) {
		fail();
	}).send();
}

void Manager::Impl::applyChat(const MTPEncryptedChat &chat) {
	chat.match([&](const MTPDencryptedChatEmpty &) {
	}, [&](const MTPDencryptedChatWaiting &data) {
		if (const auto existing = find(data.vid().v)) {
			existing->row.accessHash = data.vaccess_hash().v;
			save(*existing);
		}
	}, [&](const MTPDencryptedChatRequested &data) {
		onRequested(data);
	}, [&](const MTPDencryptedChat &data) {
		onChat(data);
	}, [&](const MTPDencryptedChatDiscarded &data) {
		onDiscarded(data.vid().v, data.is_history_deleted());
	});
}

void Manager::Impl::onRequested(const MTPDencryptedChatRequested &data) {
	const auto id = data.vid().v;
	if (find(id)
		|| uint64(data.vadmin_id().v) == session->userId().bare) {
		return;
	}
	auto chat = Chat();
	chat.row.fakeId = 0;
	chat.row.userId = userId;
	chat.row.chatId = id;
	chat.row.accessHash = data.vaccess_hash().v;
	chat.row.peerUserId = data.vadmin_id().v;
	chat.row.creator = 0;
	chat.row.state = int(ChatState::Requested);
	chat.row.keyData = ToChars(FromArray(data.vg_a().v));
	chat.row.fingerprint = 0;
	chat.row.myIn = 0;
	chat.row.myOut = 0;
	chat.row.hisIn = 0;
	chat.row.hisLayer = 0;
	chat.row.date = data.vdate().v;
	chat.row.lastDate = data.vdate().v;
	chat.row.unread = 0;
	chat.loaded = true;
	const auto name = title(chat);
	save(chat);
	chats.emplace(id, std::move(chat));
	toast(QString("%1 wants to start a secret chat. Main menu - Secret chats.").arg(name));
	notify();
}

void Manager::Impl::onChat(const MTPDencryptedChat &data) {
	const auto chat = find(data.vid().v);
	if (!chat) {
		return;
	}
	if (chat->row.state == int(ChatState::Waiting) && creator(*chat)) {
		chat->row.accessHash = data.vaccess_hash().v;
		finishCreator(
			chat->row.chatId,
			data.vg_a_or_b().v,
			data.vkey_fingerprint().v);
	} else if (chat->row.state == int(ChatState::Requested)) {
		chat->row.state = int(ChatState::Discarded);
		chat->row.keyData.clear();
		save(*chat);
		toast("The secret chat was accepted on another device.");
		notify();
	}
}

void Manager::Impl::onDiscarded(int chatId, bool historyDeleted) {
	const auto chat = find(chatId);
	if (!chat || chat->row.state == int(ChatState::Discarded)) {
		return;
	}
	chat->row.state = int(ChatState::Discarded);
	chat->row.keyData.clear();
	chat->key.clear();
	if (historyDeleted) {
		AyuDatabase::clearSecretMessages(userId, chatId);
		chat->messages.clear();
		chat->row.unread = 0;
	}
	save(*chat);
	toast(QString("%1 ended the secret chat.").arg(title(*chat)));
	notify(chatId);
}

void Manager::Impl::becomeReady(Chat &chat, const Bytes &key) {
	chat.key = key;
	chat.row.keyData = ToChars(key);
	chat.row.fingerprint = KeyFingerprint(key);
	chat.row.state = int(ChatState::Ready);
	chat.row.myIn = 0;
	chat.row.myOut = 0;
	chat.row.hisIn = 0;
	chat.working = false;
	save(chat);
	sendNotifyLayer(chat);
	notify(chat.row.chatId);
	auto early = std::move(chat.early);
	chat.early.clear();
	for (const auto &[date, bytes] : early) {
		onEncrypted(chat.row.chatId, date, bytes);
	}
}

void Manager::Impl::finishCreator(
		int chatId,
		const QByteArray &gB,
		int64 fingerprint) {
	const auto chat = find(chatId);
	if (!chat || chat->working) {
		return;
	}
	chat->working = true;
	const auto secret = FromChars(chat->row.keyData);
	const auto theirs = FromArray(gB);
	requestDh([=](const DhConfig &config) {
		const auto chat = find(chatId);
		if (!chat) {
			return;
		}
		chat->working = false;
		const auto raw = MTP::CreateAuthKey(
			Span(theirs),
			Span(secret),
			Span(config.p));
		if (raw.empty()) {
			toast("Secret chat key exchange failed.");
			return;
		}
		const auto key = PadKey(FromBytesVector(raw));
		if (KeyFingerprint(key) != fingerprint) {
			toast("Secret chat key fingerprint mismatch.");
			discard(chatId);
			return;
		}
		becomeReady(*chat, key);
	}, [=] {
		if (const auto chat = find(chatId)) {
			chat->working = false;
		}
	});
}

void Manager::Impl::accept(int chatId) {
	const auto chat = find(chatId);
	if (!chat
		|| chat->row.state != int(ChatState::Requested)
		|| chat->working) {
		return;
	}
	chat->working = true;
	const auto gA = FromChars(chat->row.keyData);
	requestDh([=](const DhConfig &config) {
		const auto chat = find(chatId);
		if (!chat) {
			return;
		}
		const auto prime = openssl::BigNum(Span(config.p));
		if (!MTP::IsGoodModExpFirst(openssl::BigNum(Span(gA)), prime)) {
			chat->working = false;
			toast("The secret chat request is invalid.");
			discard(chatId);
			return;
		}
		const auto first = MTP::CreateModExp(
			config.g,
			Span(config.p),
			Span(config.random));
		const auto raw = MTP::CreateAuthKey(
			Span(gA),
			first.randomPower,
			Span(config.p));
		if (raw.empty()) {
			chat->working = false;
			toast("Secret chat key exchange failed.");
			return;
		}
		const auto key = PadKey(FromBytesVector(raw));
		const auto fingerprint = KeyFingerprint(key);
		session->api().request(MTPmessages_AcceptEncryption(
			input(*chat),
			MTP_bytes(ToArray(FromBytesVector(first.modexp))),
			MTP_long(fingerprint)
		)).done([=](const MTPEncryptedChat &result) {
			const auto chat = find(chatId);
			if (!chat) {
				return;
			}
			chat->working = false;
			result.match([&](const MTPDencryptedChat &data) {
				if (data.vkey_fingerprint().v != fingerprint) {
					toast("Secret chat key fingerprint mismatch.");
					return;
				}
				chat->row.accessHash = data.vaccess_hash().v;
				becomeReady(*chat, key);
			}, [&](const auto &) {
				applyChat(result);
			});
		}).fail([=](const MTP::Error &error) {
			if (const auto chat = find(chatId)) {
				chat->working = false;
			}
			toast(QString("Could not accept the secret chat: %1")
				.arg(error.type()));
		}).send();
	}, [=] {
		if (const auto chat = find(chatId)) {
			chat->working = false;
		}
		toast("Could not get the encryption parameters.");
	});
}

void Manager::Impl::discard(int chatId) {
	const auto chat = find(chatId);
	if (!chat) {
		return;
	}
	if (chat->row.state != int(ChatState::Discarded)) {
		session->api().request(MTPmessages_DiscardEncryption(
			MTP_flags(0),
			MTP_int(chatId)
		)).send();
	}
	chat->row.state = int(ChatState::Discarded);
	chat->row.keyData.clear();
	chat->key.clear();
	chat->working = false;
	save(*chat);
	notify(chatId);
}

void Manager::Impl::start(not_null<UserData*> user) {
	const auto peerId = user->id.value & PeerId::kChatTypeMask;
	for (const auto &[id, chat] : chats) {
		if (uint64(chat.row.peerUserId) == peerId
			&& creator(chat)
			&& chat.row.state == int(ChatState::Waiting)) {
			toast("Waiting for the other side to accept the secret chat.");
			return;
		}
	}
	requestDh([=](const DhConfig &config) {
		const auto first = MTP::CreateModExp(
			config.g,
			Span(config.p),
			Span(config.random));
		const auto secret = FromBytesVector(first.randomPower);
		auto randomId = int32(0);
		while (!randomId) {
			RandomBytes(reinterpret_cast<uint8_t*>(&randomId), 4);
			randomId &= 0x7FFFFFFF;
		}
		session->api().request(MTPmessages_RequestEncryption(
			user->inputUser(),
			MTP_int(randomId),
			MTP_bytes(ToArray(FromBytesVector(first.modexp)))
		)).done([=](const MTPEncryptedChat &result) {
			result.match([&](const MTPDencryptedChatWaiting &data) {
				auto chat = Chat();
				chat.row.fakeId = 0;
				chat.row.userId = userId;
				chat.row.chatId = data.vid().v;
				chat.row.accessHash = data.vaccess_hash().v;
				chat.row.peerUserId = ID(peerId);
				chat.row.creator = 1;
				chat.row.state = int(ChatState::Waiting);
				chat.row.keyData = ToChars(secret);
				chat.row.fingerprint = 0;
				chat.row.myIn = 0;
				chat.row.myOut = 0;
				chat.row.hisIn = 0;
				chat.row.hisLayer = 0;
				chat.row.date = data.vdate().v;
				chat.row.lastDate = data.vdate().v;
				chat.row.unread = 0;
				chat.loaded = true;
				save(chat);
				chats[chat.row.chatId] = std::move(chat);
				toast("The secret chat request was sent.");
				notify();
			}, [&](const auto &) {
			});
		}).fail([=](const MTP::Error &error) {
			toast(QString("Could not start the secret chat: %1")
				.arg(error.type()));
		}).send();
	}, [=] {
		toast("Could not get the encryption parameters.");
	});
}

void Manager::Impl::handleEncrypted(const MTPEncryptedMessage &message) {
	message.match([&](const MTPDencryptedMessage &data) {
		onEncrypted(data.vchat_id().v, data.vdate().v, data.vbytes().v);
	}, [&](const MTPDencryptedMessageService &data) {
		onEncrypted(data.vchat_id().v, data.vdate().v, data.vbytes().v);
	});
}

void Manager::Impl::onEncrypted(
		int chatId,
		int date,
		const QByteArray &bytes) {
	const auto chat = find(chatId);
	if (!chat) {
		return;
	} else if (chat->row.state == int(ChatState::Waiting)) {
		chat->early.emplace_back(date, bytes);
		return;
	} else if (chat->row.state != int(ChatState::Ready)) {
		return;
	}
	auto object = Bytes();
	if (!DecryptPacket(chat->key, creator(*chat), FromArray(bytes), object)) {
		return;
	}
	auto inbound = Inbound();
	if (!ParseLayerObject(object, inbound)) {
		sendNotifyLayer(*chat);
		return;
	}
	if (inbound.inSeqNo < 0 || inbound.outSeqNo < 0) {
		return;
	}
	const auto mine = x(*chat);
	if ((inbound.inSeqNo % 2) != (1 - mine)
		|| (inbound.outSeqNo % 2) != mine) {
		return;
	}
	const auto outIndex = inbound.outSeqNo / 2;
	if (outIndex < chat->row.myIn) {
		return;
	}
	if (outIndex > chat->row.myIn) {
		chat->pending.emplace(outIndex, std::make_pair(inbound, date));
		requestResend(*chat, chat->row.myIn, chat->pending.begin()->first - 1);
		return;
	}
	process(*chat, inbound, date);
	drainPending(*chat);
}

void Manager::Impl::drainPending(Chat &chat) {
	while (true) {
		const auto i = chat.pending.find(chat.row.myIn);
		if (i == chat.pending.end()) {
			break;
		}
		const auto entry = std::move(i->second);
		chat.pending.erase(i);
		process(chat, entry.first, entry.second);
	}
}

void Manager::Impl::requestResend(Chat &chat, int fromIndex, int tillIndex) {
	if (tillIndex < fromIndex || tillIndex <= chat.resendRequestedTill) {
		return;
	}
	chat.resendRequestedTill = tillIndex;
	const auto mine = x(chat);
	sendObject(
		chat,
		BuildResend(RandomId(), fromIndex * 2 + mine, tillIndex * 2 + mine),
		0,
		true);
}

void Manager::Impl::answerResend(Chat &chat, int start, int end) {
	const auto from = start / 2;
	const auto till = std::min(end / 2, from + 100);
	loadMessages(chat);
	for (const auto &row : AyuDatabase::getSecretMessages(
			userId,
			chat.row.chatId)) {
		if (!row.outgoing || row.payload.empty()) {
			continue;
		}
		const auto index = row.seqOut / 2;
		if (index < from || index > till) {
			continue;
		}
		const auto layerObject = BuildLayerObject(
			FromChars(row.payload),
			row.seqIn,
			row.seqOut);
		sendRaw(chat, layerObject, row.randomId, false, nullptr);
	}
}

void Manager::Impl::process(Chat &chat, const Inbound &inbound, int date) {
	chat.row.myIn = inbound.outSeqNo / 2 + 1;
	chat.row.hisIn = std::max(chat.row.hisIn, inbound.inSeqNo / 2);
	chat.row.hisLayer = std::max(chat.row.hisLayer, inbound.layer);
	const auto chatId = chat.row.chatId;
	if (inbound.service) {
		switch (inbound.action) {
		case ActionKind::NotifyLayer:
			chat.row.hisLayer = std::max(
				chat.row.hisLayer,
				inbound.actionValue);
			break;
		case ActionKind::Resend:
			answerResend(chat, inbound.resendStart, inbound.resendEnd);
			break;
		case ActionKind::DeleteMessages:
			for (const auto id : inbound.ids) {
				removeMessage(chat, id);
			}
			break;
		case ActionKind::FlushHistory:
			AyuDatabase::clearSecretMessages(userId, chatId);
			chat.messages.clear();
			chat.row.unread = 0;
			break;
		default:
			break;
		}
		save(chat);
		notify(chatId);
		return;
	}
	auto text = QString::fromStdString(inbound.text);
	auto kind = MessageKind::Text;
	if (inbound.hasMedia) {
		kind = MessageKind::Attachment;
		text = text.isEmpty()
			? QString(kAttachmentNote)
			: (text + "\n" + kAttachmentNote);
	}
	addMessage(chat, inbound.randomId, false, date, kind, text);
	if (openChat != chatId) {
		toast(QString("New secret message from %1").arg(title(chat)));
	}
}

void Manager::Impl::addMessage(
		Chat &chat,
		int64_t randomId,
		bool outgoing,
		int date,
		MessageKind kind,
		const QString &text,
		int seqIn,
		int seqOut,
		const Bytes &payload) {
	auto row = SecretMessageRow();
	row.fakeId = 0;
	row.userId = userId;
	row.chatId = chat.row.chatId;
	row.randomId = randomId;
	row.outgoing = outgoing ? 1 : 0;
	row.date = date;
	row.kind = int(kind);
	row.seqIn = seqIn;
	row.seqOut = seqOut;
	row.text = text.toStdString();
	row.payload = ToChars(payload);
	const auto added = AyuDatabase::addSecretMessage(row);
	if (added) {
		loadMessages(chat);
		auto info = MessageInfo();
		info.randomId = randomId;
		info.outgoing = outgoing;
		info.date = date;
		info.kind = kind;
		info.text = text;
		const auto exists = std::any_of(
			chat.messages.begin(),
			chat.messages.end(),
			[&](const MessageInfo &message) {
				return message.randomId == randomId;
			});
		if (!exists) {
			chat.messages.push_back(info);
		}
		chat.row.lastDate = std::max(chat.row.lastDate, date);
		if (!outgoing) {
			if (openChat == chat.row.chatId) {
				chat.row.unread = 0;
			} else {
				++chat.row.unread;
			}
		}
	}
	save(chat);
	notify(chat.row.chatId);
}

void Manager::Impl::removeMessage(Chat &chat, int64_t randomId) {
	AyuDatabase::removeSecretMessage(userId, chat.row.chatId, randomId);
	chat.messages.erase(
		std::remove_if(
			chat.messages.begin(),
			chat.messages.end(),
			[&](const MessageInfo &message) {
				return message.randomId == randomId;
			}),
		chat.messages.end());
}

void Manager::Impl::loadMessages(Chat &chat) {
	if (chat.loaded) {
		return;
	}
	chat.loaded = true;
	chat.messages.clear();
	for (const auto &row : AyuDatabase::getSecretMessages(
			userId,
			chat.row.chatId)) {
		auto info = MessageInfo();
		info.randomId = row.randomId;
		info.outgoing = (row.outgoing != 0);
		info.date = row.date;
		info.kind = MessageKind(row.kind);
		info.text = QString::fromStdString(row.text);
		chat.messages.push_back(std::move(info));
	}
}

void Manager::Impl::sendRaw(
		Chat &chat,
		const Bytes &layerObject,
		int64_t randomId,
		bool service,
		Fn<void()> done) {
	const auto data = ToArray(EncryptPacket(
		chat.key,
		creator(chat),
		layerObject));
	const auto chatId = chat.row.chatId;
	const auto failed = [=](const MTP::Error &error) {
		const auto type = error.type();
		if (type.startsWith("ENCRYPTION_")) {
			if (const auto chat = find(chatId)) {
				chat->row.state = int(ChatState::Discarded);
				chat->key.clear();
				chat->row.keyData.clear();
				save(*chat);
				notify(chatId);
			}
		}
	};
	if (service) {
		session->api().request(MTPmessages_SendEncryptedService(
			input(chat),
			MTP_long(randomId),
			MTP_bytes(data)
		)).done([=] {
			if (done) {
				done();
			}
		}).fail(failed).send();
	} else {
		session->api().request(MTPmessages_SendEncrypted(
			MTP_flags(0),
			input(chat),
			MTP_long(randomId),
			MTP_bytes(data)
		)).done([=] {
			if (done) {
				done();
			}
		}).fail(failed).send();
	}
}

void Manager::Impl::sendObject(
		Chat &chat,
		const Bytes &message,
		int64_t randomId,
		bool service,
		Fn<void()> done) {
	if (chat.row.state != int(ChatState::Ready) || chat.key.empty()) {
		return;
	}
	const auto mine = x(chat);
	++chat.row.myOut;
	const auto inSeq = chat.row.myIn * 2 + mine;
	const auto outSeq = chat.row.myOut * 2 - 1 - mine;
	save(chat);
	const auto layerObject = BuildLayerObject(message, inSeq, outSeq);
	sendRaw(chat, layerObject, randomId ? randomId : RandomId(), service, done);
}

void Manager::Impl::sendNotifyLayer(Chat &chat) {
	sendObject(chat, BuildNotifyLayer(RandomId(), kLayer), 0, true);
}

void Manager::Impl::sendText(Chat &chat, const QString &text) {
	if (chat.row.state != int(ChatState::Ready) || chat.key.empty()) {
		return;
	}
	const auto trimmed = text.trimmed();
	if (trimmed.isEmpty()) {
		return;
	}
	const auto randomId = RandomId();
	const auto message = BuildTextMessage(randomId, trimmed.toStdString());
	const auto mine = x(chat);
	const auto myOut = chat.row.myOut + 1;
	const auto inSeq = chat.row.myIn * 2 + mine;
	const auto outSeq = myOut * 2 - 1 - mine;
	addMessage(
		chat,
		randomId,
		true,
		base::unixtime::now(),
		MessageKind::Text,
		trimmed,
		inSeq,
		outSeq,
		message);
	sendObject(chat, message, randomId, false);
}

void Manager::Impl::ackQts(int qts) {
	if (qts > 0) {
		session->api().request(MTPmessages_ReceivedQueue(
			MTP_int(qts)
		)).send();
	}
}

Manager::Manager(not_null<Main::Session*> session)
: _impl(std::make_unique<Impl>(session)) {
}

Manager::~Manager() = default;

void Manager::handleUpdate(const MTPUpdate &update) {
	switch (update.type()) {
	case mtpc_updateEncryption:
		_impl->applyChat(update.c_updateEncryption().vchat());
		break;
	case mtpc_updateNewEncryptedMessage: {
		const auto &data = update.c_updateNewEncryptedMessage();
		_impl->handleEncrypted(data.vmessage());
		_impl->ackQts(data.vqts().v);
	} break;
	default:
		break;
	}
}

void Manager::handleDifference(
		const QVector<MTPEncryptedMessage> &messages,
		int qts) {
	for (const auto &message : messages) {
		_impl->handleEncrypted(message);
	}
	if (!messages.isEmpty()) {
		_impl->ackQts(qts);
	}
}

std::vector<ChatInfo> Manager::chats() const {
	auto result = std::vector<ChatInfo>();
	for (auto &[id, chat] : _impl->chats) {
		auto info = ChatInfo();
		info.id = id;
		info.peerUserId = uint64(chat.row.peerUserId);
		info.creator = (chat.row.creator != 0);
		info.state = ChatState(chat.row.state);
		info.date = chat.row.date;
		info.lastDate = chat.row.lastDate;
		info.unread = chat.row.unread;
		info.title = _impl->title(chat);
		_impl->loadMessages(chat);
		if (!chat.messages.empty()) {
			info.lastText = chat.messages.back().text;
		}
		result.push_back(std::move(info));
	}
	std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
		return a.lastDate > b.lastDate;
	});
	return result;
}

std::vector<MessageInfo> Manager::messages(int chatId) {
	if (const auto chat = _impl->find(chatId)) {
		_impl->loadMessages(*chat);
		return chat->messages;
	}
	return {};
}

int Manager::pendingRequests() const {
	auto result = 0;
	for (const auto &[id, chat] : _impl->chats) {
		if (chat.row.state == int(ChatState::Requested)) {
			++result;
		}
	}
	return result;
}

int Manager::unreadTotal() const {
	auto result = 0;
	for (const auto &[id, chat] : _impl->chats) {
		result += chat.row.unread;
	}
	return result;
}

void Manager::accept(int chatId) {
	_impl->accept(chatId);
}

void Manager::decline(int chatId) {
	_impl->discard(chatId);
}

void Manager::discard(int chatId) {
	_impl->discard(chatId);
}

void Manager::start(not_null<UserData*> user) {
	_impl->start(user);
}

void Manager::remove(int chatId) {
	const auto chat = _impl->find(chatId);
	if (!chat) {
		return;
	}
	if (chat->row.state != int(ChatState::Discarded)) {
		_impl->discard(chatId);
	}
	AyuDatabase::removeSecretChat(_impl->userId, chatId);
	_impl->chats.erase(chatId);
	_impl->notify(chatId);
}

void Manager::send(int chatId, const QString &text) {
	if (const auto chat = _impl->find(chatId)) {
		_impl->sendText(*chat, text);
	}
}

void Manager::markRead(int chatId) {
	const auto chat = _impl->find(chatId);
	if (!chat || chat->row.unread == 0) {
		return;
	}
	chat->row.unread = 0;
	_impl->save(*chat);
	_impl->notify();
}

void Manager::setOpenChat(int chatId) {
	_impl->openChat = chatId;
}

rpl::producer<> Manager::changes() const {
	return _impl->changes.events();
}

rpl::producer<int> Manager::messageChanges() const {
	return _impl->messageChanges.events();
}

Manager &Get(not_null<Main::Session*> session) {
	static auto managers = std::map<
		Main::Session*,
		std::unique_ptr<Manager>>();
	const auto i = managers.find(session.get());
	if (i != managers.end()) {
		return *i->second;
	}
	const auto raw = session.get();
	auto &result = managers.emplace(
		raw,
		std::make_unique<Manager>(session)).first->second;
	session->lifetime().add([raw] {
		managers.erase(raw);
	});
	return *result;
}

} // namespace AyuSecret
