#include <QtCore/QString>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QImage>

#include "ayu/secret/secret_crypto.cpp"
#include "ayu/secret/secret_tl.h"
#include "ayu/secret/secret_rekey.h"
#if __has_include("ayu/ui/boxes/secret_key_emoji.h")
#include "ayu/ui/boxes/secret_key_emoji.h"
#endif
#include <openssl/evp.h>

#include <array>
#include <cassert>
#include <functional>
#include <map>
#include <optional>
#include <string>

using namespace AyuSecret;
#if __has_include("ayu/ui/boxes/secret_key_emoji.h")
using Ui::kSecretEmojis;
#endif
using ID = int64_t;
using int64 = int64_t;
using uint64 = uint64_t;
constexpr auto kExtrasMagic = 0x31435941U;
constexpr auto kChatRowMagic = 0x32524359U;
constexpr auto kPfsStateMagic = 0x31534650U;
constexpr auto kRekeyAfterUsesOut = 100;
constexpr auto kRekeyAfterUsesIn = 120;
constexpr auto kRekeyAfterTime = 7 * 86400;
constexpr auto kPfsOurTimeout = 3600;
namespace base::unixtime { int now() { return 1000; } }
namespace crl { int64_t now() { return 1000; } }

struct SecretChatRow {
	ID userId = 7;
	int chatId = 11, fakeId = 1;
	int64_t accessHash = 0, peerUserId = 0, fingerprint = 0;
	int creator = 0, state = 0, myIn = 0, myOut = 0, hisIn = 0;
	int hisLayer = 0, date = 0, lastDate = 0, unread = 0;
	std::vector<char> keyData;
};
struct EarlyEntry {
	int date = 0;
	Bytes bytes;
	std::optional<int> file;
};
@CHAT@
	bool locked = false;
	bool working = false;
	int64_t typingUntil = 0;
	std::vector<EarlyEntry> early;
};
enum class ChatState { Requested, Waiting, Ready, Discarded };
@CHAT_INFO@
@ROW_STATE@
@TO_CHARS@
@FROM_CHARS@
@CHAT_CONTEXT@

namespace AyuSecret::Vault {
@VAULT_CONSTANTS@
bool DeriveKey(std::array<uint8_t, 32> &key) {
	key.fill(0x42);
	return true;
}
@VAULT_IS_SEALED@
@VAULT_IS_BOUND@
@VAULT_SEAL@
@VAULT_OPEN@
}
@SEAL_ROW@
@OPEN_ROW@
@ENCODE_EXTRAS@
@DECODE_EXTRAS@

namespace AyuDatabase {
SecretChatRow Saved;
int Writes = 0;
void saveSecretChat(const SecretChatRow &row) { Saved = row; ++Writes; }
std::vector<SecretChatRow> getSecretChats(ID) { return { Saved }; }
}
int PeerIdFromFakeId(int id) { return id; }

Bytes Key(int rotation) {
	auto result = Bytes(kKeySize);
	for (auto i = 0; i != kKeySize; ++i) {
		result[i] = uint8_t(i + rotation * 17);
	}
	return result;
}
namespace openssl {
struct BigNum { explicit BigNum(const Bytes &) {} };
}
namespace MTP {
Bytes NextKey;
struct ModExp { Bytes modexp = { 1 }, randomPower = { 2 }; };
ModExp CreateModExp(int, const Bytes &, const Bytes &) { return {}; }
bool IsGoodModExpFirst(const openssl::BigNum &, const openssl::BigNum &) {
	return true;
}
Bytes CreateAuthKey(
		const Bytes &, const Bytes &, const Bytes &) {
	return NextKey;
}
}
const Bytes &Span(const Bytes &value) { return value; }
const Bytes &FromBytesVector(const Bytes &value) { return value; }
struct Inbound { int64_t exchangeId = 0, fingerprint = 0; Bytes value; };
int64_t RandomId() { return 31; }
int BuildAbortKey(int64_t, int64_t) { return 1; }
int BuildCommitKey(int64_t, int64_t, int64_t) { return 2; }
int BuildNoop(int64_t) { return 3; }
int BuildAcceptKey(int64_t, int64_t, const Bytes &, int64_t) { return 4; }
int BuildRequestKey(int64_t, int64_t, const Bytes &) { return 5; }
struct DhConfig { int g = 2; Bytes p = { 3 }, random = { 4 }; };

