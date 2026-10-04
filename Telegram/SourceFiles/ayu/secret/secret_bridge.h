#pragma once

#include "ayu/secret/secret_manager.h"

#include <map>
#include <optional>
#include <set>

class History;
class HistoryItem;
class UserData;

namespace AyuSecret {

class Bridge final {
public:
	Bridge(not_null<Main::Session*> session, not_null<Manager*> manager);
	~Bridge();

	void refreshAll();
	void refreshChat(int chatId);
	void removeAll();
	void removeChat(int chatId);

	[[nodiscard]] std::optional<std::pair<int, int64_t>> lookup(
		not_null<const HistoryItem*> item) const;
	[[nodiscard]] HistoryItem *item(int chatId, int64_t randomId) const;
	[[nodiscard]] History *history(int chatId) const;
	[[nodiscard]] int64_t randomIdOf(
		int chatId,
		FullMsgId id) const;

	void itemsDeletedByUser(
		const std::vector<not_null<HistoryItem*>> &items);
	void historyClearedByUser(not_null<History*> history);
	void messagesRead(not_null<History*> history);
	void mediaOpened(not_null<HistoryItem*> item);

private:
	struct Binding {
		int chatId = 0;
		UserData *user = nullptr;
		History *history = nullptr;
		int revision = -2;
		std::map<int64_t, MsgId> items;
		std::set<int64_t> requested;
		bool typing = false;
		bool asked = false;
		int lastState = -1;
	};

	[[nodiscard]] Binding &ensureBinding(const ChatInfo &info);
	void syncMessages(Binding &binding, const ChatInfo &info);
	[[nodiscard]] HistoryItem *createItem(
		Binding &binding,
		const ChatInfo &info,
		const MessageData &message,
		bool unread);
	void askRequest(Binding &binding, const ChatInfo &info);

	const not_null<Main::Session*> _session;
	const not_null<Manager*> _manager;
	std::map<int, Binding> _bindings;
	std::map<MsgId, std::pair<int, int64_t>> _byMsg;
	bool _refreshing = false;
	rpl::lifetime _lifetime;

};

[[nodiscard]] Bridge &BridgeFor(not_null<Main::Session*> session);
void Start(not_null<Main::Session*> session);

} // namespace AyuSecret
