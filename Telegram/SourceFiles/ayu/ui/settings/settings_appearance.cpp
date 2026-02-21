// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "settings_appearance.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/boxes/font_selector.h"
#include "ayu/ui/components/icon_picker.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_main.h"
#include "inline_bots/bot_attach_web_view.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace AyBuilder;

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

void BuildAppIcon(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/appIcon"_q,
		.title = tr::ayu_AppIconHeader(),
		.keywords = { u"icon"_q, u"app"_q },
	});

	builder.add([](const WidgetContext &ctx) -> SectionBuilder::WidgetToAdd {
		return {
			.widget = object_ptr<IconPicker>(ctx.container),
			.margin = st::settingsCheckboxPadding,
		};
	}, [] {
		return SearchEntry{
			.id = u"ayu/iconPicker"_q,
			.title = tr::ayu_AppIconHeader(tr::now),
			.keywords = { u"icon"_q, u"app"_q, u"logo"_q },
		};
	});

#ifdef Q_OS_WIN
	builder.addDivider();
	builder.addSkip();
	ayu.addSettingToggle({
		.id = u"ayu/hideNotificationBadge"_q,
		.title = tr::ayu_HideNotificationBadge(),
		.getter = &AyuSettings::hideNotificationBadge,
		.setter = &AyuSettings::setHideNotificationBadge,
		.keywords = { u"notification"_q, u"badge"_q },
	});
	builder.addSkip();
	builder.addDividerText(tr::ayu_HideNotificationBadgeDescription());
	builder.addSkip();
#endif
}

void BuildAppearance(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	builder.addSubsectionTitle({
		.id = u"ayu/appearanceSection"_q,
		.title = tr::ayu_CategoryAppearance(),
	});

	ayu.addSettingToggle({
		.id = u"ayu/newSwitchStyle"_q,
		.title = tr::ayu_MaterialSwitches(),
		.getter = &AyuSettings::materialSwitches,
		.setter = &AyuSettings::setMaterialSwitches,
		.keywords = { u"material"_q, u"switch"_q, u"toggle"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/removeMessageTail"_q,
		.title = tr::ayu_RemoveMessageTail(),
		.getter = &AyuSettings::removeMessageTail,
		.setter = &AyuSettings::setRemoveMessageTail,
		.keywords = { u"tail"_q, u"bubble"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/disableCustomBackgrounds"_q,
		.title = tr::ayu_DisableCustomBackgrounds(),
		.getter = &AyuSettings::disableCustomBackgrounds,
		.setter = &AyuSettings::setDisableCustomBackgrounds,
		.keywords = { u"background"_q, u"wallpaper"_q },
	});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"ayu/monoFont"_q,
		.title = tr::ayu_MonospaceFont(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(
			settings->monoFont().isEmpty()
				? tr::ayu_FontDefault(tr::now)
				: settings->monoFont()),
		.onClick = [=] {
			AyuUi::FontSelectorBox::Show(
				controller,
				[=](const QString &font) {
					AyuSettings::getInstance().setMonoFont(font);
				});
		},
		.keywords = { u"font"_q, u"monospace"_q },
	});

	ayu.addSectionDivider();
}

void BuildChatFolders(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/chatFolders"_q,
		.title = tr::ayu_ChatFoldersHeader(),
		.keywords = { u"folders"_q, u"tabs"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/hideNotificationCounters"_q,
		.title = tr::ayu_HideNotificationCounters(),
		.getter = &AyuSettings::hideNotificationCounters,
		.setter = &AyuSettings::setHideNotificationCounters,
		.keywords = { u"notification"_q, u"counter"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/hideAllChats"_q,
		.title = tr::ayu_HideAllChats(),
		.getter = &AyuSettings::hideAllChatsFolder,
		.setter = &AyuSettings::setHideAllChatsFolder,
		.keywords = { u"all chats"_q, u"folder"_q },
	});

	ayu.addSectionDivider();
}

void BuildTrayElements(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/trayElements"_q,
		.title = tr::ayu_TrayElementsHeader(),
		.keywords = { u"tray"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/ghostToggleInTray"_q,
		.title = tr::ayu_EnableGhostModeTray(),
		.getter = &AyuSettings::showGhostToggleInTray,
		.setter = &AyuSettings::setShowGhostToggleInTray,
		.keywords = { u"ghost"_q, u"tray"_q },
	});

#ifdef WIN32
	ayu.addSettingToggle({
		.id = u"ayu/streamerToggleInTray"_q,
		.title = tr::ayu_EnableStreamerModeTray(),
		.getter = &AyuSettings::showStreamerToggleInTray,
		.setter = &AyuSettings::setShowStreamerToggleInTray,
		.keywords = { u"streamer"_q, u"tray"_q },
	});
#endif

	ayu.addSectionDivider();
}

