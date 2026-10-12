#pragma once

namespace AyuSecret {

[[nodiscard]] bool PasscodeSet();
[[nodiscard]] bool Enabled();
void SyncPolicy();
void HandlePasscodeChange();

} // namespace AyuSecret