struct Manager { struct Impl; };
struct Manager::Impl {
	ID userId = 7;
	std::map<int, Chat> chats;
	std::vector<std::pair<int, int64_t>> services;
	std::function<void(const DhConfig &)> dhDone;
	std::function<void()> dhFail;
	void requestDh(
			std::function<void(const DhConfig &)> done,
			std::function<void()> fail) {
		dhDone = std::move(done);
		dhFail = std::move(fail);
	}
	void load() {
		chats.clear();
@LOAD_CHATS@
	}
	Chat *find(int id) { return chats.contains(id) ? &chats.at(id) : nullptr; }
	QString title(const Chat &) const { return QString(); }
	void notify(int) {}
	void sendNotifyLayer(Chat &) {}
	void onEncrypted(int, int, const Bytes &, const int *) {}
	void sendService(Chat &chat, int action) {
		services.emplace_back(action, KeyFingerprint(chat.key));
	}
	void saveChat(Chat &chat);
	void becomeReady(Chat &chat, const Bytes &key);
	void resetKeyUsage(Chat &chat);
	void clearOurExchange(Chat &chat);
	void handleCommitKey(Chat &chat, const Inbound &inbound);
	void handleAcceptKey(Chat &chat, const Inbound &inbound);
	void handleRequestKey(Chat &chat, const Inbound &inbound);
	void handleAbortKey(Chat &chat, const Inbound &inbound);
	void startRekey(Chat &chat);
	void maybeStartRekey(Chat &chat);
	ChatInfo makeInfo(const Chat &chat, bool withKey) const;
};
@SAVE_CHAT@
@BECOME_READY@
@RESET_KEY_USAGE@
@CLEAR_OUR_EXCHANGE@
@COMMIT_KEY@
@ACCEPT_KEY@
@REQUEST_KEY@
@ABORT_KEY@
@START_REKEY@
@MAYBE_REKEY@
@MAKE_INFO@
@IDENTICON@
@HEX_TEXT@
@EMOJI_TEXT@

QByteArray Snapshot(Manager::Impl &manager, const Chat &chat) {
	const auto info = manager.makeInfo(chat, true);
	assert(manager.makeInfo(chat, false).keyHash.empty());
	const auto image = MakeIdenticon(info.keyHash);
	const auto pixels = QByteArray(
		reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());
	return QJsonDocument(QJsonObject{
		{ "hash", QString::fromLatin1(QByteArray(
			reinterpret_cast<const char*>(info.keyHash.data()),
			int(info.keyHash.size())).toHex()) },
		{ "hex", SecretKeyHexText(info.keyHash) },
		{ "emoji", SecretKeyEmojiText(info.keyHash) },
		{ "pixels", QString::fromLatin1(QCryptographicHash::hash(
			pixels, QCryptographicHash::Sha256).toHex()) },
	}).toJson(QJsonDocument::Compact);
}

void Rotate(Manager::Impl &manager, Chat &chat, int rotation, bool initiator) {
	const auto old = KeyFingerprint(chat.key);
	const auto next = Key(rotation);
	const auto fingerprint = KeyFingerprint(next);
	const auto inbound = Inbound{ 100 + rotation, fingerprint, Bytes{ 1 } };
	manager.services.clear();
	if (initiator) {
		chat.pfsOurExchange = inbound.exchangeId;
		chat.pfsOurSecret = Bytes{ 2 };
		chat.pfsOurP = Bytes{ 3 };
		MTP::NextKey = next;
		manager.handleAcceptKey(chat, inbound);
		assert(manager.services.front() == std::make_pair(2, old));
	} else {
		chat.pfsExchange = inbound.exchangeId;
		chat.pfsPending = next;
		manager.handleCommitKey(chat, inbound);
		assert(manager.services.back() == std::make_pair(3, fingerprint));
	}
	assert(chat.key == next && chat.row.fingerprint == fingerprint);
	auto plain = Bytes();
	const auto message = Bytes{ 1, 2, 3, 4 };
	assert(DecryptPacket(next, false, EncryptPacket(next, true, message), plain));
	assert(plain == message);
}

void WriteSaved(const QString &path) {
	auto file = QFile(path);
	assert(file.open(QIODevice::WriteOnly));
	const auto &bytes = AyuDatabase::Saved.keyData;
	assert(file.write(bytes.data(), bytes.size()) == qint64(bytes.size()));
}
void ReadSaved(const QString &path) {
	auto file = QFile(path);
	assert(file.open(QIODevice::ReadOnly));
	const auto bytes = file.readAll();
	AyuDatabase::Saved.keyData.assign(bytes.begin(), bytes.end());
}

