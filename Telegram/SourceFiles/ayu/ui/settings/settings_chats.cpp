// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "settings_chats.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/boxes/edit_mark_box.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "core/application.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_icons.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace AyBuilder;

namespace {

void BuildStickersAndEmoji(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/stickersEmoji"_q,
		.title = tr::lng_settings_stickers_emoji(),
		.keywords = { u"stickers"_q, u"emoji"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/showOnlyAddedEmojis"_q,
		.title = tr::ayu_ShowOnlyAddedEmojisAndStickers(),
		.getter = &AyuSettings::showOnlyAddedEmojisAndStickers,
		.setter = &AyuSettings::setShowOnlyAddedEmojisAndStickers,
		.keywords = { u"emoji"_q, u"stickers"_q, u"added"_q },
	});

	ayu.addCollapsibleToggle({
		.id = u"ayu/hideReactions"_q,
		.title = tr::ayu_HideReactions(),
		.checkboxes = {
			NestedEntry{
				tr::ayu_HideReactionsInChannels(tr::now),
				[] { return !AyuSettings::getInstance().showChannelReactions(); },
				[](bool v) { AyuSettings::getInstance().setShowChannelReactions(!v); }
			},
			NestedEntry{
				tr::ayu_HideReactionsInGroups(tr::now),
				[] { return !AyuSettings::getInstance().showGroupReactions(); },
				[](bool v) { AyuSettings::getInstance().setShowGroupReactions(!v); }
			}
		},
		.toggledWhenAll = false,
		.keywords = { u"reactions"_q, u"hide"_q },
	});

	ayu.addSectionDivider();
}

void BuildRecentStickersLimit(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	ayu.addSlider({
		.id = u"ayu/recentStickersCount"_q,
		.title = tr::ayu_SettingsRecentStickersCount(),
		.steps = 200 + 1,
		.current = settings->recentStickersCount(),
		.indexToValue = [](int index) { return index; },
		.onChanged = nullptr,
		.onFinalChanged = [](int amount) {
			AyuSettings::getInstance().setRecentStickersCount(amount);
		},
		.formatLabel = [](int amount) { return QString::number(amount); },
		.keywords = { u"stickers"_q, u"recent"_q, u"limit"_q },
	});

	ayu.addSectionDivider();
}

