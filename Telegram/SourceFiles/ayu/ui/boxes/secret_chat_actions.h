#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Main {
class Session;
} // namespace Main

class PeerData;

namespace Ui {

class GenericBox;

void FillSecretTimerBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	int chatId,
	int current);

void ShowSecretTimer(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
void ShowSecretKey(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
void ConfirmSecretClear(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);
void ConfirmSecretEnd(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

} // namespace Ui
