#pragma once

#include "ayu/secret/secret_model.h"
#include "base/basic_types.h"
#include "rpl/event_stream.h"
#include "ui/text/text_entity.h"

#include <QtCore/QString>

class UserData;

namespace AyuSecret {
class Bridge;
} // namespace AyuSecret

namespace Main {
class Session;
} // namespace Main

namespace AyuSecret {

enum class ChatState : int {
	Requested = 0,
	Waiting = 1,
	Ready = 2,
	Discarded = 3,
};

struct ChatInfo {
	int id = 0;
	uint64 peerUserId = 0;
	bool creator = false;
	ChatState state = ChatState::Discarded;
	int date = 0;
	int lastDate = 0;
	int unread = 0;
	int ttl = 0;
	int layer = 0;
	bool typing = false;
	int64 fingerprint = 0;
	std::vector<uint8_t> keyHash;
	QString title;
	QString lastText;
};

struct OutgoingFile {
	QString path;
	QByteArray bytes;
	QString name;
	QString mime;
	QString caption;
	int64 replyTo = 0;
	MediaType kind = MediaType::None;
	int duration = 0;
	QByteArray waveform;
	bool round = false;
	int width = 0;
	int height = 0;
};

class Manager final {
public:
	explicit Manager(not_null<Main::Session*> session);
	~Manager();

	void handleUpdate(const MTPUpdate &update);
	void handleDifference(
		const QVector<MTPEncryptedMessage> &messages,
		int qts);

	[[nodiscard]] std::vector<ChatInfo> chats() const;
	[[nodiscard]] std::optional<ChatInfo> chat(int chatId) const;
	[[nodiscard]] std::vector<MessageData> messages(int chatId);
	[[nodiscard]] int revision(int chatId) const;
	[[nodiscard]] int pendingRequests() const;
	[[nodiscard]] int unreadTotal() const;
	[[nodiscard]] double progress(int chatId, int64 randomId) const;

	void accept(int chatId);
	void decline(int chatId);
	void discard(int chatId);
	void remove(int chatId);
	void start(not_null<UserData*> user);

	void sendText(int chatId, TextWithEntities text, int64 replyTo = 0);
	void sendFile(int chatId, OutgoingFile outgoing);
	void downloadMedia(int chatId, int64 randomId);
	void openMessage(int chatId, int64 randomId);
	[[nodiscard]] QString exportFile(int chatId, int64 randomId);
	[[nodiscard]] std::optional<MessageData> message(
		int chatId,
		int64 randomId);
	[[nodiscard]] QByteArray readFile(int chatId, int64 randomId);
	void setTtl(int chatId, int seconds);
	void deleteMessages(int chatId, const std::vector<int64_t> &randomIds);
	void clearHistory(int chatId);
	void setTyping(int chatId);

	void markRead(int chatId);
	void setOpenChat(int chatId);

	[[nodiscard]] Bridge &bridge() const {
		return *_bridge;
	}

	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] rpl::producer<int> messageChanges() const;

private:
	struct Impl;
	const std::unique_ptr<Impl> _impl;
	std::unique_ptr<Bridge> _bridge;

};

[[nodiscard]] Manager &Get(not_null<Main::Session*> session);

} // namespace AyuSecret
