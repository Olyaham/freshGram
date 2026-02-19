// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2025
#include "settings_appearance.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/boxes/font_selector.h"
#include "ayu/ui/components/icon_picker.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "inline_bots/bot_attach_web_view.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

namespace {

bool HasDrawerBots(not_null<Window::SessionController*> controller) {
	// todo: maybe iterate through all accounts
	const auto bots = &controller->session().attachWebView();
	for (const auto &bot : bots->attachBots()) {
		if (!bot.inMainMenu || !bot.media) {
			continue;
		}

		return true;
	}

	return false;
}

}

rpl::producer<QString> AyuAppearance::title() {
	return tr::ayu_CategoryAppearance();
}

AyuAppearance::AyuAppearance(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
	setupContent(controller);
}

void SetupAppIcon(not_null<Ui::VerticalLayout*> container) {
	AddSubsectionTitle(container, tr::ayu_AppIconHeader());
	container->add(
		object_ptr<IconPicker>(container),
		st::settingsCheckboxPadding);

#ifdef Q_OS_WIN
	AddDivider(container);
	AddSkip(container);
	AddSettingToggle(container, tr::ayu_HideNotificationBadge(), &AyuSettings::hideNotificationBadge, &AyuSettings::setHideNotificationBadge);
	AddSkip(container);
	AddDividerText(container, tr::ayu_HideNotificationBadgeDescription());
	AddSkip(container);
#endif
}

void SetupAppearance(not_null<Ui::VerticalLayout*> container, not_null<Window::SessionController*> controller) {
	auto *settings = &AyuSettings::getInstance();

	AddSubsectionTitle(container, tr::ayu_CategoryAppearance());

	AddSettingToggle(container, tr::ayu_MaterialSwitches(), &AyuSettings::materialSwitches, &AyuSettings::setMaterialSwitches);
	AddSettingToggle(container, tr::ayu_RemoveMessageTail(), &AyuSettings::removeMessageTail, &AyuSettings::setRemoveMessageTail);
	AddSettingToggle(container, tr::ayu_DisableCustomBackgrounds(), &AyuSettings::disableCustomBackgrounds, &AyuSettings::setDisableCustomBackgrounds);

	const auto monoButton = AddButtonWithLabel(
		container,
		tr::ayu_MonospaceFont(),
		rpl::single(
			settings->monoFont().isEmpty() ? tr::ayu_FontDefault(tr::now) : settings->monoFont()
		),
		st::settingsButtonNoIcon);
	const auto monoGuard = Ui::CreateChild<base::binary_guard>(monoButton.get());

	monoButton->addClickHandler(
		[=]
		{
			*monoGuard = AyuUi::FontSelectorBox::Show(
				controller,
				[=](const QString& font)
				{
					AyuSettings::getInstance().setMonoFont(font);
				});
		});

	AddSectionDivider(container);
}

void SetupChatFolders(not_null<Ui::VerticalLayout*> container) {
	AddSubsectionTitle(container, tr::ayu_ChatFoldersHeader());

	AddSettingToggle(container, tr::ayu_HideNotificationCounters(), &AyuSettings::hideNotificationCounters, &AyuSettings::setHideNotificationCounters);
	AddSettingToggle(container, tr::ayu_HideAllChats(), &AyuSettings::hideAllChatsFolder, &AyuSettings::setHideAllChatsFolder);

	AddSectionDivider(container);
}

void SetupDrawerElements(not_null<Ui::VerticalLayout*> container, not_null<Window::SessionController*> controller) {
	AddSubsectionTitle(container, tr::ayu_DrawerElementsHeader());

	AddSettingToggle(container, tr::lng_menu_my_profile(), &AyuSettings::showMyProfileInDrawer, &AyuSettings::setShowMyProfileInDrawer, st::menuIconProfile);

	if (HasDrawerBots(controller)) {
		AddSettingToggle(container, tr::lng_filters_type_bots(), &AyuSettings::showBotsInDrawer, &AyuSettings::setShowBotsInDrawer, st::menuIconBot);
	}

	AddSettingToggle(container, tr::lng_create_group_title(), &AyuSettings::showNewGroupInDrawer, &AyuSettings::setShowNewGroupInDrawer, st::menuIconGroups);
	AddSettingToggle(container, tr::lng_create_channel_title(), &AyuSettings::showNewChannelInDrawer, &AyuSettings::setShowNewChannelInDrawer, st::menuIconChannel);
	AddSettingToggle(container, tr::lng_menu_contacts(), &AyuSettings::showContactsInDrawer, &AyuSettings::setShowContactsInDrawer, st::menuIconUserShow);
	AddSettingToggle(container, tr::lng_menu_calls(), &AyuSettings::showCallsInDrawer, &AyuSettings::setShowCallsInDrawer, st::menuIconPhone);
	AddSettingToggle(container, tr::lng_saved_messages(), &AyuSettings::showSavedMessagesInDrawer, &AyuSettings::setShowSavedMessagesInDrawer, st::menuIconSavedMessages);
	AddSettingToggle(container, tr::ayu_LReadMessages(), &AyuSettings::showLReadToggleInDrawer, &AyuSettings::setShowLReadToggleInDrawer, st::ayuLReadMenuIcon);
	AddSettingToggle(container, tr::ayu_SReadMessages(), &AyuSettings::showSReadToggleInDrawer, &AyuSettings::setShowSReadToggleInDrawer, st::ayuSReadMenuIcon);
	AddSettingToggle(container, tr::lng_menu_night_mode(), &AyuSettings::showNightModeToggleInDrawer, &AyuSettings::setShowNightModeToggleInDrawer, st::menuIconNightMode);
	AddSettingToggle(container, tr::ayu_GhostModeToggle(), &AyuSettings::showGhostToggleInDrawer, &AyuSettings::setShowGhostToggleInDrawer, st::ayuGhostIcon);

#ifdef WIN32
	AddSettingToggle(container, tr::ayu_StreamerModeToggle(), &AyuSettings::showStreamerToggleInDrawer, &AyuSettings::setShowStreamerToggleInDrawer, st::ayuStreamerModeMenuIcon);
#endif

	AddSkip(container);
}

void SetupTrayElements(not_null<Ui::VerticalLayout*> container) {
	AddSubsectionTitle(container, tr::ayu_TrayElementsHeader());

	AddSettingToggle(container, tr::ayu_EnableGhostModeTray(), &AyuSettings::showGhostToggleInTray, &AyuSettings::setShowGhostToggleInTray);

#ifdef WIN32
	AddSettingToggle(container, tr::ayu_EnableStreamerModeTray(), &AyuSettings::showStreamerToggleInTray, &AyuSettings::setShowStreamerToggleInTray);
#endif

	AddSectionDivider(container);
}

void AyuAppearance::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	AddSkip(content);

	SetupAppIcon(content);
	SetupAppearance(content, controller);
	SetupChatFolders(content);
	SetupTrayElements(content);
	SetupDrawerElements(content, controller);
	AddSkip(content);

	ResizeFitChild(this, content);
}

} // namespace Settings