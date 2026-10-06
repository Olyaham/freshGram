#pragma once

namespace Data {
class Thread;
} // namespace Data

namespace AyuMentions {

void GuardJump(not_null<Data::Thread*> thread, MsgId msgId);

} // namespace AyuMentions