int main(int argc, char **argv) {
	assert(argc == 3);
	const auto mode = std::string(argv[1]);
	const auto path = QString::fromUtf8(argv[2]);
	auto manager = Manager::Impl();
	if (mode == "layers") {
		auto hash = Bytes();
		UpdateKeyVisualHash(hash, Key(0), 45);
		assert(hash.size() == 16);
		const auto original = hash;
		UpdateKeyVisualHash(hash, Key(1), 45);
		assert(hash == original);
		auto chat = Chat();
		chat.row.state = int(ChatState::Ready);
		chat.key = Key(1);
		chat.keyHash = hash;
		manager.saveChat(chat);
		manager.load();
		assert(manager.chats.at(11).keyHash == original);
		UpdateKeyVisualHash(hash, Key(1), 46);
		const auto upgraded = hash;
		assert(hash.size() == 36);
		assert(std::equal(original.begin(), original.end(), hash.begin()));
		const auto current = KeyVisualHash(Key(1));
		assert(std::equal(current.begin() + 16, current.end(), hash.begin() + 16));
		UpdateKeyVisualHash(hash, Key(2), 73);
		assert(hash == upgraded);
		chat.keyHash = hash;
		manager.saveChat(chat);
		manager.load();
		assert(manager.chats.at(11).keyHash == upgraded);
		auto empty = Bytes();
		UpdateKeyVisualHash(empty, Bytes(), 73);
		assert(empty.empty());
		return 0;
	}
	if (mode == "create") {
		auto chat = Chat();
		chat.row.state = int(ChatState::Waiting);
		manager.becomeReady(chat, Key(0));
		WriteSaved(path);
		puts(Snapshot(manager, chat).constData());
		return 0;
	}
	if (mode == "failure" || mode == "gates" || mode == "supersede" || mode == "timeout") {
		auto &chat = manager.chats[11];
		manager.becomeReady(chat, Key(0));
		chat.row.hisLayer = 73;
		const auto inbound = Inbound{ 99, 0, Bytes{ 1 } };
		if (mode == "failure") {
			manager.handleRequestKey(chat, inbound);
			assert(chat.pfsExchange == 99 && chat.pfsPending.empty());
			manager.saveChat(chat);
			const auto failure = manager.dhFail;
			failure();
			assert(chat.pfsExchange == 0 && chat.pfsPending.empty());
			manager.load();
			auto &restored = manager.chats.at(11);
			assert(restored.pfsExchange == 0);
			manager.handleRequestKey(restored, inbound);
			restored.pfsExchange = 101;
			manager.dhFail();
			assert(restored.pfsExchange == 101);
			restored.pfsExchange = 99;
			restored.pfsPending = Key(1);
			failure();
			assert(restored.pfsExchange == 99 && restored.pfsPending == Key(1));
			manager.chats.clear();
			failure();
		} else if (mode == "timeout") {
			chat.pfsOurExchange = 99;
			chat.pfsOurStartedAt = base::unixtime::now() - kPfsOurTimeout - 1;
			chat.pfsOurSecret = Bytes{ 2 };
			chat.pfsOurP = Bytes{ 3 };
			manager.maybeStartRekey(chat);
			assert(!chat.pfsOurExchange && chat.pfsOurSecret.empty());
			manager.load();
			auto &loaded = manager.chats.at(11);
			assert(!loaded.pfsOurExchange);
			loaded.pfsExchange = 101;
			loaded.pfsPending = Key(1);
			manager.maybeStartRekey(loaded);
			assert(loaded.pfsExchange == 101 && loaded.pfsPending == Key(1));
			assert(!manager.dhDone);
		} else if (mode == "supersede") {
			MTP::NextKey = Key(1);
			manager.handleRequestKey(chat, inbound);
			const auto staleFailure = manager.dhFail;
			manager.dhDone(DhConfig());
			assert(chat.pfsPending == Key(1));
			manager.handleRequestKey(chat, Inbound{ 101, 0, Bytes{ 1 } });
			staleFailure();
			assert(chat.pfsExchange == 101);
			MTP::NextKey = Key(2);
			manager.dhDone(DhConfig());
			assert(chat.pfsPending == Key(2));
			manager.handleCommitKey(chat, Inbound{ 99, KeyFingerprint(Key(1)), {} });
			assert(chat.key == Key(0));
			manager.handleCommitKey(chat, Inbound{ 101, KeyFingerprint(Key(2)), {} });
			assert(chat.key == Key(2));
		} else {
			chat.keyUsesOut = 100;
			chat.row.hisLayer = 45;
			manager.maybeStartRekey(chat);
			manager.startRekey(chat);
			assert(!manager.dhDone);
			chat.row.hisLayer = 46;
			manager.maybeStartRekey(chat);
			assert(manager.dhDone);
			chat.row.hisLayer = 45;
			manager.dhDone(DhConfig());
			assert(!chat.pfsOurExchange);
		}
		return 0;
	}
	if (mode.starts_with("pending-")) {
		auto &chat = manager.chats[11];
		manager.becomeReady(chat, Key(0));
		chat.row.hisLayer = 73;
		if (mode == "pending-initiator") {
			manager.startRekey(chat);
			manager.dhDone(DhConfig());
			assert(chat.pfsOurExchange == 31);
		} else {
			MTP::NextKey = Key(1);
			manager.handleRequestKey(chat, Inbound{ 99, 0, Bytes{ 1 } });
			if (mode == "pending-responder") {
				manager.dhDone(DhConfig());
			}
		}
		manager.saveChat(chat);
		WriteSaved(path);
		return 0;
	}
	if (mode.starts_with("legacy-")) {
		auto chat = Chat();
		manager.becomeReady(chat, Key(1));
		if (mode == "legacy-raw") {
			chat.row.keyData = ToChars(chat.key);
		} else {
			auto writer = Writer();
			writer.writeUInt(kExtrasMagic);
			writer.writeBytes(chat.key.data(), chat.key.size());
			writer.writeBytes(nullptr, 0);
			writer.writeInt(0);
			writer.writeLong(0);
			writer.writeBytes(nullptr, 0);
			if (mode == "legacy-accounting") {
				writer.writeInt(base::unixtime::now());
				writer.writeInt(0);
				writer.writeInt(0);
			}
			chat.row.keyData = ToChars(writer.data());
		}
		assert(SealChatRow(chat.row));
		AyuDatabase::Saved = chat.row;
		AyuDatabase::Writes = 0;
		manager.load();
		assert(AyuDatabase::Writes == 1);
		auto &loaded = manager.chats.at(11);
		assert(loaded.keyInstalledAt == base::unixtime::now());
		assert(loaded.keyUsesOut == 0 && loaded.keyUsesIn == 0);
		loaded.row.hisLayer = 73;
		manager.maybeStartRekey(loaded);
		assert(!manager.dhDone);
		assert(loaded.keyHash == KeyVisualHash(Key(1)));
		const auto before = Snapshot(manager, loaded);
		Rotate(manager, loaded, 2, false);
		Rotate(manager, loaded, 3, true);
		assert(Snapshot(manager, loaded) == before);
		WriteSaved(path);
		puts(before.constData());
		return 0;
	}
	ReadSaved(path);
	manager.load();
	if (mode.starts_with("resume-")) {
		auto &chat = manager.chats.at(11);
		if (mode == "resume-orphan") {
			assert(chat.pfsExchange == 0 && chat.pfsPending.empty());
			assert(AyuDatabase::Writes == 1);
		} else if (mode == "resume-initiator") {
			assert(chat.pfsOurExchange == 31 && !chat.pfsOurSecret.empty());
			assert(!chat.pfsOurP.empty() && chat.pfsOurStartedAt == 1000);
			MTP::NextKey = Key(1);
			manager.handleAcceptKey(chat, Inbound{ 31, KeyFingerprint(Key(1)), Bytes{ 1 } });
			assert(chat.key == Key(1) && chat.pfsOurExchange == 0);
		} else {
			assert(chat.pfsExchange == 99 && chat.pfsPending == Key(1));
			assert(!chat.pfsAnswerGB.empty());
			manager.handleRequestKey(chat, Inbound{ 99, 0, Bytes{ 1 } });
			assert(!manager.dhDone && manager.services.back().first == 4);
			manager.handleCommitKey(chat, Inbound{ 99, KeyFingerprint(Key(1)), {} });
			assert(chat.key == Key(1) && chat.pfsExchange == 0);
		}
		assert(chat.keyHash == KeyVisualHash(Key(0)));
		WriteSaved(path);
		return 0;
	}
	assert(AyuDatabase::Writes == 0);
	auto &chat = manager.chats.at(11);
	const auto before = Snapshot(manager, chat);
	if (mode == "corrupt") {
		chat.row.keyData = EncodeExtras(chat);
		auto reader = Reader(
			reinterpret_cast<const uint8_t*>(chat.row.keyData.data()),
			chat.row.keyData.size());
		reader.readUInt(); reader.readBytes(); reader.readBytes();
		reader.readInt(); reader.readLong(); reader.readBytes();
		reader.readInt(); reader.readInt(); reader.readInt();
		const auto hashOffset = chat.row.keyData.size() - reader.left();
		chat.row.keyData.resize(hashOffset + 3);
		auto broken = Chat();
		broken.row = chat.row;
		assert(!DecodeExtras(broken));
		assert(broken.keyHash.empty());
		return 0;
	}
	if (mode.starts_with("rotate-")) {
		Rotate(manager, chat, 1, mode == "rotate-initiator");
		assert(Snapshot(manager, chat) == before);
		Rotate(manager, chat, 2, mode != "rotate-initiator");
		assert(Snapshot(manager, chat) == before);
		assert(chat.key != Key(0) && chat.otherKey != Key(0));
		assert(chat.keyHash != KeyVisualHash(chat.key));
		WriteSaved(path);
	} else {
		assert((mode == "restart" && chat.key == Key(2))
			|| (mode == "restart-legacy" && chat.key == Key(3)));
	}
	puts(Snapshot(manager, chat).constData());
}
