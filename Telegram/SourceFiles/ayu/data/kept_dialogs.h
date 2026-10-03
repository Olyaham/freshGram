#pragma once

#include "base/basic_types.h"
#include "rpl/lifetime.h"

class History;
class PeerData;
class ChannelData;

namespace Data {
class Session;
} // namespace Data

namespace AyuKept {

void setup(not_null<Data::Session*> owner, rpl::lifetime &lifetime);

void markLost(not_null<History*> history);
void note(not_null<History*> history);
void forget(not_null<PeerData*> peer);

void userLeaving(not_null<ChannelData*> channel);
[[nodiscard]] bool takeUserLeaving(not_null<ChannelData*> channel);

} // namespace AyuKept
