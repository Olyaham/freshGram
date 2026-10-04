#pragma once

#include "ayu/data/entities.h"

class HistoryItem;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace AyuSent {

void note(not_null<HistoryItem*> item);
void edited(not_null<HistoryItem*> item);

[[nodiscard]] std::vector<SentMessage> load(
	not_null<Main::Session*> session,
	ID dialogId,
	const std::string &query,
	int offset,
	int limit);

[[nodiscard]] PeerData *resolvePeer(
	not_null<Main::Session*> session,
	ID dialogId);

} // namespace AyuSent