void BuildDrawerElements(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/drawerElements"_q,
		.title = tr::ayu_DrawerElementsHeader(),
		.keywords = { u"drawer"_q, u"menu"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/myProfileInDrawer"_q,
		.title = tr::lng_menu_my_profile(),
		.getter = &AyuSettings::showMyProfileInDrawer,
		.setter = &AyuSettings::setShowMyProfileInDrawer,
		.icon = { &st::menuIconProfile },
	});

	const auto controller = builder.controller();
	if (controller && HasDrawerBots(controller)) {
		ayu.addSettingToggle({
			.id = u"ayu/botsInDrawer"_q,
			.title = tr::lng_filters_type_bots(),
			.getter = &AyuSettings::showBotsInDrawer,
			.setter = &AyuSettings::setShowBotsInDrawer,
			.icon = { &st::menuIconBot },
		});
	}

	ayu.addSettingToggle({
		.id = u"ayu/newGroupInDrawer"_q,
		.title = tr::lng_create_group_title(),
		.getter = &AyuSettings::showNewGroupInDrawer,
		.setter = &AyuSettings::setShowNewGroupInDrawer,
		.icon = { &st::menuIconGroups },
	});
	ayu.addSettingToggle({
		.id = u"ayu/newChannelInDrawer"_q,
		.title = tr::lng_create_channel_title(),
		.getter = &AyuSettings::showNewChannelInDrawer,
		.setter = &AyuSettings::setShowNewChannelInDrawer,
		.icon = { &st::menuIconChannel },
	});
	ayu.addSettingToggle({
		.id = u"ayu/contactsInDrawer"_q,
		.title = tr::lng_menu_contacts(),
		.getter = &AyuSettings::showContactsInDrawer,
		.setter = &AyuSettings::setShowContactsInDrawer,
		.icon = { &st::menuIconUserShow },
	});
	ayu.addSettingToggle({
		.id = u"ayu/callsInDrawer"_q,
		.title = tr::lng_menu_calls(),
		.getter = &AyuSettings::showCallsInDrawer,
		.setter = &AyuSettings::setShowCallsInDrawer,
		.icon = { &st::menuIconPhone },
	});
	ayu.addSettingToggle({
		.id = u"ayu/savedMessagesInDrawer"_q,
		.title = tr::lng_saved_messages(),
		.getter = &AyuSettings::showSavedMessagesInDrawer,
		.setter = &AyuSettings::setShowSavedMessagesInDrawer,
		.icon = { &st::menuIconSavedMessages },
	});
	ayu.addSettingToggle({
		.id = u"ayu/lreadToggleInDrawer"_q,
		.title = tr::ayu_LReadMessages(),
		.getter = &AyuSettings::showLReadToggleInDrawer,
		.setter = &AyuSettings::setShowLReadToggleInDrawer,
		.icon = { &st::ayuLReadMenuIcon },
	});
	ayu.addSettingToggle({
		.id = u"ayu/sreadToggleInDrawer"_q,
		.title = tr::ayu_SReadMessages(),
		.getter = &AyuSettings::showSReadToggleInDrawer,
		.setter = &AyuSettings::setShowSReadToggleInDrawer,
		.icon = { &st::ayuSReadMenuIcon },
	});
	ayu.addSettingToggle({
		.id = u"ayu/nightModeToggleInDrawer"_q,
		.title = tr::lng_menu_night_mode(),
		.getter = &AyuSettings::showNightModeToggleInDrawer,
		.setter = &AyuSettings::setShowNightModeToggleInDrawer,
		.icon = { &st::menuIconNightMode },
	});
	ayu.addSettingToggle({
		.id = u"ayu/ghostToggleInDrawer"_q,
		.title = tr::ayu_GhostModeToggle(),
		.getter = &AyuSettings::showGhostToggleInDrawer,
		.setter = &AyuSettings::setShowGhostToggleInDrawer,
		.icon = { &st::ayuGhostIcon },
	});

#ifdef WIN32
	ayu.addSettingToggle({
		.id = u"ayu/streamerToggleInDrawer"_q,
		.title = tr::ayu_StreamerModeToggle(),
		.getter = &AyuSettings::showStreamerToggleInDrawer,
		.setter = &AyuSettings::setShowStreamerToggleInDrawer,
		.icon = { &st::ayuStreamerModeMenuIcon },
	});
#endif

	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuAppearance::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryAppearance,
	.icon = &st::menuIconPalette,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSkip();
	BuildAppIcon(builder, ayu);
	BuildAppearance(builder, ayu);
	BuildChatFolders(builder, ayu);
	BuildTrayElements(builder, ayu);
	BuildDrawerElements(builder, ayu);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> AyuAppearance::title() {
	return tr::ayu_CategoryAppearance();
}

AyuAppearance::AyuAppearance(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuAppearanceId() {
	return AyuAppearance::Id();
}

} // namespace Settings
