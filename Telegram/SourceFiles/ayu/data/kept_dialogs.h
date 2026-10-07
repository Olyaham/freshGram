#pragma once

#include "base/basic_types.h"
#include "rpl/lifetime.h"

class History;
class HistoryItem;
class PeerData;
class ChannelData;
class ChatData;

namespace Data {
class Session;
} // namespace Data

namespace AyuKept {

void setup(not_null<Data::Session*> owner, rpl::lifetime &lifetime);

void markLost(not_null<History*> history);
[[nodiscard]] bool keepOnDelete(not_null<History*> history);
void note(not_null<History*> history);
void forget(not_null<PeerData*> peer);
[[nodiscard]] bool showsDeleted(not_null<PeerData*> peer);
void revive(not_null<History*> history, not_null<HistoryItem*> item);
void checkWiped(not_null<History*> history);

void userLeaving(not_null<PeerData*> peer);
[[nodiscard]] bool takeUserLeaving(not_null<PeerData*> peer);
void chatAmInChanged(not_null<ChatData*> chat);

} // namespace AyuKept
