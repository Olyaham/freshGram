#pragma once

#include <vector>

namespace Ui {

class GenericBox;

void FillSecretKeyBox(
	not_null<Ui::GenericBox*> box,
	std::vector<uint8_t> keyHash,
	std::vector<uint8_t> keySha256);

} // namespace Ui
