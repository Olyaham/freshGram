#pragma once

#include "base/basic_types.h"
#include "rpl/event_stream.h"

#include <QtCore/QString>

class UserData;

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

enum class MessageKind : int {
	Text = 0,
	Attachment = 1,
	Note = 2,
};

struct ChatInfo {
	int id = 0;
	uint64 peerUserId = 0;
	bool creator = false;
	ChatState state = ChatState::Discarded;
	int date = 0;
	int lastDate = 0;
	int unread = 0;
	QString title;
	QString lastText;
};

struct MessageInfo {
	int64 randomId = 0;
	bool outgoing = false;
	int date = 0;
	MessageKind kind = MessageKind::Text;
	QString text;
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
	[[nodiscard]] std::vector<MessageInfo> messages(int chatId);
	[[nodiscard]] int pendingRequests() const;
	[[nodiscard]] int unreadTotal() const;

	void accept(int chatId);
	void decline(int chatId);
	void discard(int chatId);
	void start(not_null<UserData*> user);
	void remove(int chatId);
	void send(int chatId, const QString &text);
	void markRead(int chatId);
	void setOpenChat(int chatId);

	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] rpl::producer<int> messageChanges() const;

private:
	struct Impl;
	const std::unique_ptr<Impl> _impl;

};

[[nodiscard]] Manager &Get(not_null<Main::Session*> session);

} // namespace AyuSecret