void BuildGroupsAndChannels(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	builder.addSubsectionTitle({
		.id = u"ayu/groupsChannels"_q,
		.title = tr::lng_premium_double_limits_subtitle_channels(),
		.keywords = { u"groups"_q, u"channels"_q },
	});

	ayu.addChooseButton({
		.id = u"ayu/bottomButton"_q,
		.title = tr::ayu_ChannelBottomButton(),
		.boxTitle = tr::ayu_ChannelBottomButton(),
		.initialSelection = static_cast<int>(settings->channelBottomButton()),
		.options = {
			tr::ayu_ChannelBottomButtonHide(tr::now),
			tr::ayu_ChannelBottomButtonMute(tr::now),
			tr::ayu_ChannelBottomButtonDiscuss(tr::now),
		},
		.setter = [](int index) {
			AyuSettings::getInstance().setChannelBottomButton(
				static_cast<ChannelBottomButton>(index));
		},
		.keywords = { u"channel"_q, u"bottom"_q, u"button"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/adminShortcuts"_q,
		.title = tr::ayu_QuickAdminShortcuts(),
		.getter = &AyuSettings::quickAdminShortcuts,
		.setter = &AyuSettings::setQuickAdminShortcuts,
		.keywords = { u"admin"_q, u"shortcuts"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showMessageShot"_q,
		.title = tr::ayu_SettingsShowMessageShot(),
		.getter = &AyuSettings::showMessageShot,
		.setter = &AyuSettings::setShowMessageShot,
		.keywords = { u"screenshot"_q, u"shot"_q },
	});

	builder.addSkip();
	builder.addDividerText(tr::ayu_SettingsShowMessageShotDescription());
	builder.addSkip();
}

void BuildMarks(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	builder.addSubsectionTitle({
		.id = u"ayu/messages"_q,
		.title = tr::lng_settings_messages(),
		.keywords = { u"messages"_q, u"marks"_q },
	});

	builder.addButton({
		.id = u"ayu/deletedMark"_q,
		.title = tr::ayu_DeletedMarkText(),
		.st = &st::settingsButtonNoIcon,
		.label = AyuSettings::getInstance().deletedMarkChanges(),
		.onClick = [=] {
			auto box = Box<EditMarkBox>(
				tr::ayu_DeletedMarkText(),
				settings->deletedMark(),
				QString("🧹"),
				[=](const QString &value) {
					AyuSettings::getInstance().setDeletedMark(value);
				});
			Ui::show(std::move(box));
		},
		.keywords = { u"deleted"_q, u"mark"_q },
	});

	builder.addButton({
		.id = u"ayu/editedMark"_q,
		.title = tr::ayu_EditedMarkText(),
		.st = &st::settingsButtonNoIcon,
		.label = AyuSettings::getInstance().editedMarkChanges(),
		.onClick = [=] {
			auto box = Box<EditMarkBox>(
				tr::ayu_EditedMarkText(),
				settings->editedMark(),
				tr::lng_edited(tr::now),
				[=](const QString &value) {
					AyuSettings::getInstance().setEditedMark(value);
				});
			Ui::show(std::move(box));
		},
		.keywords = { u"edited"_q, u"mark"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/replaceEditedWithIcon"_q,
		.title = tr::ayu_ReplaceMarksWithIcons(),
		.getter = &AyuSettings::replaceBottomInfoWithIcons,
		.setter = &AyuSettings::setReplaceBottomInfoWithIcons,
		.keywords = { u"icons"_q, u"marks"_q },
	});

	ayu.addSectionDivider();

	ayu.addSettingToggle({
		.id = u"ayu/hideShareButton"_q,
		.title = tr::ayu_HideShareButton(),
		.getter = &AyuSettings::hideFastShare,
		.setter = &AyuSettings::setHideFastShare,
		.keywords = { u"share"_q, u"forward"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/disableColorfulReplies"_q,
		.title = tr::ayu_SimpleQuotesAndReplies(),
		.getter = &AyuSettings::simpleQuotesAndReplies,
		.setter = &AyuSettings::setSimpleQuotesAndReplies,
		.keywords = { u"quotes"_q, u"replies"_q, u"simple"_q },
	});

	ayu.addSectionDivider();
}

void BuildWideMessagesMultiplier(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	constexpr auto kSizeAmount = 61; // (4.00 - 1.00) / 0.05 + 1
	constexpr auto kMinSize = 1.00;
	constexpr auto kStep = 0.05;

	const auto valueToIndex = [=](double value) {
		return static_cast<int>(std::round((value - kMinSize) / kStep));
	};

	const auto controller = builder.controller();
	ayu.addSlider({
		.id = u"ayu/wideMultiplier"_q,
		.title = tr::ayu_SettingsWideMultiplier(),
		.steps = kSizeAmount,
		.current = valueToIndex(settings->wideMultiplier()),
		.indexToValue = [](int index) { return index; },
		.onChanged = nullptr,
		.onFinalChanged = [=](int index) {
			AyuSettings::getInstance().setWideMultiplier(
				kMinSize + index * kStep);
			crl::on_main([=] {
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_settings_need_restart(),
					.confirmed = [] { Core::Restart(); },
					.confirmText = tr::lng_settings_restart_now(),
					.cancelText = tr::lng_settings_restart_later(),
				}));
			});
		},
		.formatLabel = [=](int index) {
			return QString::number(kMinSize + index * kStep, 'f', 2);
		},
		.keywords = { u"wide"_q, u"multiplier"_q, u"width"_q },
	});

	builder.addSkip();
	builder.addDividerText(tr::ayu_SettingsWideMultiplierDescription());
	builder.addSkip();
}

void BuildContextMenuElements(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	builder.addSubsectionTitle({
		.id = u"ayu/contextMenu"_q,
		.title = tr::ayu_ContextMenuElementsHeader(),
		.keywords = { u"context"_q, u"menu"_q },
	});

	const auto options = std::vector{
		tr::ayu_SettingsContextMenuItemHidden(tr::now),
		tr::ayu_SettingsContextMenuItemShown(tr::now),
		tr::ayu_SettingsContextMenuItemExtended(tr::now),
	};

	ayu.addChooseButton({
		.id = u"ayu/contextReactionsPanel"_q,
		.title = tr::ayu_SettingsContextMenuReactionsPanel(),
		.boxTitle = tr::ayu_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showReactionsPanelInContextMenu()),
		.options = options,
		.setter = [](int i) { AyuSettings::getInstance().setShowReactionsPanelInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconReactions },
		.keywords = { u"reactions"_q, u"panel"_q },
	});
	ayu.addChooseButton({
		.id = u"ayu/contextViewsPanel"_q,
		.title = tr::ayu_SettingsContextMenuViewsPanel(),
		.boxTitle = tr::ayu_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showViewsPanelInContextMenu()),
		.options = options,
		.setter = [](int i) { AyuSettings::getInstance().setShowViewsPanelInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconShowInChat },
		.keywords = { u"views"_q, u"panel"_q },
	});
	ayu.addChooseButton({
		.id = u"ayu/contextHideMessage"_q,
		.title = tr::ayu_ContextHideMessage(),
		.boxTitle = tr::ayu_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showHideMessageInContextMenu()),
		.options = options,
		.setter = [](int i) { AyuSettings::getInstance().setShowHideMessageInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconClear },
		.keywords = { u"hide"_q, u"message"_q },
	});
	ayu.addChooseButton({
		.id = u"ayu/contextUserMessages"_q,
		.title = tr::ayu_UserMessagesMenuText(),
		.boxTitle = tr::ayu_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showUserMessagesInContextMenu()),
		.options = options,
		.setter = [](int i) { AyuSettings::getInstance().setShowUserMessagesInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconTTL },
		.keywords = { u"user"_q, u"messages"_q },
	});
	ayu.addChooseButton({
		.id = u"ayu/contextMessageDetails"_q,
		.title = tr::ayu_MessageDetailsPC(),
		.boxTitle = tr::ayu_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showMessageDetailsInContextMenu()),
		.options = options,
		.setter = [](int i) { AyuSettings::getInstance().setShowMessageDetailsInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconInfo },
		.keywords = { u"details"_q, u"info"_q },
	});
	ayu.addChooseButton({
		.id = u"ayu/contextRepeatMessage"_q,
		.title = tr::ayu_RepeatMessage(),
		.boxTitle = tr::ayu_SettingsContextMenuTitle(),
		.initialSelection = static_cast<int>(settings->showRepeatMessageInContextMenu()),
		.options = options,
		.setter = [](int i) { AyuSettings::getInstance().setShowRepeatMessageInContextMenu(static_cast<ContextMenuVisibility>(i)); },
		.icon = { &st::menuIconRestore },
		.keywords = { u"repeat"_q, u"resend"_q },
	});
	if (settings->filtersEnabled()) {
		ayu.addChooseButton({
			.id = u"ayu/contextAddFilter"_q,
			.title = tr::ayu_RegexFilterQuickAdd(),
			.boxTitle = tr::ayu_SettingsContextMenuTitle(),
			.initialSelection = static_cast<int>(settings->showAddFilterInContextMenu()),
			.options = options,
			.setter = [](int i) { AyuSettings::getInstance().setShowAddFilterInContextMenu(static_cast<ContextMenuVisibility>(i)); },
			.icon = { &st::menuIconAddToFolder },
			.keywords = { u"filter"_q, u"add"_q },
		});
	}

	builder.addSkip();
	builder.addDividerText(tr::ayu_SettingsContextMenuDescription());
	builder.addSkip();
}

