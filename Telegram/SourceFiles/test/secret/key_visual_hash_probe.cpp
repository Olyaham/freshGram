#include <QtCore/QString>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QImage>

#include "ayu/secret/secret_crypto.cpp"
#include "ayu/secret/secret_tl.h"
#include "ayu/ui/boxes/secret_key_emoji.h"
#include <openssl/evp.h>

#include <array>
#include <cassert>
#include <map>
#include <optional>
#include <string>

using namespace AyuSecret;
using Ui::kSecretEmojis;
using ID = int64_t;
using int64 = int64_t;
using uint64 = uint64_t;
constexpr auto kExtrasMagic = 0x31435941U;
constexpr auto kChatRowMagic = 0x32524359U;
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

struct Manager { struct Impl; };
struct Manager::Impl {
	ID userId = 7;
	std::map<int, Chat> chats;
	std::vector<std::pair<int, int64_t>> services;
	void load() {
		chats.clear();
@LOAD_CHATS@
	}
	Chat *find(int id) { return &chats.at(id); }
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
	ChatInfo makeInfo(const Chat &chat, bool withKey) const;
};
@SAVE_CHAT@
@BECOME_READY@
@RESET_KEY_USAGE@
@CLEAR_OUR_EXCHANGE@
@COMMIT_KEY@
@ACCEPT_KEY@
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
	if (mode.starts_with("legacy-")) {
		auto chat = Chat();
		manager.becomeReady(chat, Key(1));
		if (mode == "legacy-raw") {
			chat.row.keyData = ToChars(chat.key);
		} else {
			chat.keyHash.clear();
			chat.row.keyData = EncodeExtras(chat);
			chat.row.keyData.resize(chat.row.keyData.size()
				- (mode == "legacy-accounting" ? 4 : 16));
		}
		assert(SealChatRow(chat.row));
		AyuDatabase::Saved = chat.row;
		AyuDatabase::Writes = 0;
		manager.load();
		assert(AyuDatabase::Writes == 1);
		auto &loaded = manager.chats.at(11);
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
	assert(AyuDatabase::Writes == 0);
	auto &chat = manager.chats.at(11);
	const auto before = Snapshot(manager, chat);
	if (mode == "corrupt") {
		chat.row.keyData = EncodeExtras(chat);
		chat.row.keyData.pop_back();
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
