#pragma once

#include <vector>

namespace Ui {

class GenericBox;

[[nodiscard]] QString SecretKeyHexText(const std::vector<uint8_t> &keyHash);
[[nodiscard]] QString SecretKeyEmojiText(const std::vector<uint8_t> &keyHash);

void FillSecretKeyBox(
	not_null<Ui::GenericBox*> box,
	std::vector<uint8_t> keyHash,
	int64 fingerprint);

} // namespace Ui