void BuildMessageFieldElements(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/messageField"_q,
		.title = tr::ayu_MessageFieldElementsHeader(),
		.keywords = { u"message"_q, u"field"_q, u"compose"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/showAttachButton"_q,
		.title = tr::ayu_MessageFieldElementAttach(),
		.getter = &AyuSettings::showAttachButtonInMessageField,
		.setter = &AyuSettings::setShowAttachButtonInMessageField,
		.icon = { &st::messageFieldAttachIcon },
		.keywords = { u"attach"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showCommandsButton"_q,
		.title = tr::ayu_MessageFieldElementCommands(),
		.getter = &AyuSettings::showCommandsButtonInMessageField,
		.setter = &AyuSettings::setShowCommandsButtonInMessageField,
		.icon = { &st::messageFieldCommandsIcon },
		.keywords = { u"commands"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showAutoDeleteButton"_q,
		.title = tr::ayu_MessageFieldElementTTL(),
		.getter = &AyuSettings::showAutoDeleteButtonInMessageField,
		.setter = &AyuSettings::setShowAutoDeleteButtonInMessageField,
		.icon = { &st::messageFieldTTLIcon },
		.keywords = { u"auto delete"_q, u"ttl"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showEmojiButton"_q,
		.title = tr::ayu_MessageFieldElementEmoji(),
		.getter = &AyuSettings::showEmojiButtonInMessageField,
		.setter = &AyuSettings::setShowEmojiButtonInMessageField,
		.icon = { &st::messageFieldEmojiIcon },
		.keywords = { u"emoji"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showMicrophoneButton"_q,
		.title = tr::ayu_MessageFieldElementVoice(),
		.getter = &AyuSettings::showMicrophoneButtonInMessageField,
		.setter = &AyuSettings::setShowMicrophoneButtonInMessageField,
		.icon = { &st::messageFieldVoiceIcon },
		.keywords = { u"voice"_q, u"microphone"_q },
	});

	ayu.addSectionDivider();
}

void BuildMessageFieldPopups(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle({
		.id = u"ayu/messageFieldPopups"_q,
		.title = tr::ayu_MessageFieldPopupsHeader(),
		.keywords = { u"popup"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/showAttachPopup"_q,
		.title = tr::ayu_MessageFieldElementAttach(),
		.getter = &AyuSettings::showAttachPopup,
		.setter = &AyuSettings::setShowAttachPopup,
		.icon = { &st::messageFieldAttachIcon },
		.keywords = { u"attach"_q, u"popup"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/showEmojiPopup"_q,
		.title = tr::ayu_MessageFieldElementEmoji(),
		.getter = &AyuSettings::showEmojiPopup,
		.setter = &AyuSettings::setShowEmojiPopup,
		.icon = { &st::messageFieldEmojiIcon },
		.keywords = { u"emoji"_q, u"popup"_q },
	});
}

const auto kMeta = BuildHelper({
	.id = AyuChats::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryChats,
	.icon = &st::menuIconChatBubble,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSkip();
	BuildStickersAndEmoji(builder, ayu);
	BuildRecentStickersLimit(builder, ayu);
	BuildGroupsAndChannels(builder, ayu);
	BuildMarks(builder, ayu);
	BuildWideMessagesMultiplier(builder, ayu);
	BuildContextMenuElements(builder, ayu);
	BuildMessageFieldElements(builder, ayu);
	BuildMessageFieldPopups(builder, ayu);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> AyuChats::title() {
	return tr::ayu_CategoryChats();
}

AyuChats::AyuChats(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuChats::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuChatsId() {
	return AyuChats::Id();
}

} // namespace Settings
