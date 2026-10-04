#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {

class GenericBox;

void FillSecretChatsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller);

void FillSecretChatBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	int chatId);

} // namespace Ui
