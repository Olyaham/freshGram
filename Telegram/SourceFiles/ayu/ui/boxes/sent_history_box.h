#pragma once

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {

class GenericBox;

void FillSentHistoryBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	PeerData *current);

} // namespace Ui
