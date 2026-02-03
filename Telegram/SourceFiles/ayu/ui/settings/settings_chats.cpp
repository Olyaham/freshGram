// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2025
#include "settings_chats.h"

#include <styles/style_ayu_icons.h>

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/boxes/edit_mark_box.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "core/application.h"
#include "settings/settings_common.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

rpl::producer<QString> AyuChats::title() {
	return tr::ayu_CategoryChats();
}

AyuChats::AyuChats(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
	: Section(parent) {
	setupContent(controller);
}

void SetupStickersAndEmojiSettings(not_null<Ui::VerticalLayout*> container) {
	auto *settings = &AyuSettings::getInstance();

	AddSubsectionTitle(container, tr::lng_settings_stickers_emoji()/*rpl::single(QString("Stickers and Emoji"))*/);

	AddSettingToggle(container, tr::ayu_ShowOnlyAddedEmojisAndStickers(), &AyuSettings::showOnlyAddedEmojisAndStickers, &AyuSettings::setShowOnlyAddedEmojisAndStickers);

	std::vector checkboxes = {
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
	};

	AddCollapsibleToggle(container, tr::ayu_HideReactions(), checkboxes, false);

	AddSectionDivider(container);
}

void SetupRecentStickersLimit(not_null<Ui::VerticalLayout*> container) {
	auto *settings = &AyuSettings::getInstance();

	container->add(
		object_ptr<Button>(container,
						   tr::ayu_SettingsRecentStickersCount(),
						   st::settingsButtonNoIcon)
	)->setAttribute(Qt::WA_TransparentForMouseEvents);

	auto recentStickersLimitSlider = MakeSliderWithLabel(
		container,
		st::autoDownloadLimitSlider,
		st::settingsScaleLabel,
		0,
		st::settingsScaleLabel.style.font->width("8%%"));
	container->add(std::move(recentStickersLimitSlider.widget), st::recentStickersLimitPadding);
	const auto slider = recentStickersLimitSlider.slider;
	const auto label = recentStickersLimitSlider.label;

	const auto updateLabel = [=](int amount)
	{
		label->setText(QString::number(amount));
	};
	updateLabel(settings->recentStickersCount());

	slider->setPseudoDiscrete(
		200 + 1,
		[=](int amount)
		{
			return amount;
		},
		settings->recentStickersCount(),
		[=](int amount)
		{
			updateLabel(amount);
		},
		[=](int amount)
		{
			updateLabel(amount);
			AyuSettings::getInstance().setRecentStickersCount(amount);
		});

	AddSectionDivider(container);
}

void SetupGroupsAndChannels(not_null<Ui::VerticalLayout*> container, not_null<Window::SessionController*> controller) {
	auto *settings = &AyuSettings::getInstance();

	AddSubsectionTitle(container,
					   tr::lng_premium_double_limits_subtitle_channels()
					   /*rpl::single(QString("Groups and Channels"))*/);

	const auto options = std::vector{
		tr::ayu_ChannelBottomButtonHide(tr::now),
		tr::ayu_ChannelBottomButtonMute(tr::now),
		tr::ayu_ChannelBottomButtonDiscuss(tr::now),
	};

	AddChooseButtonWithIconAndRightText(
		container,
		controller,
		static_cast<int>(settings->channelBottomButton()),
		options,
		tr::ayu_ChannelBottomButton(),
		tr::ayu_ChannelBottomButton(),
		[=](int index)
		{
			AyuSettings::getInstance().setChannelBottomButton(static_cast<ChannelBottomButton>(index));
		});

	AddSettingToggle(container, tr::ayu_QuickAdminShortcuts(), &AyuSettings::quickAdminShortcuts, &AyuSettings::setQuickAdminShortcuts);
	AddSettingToggle(container, tr::ayu_SettingsShowMessageShot(), &AyuSettings::showMessageShot, &AyuSettings::setShowMessageShot);

	AddSkip(container);
	AddDividerText(container, tr::ayu_SettingsShowMessageShotDescription());
	AddSkip(container);
}

void SetupMarks(not_null<Ui::VerticalLayout*> container) {
	auto *settings = &AyuSettings::getInstance();

	AddSubsectionTitle(container, tr::lng_settings_messages()/*rpl::single(QString("Messages"))*/);

	AddButtonWithLabel(
		container,
		tr::ayu_DeletedMarkText(),
		AyuSettings::getInstance().deletedMarkChanges(),
		st::settingsButtonNoIcon
	)->addClickHandler(
		[=]()
		{
			auto box = Box<EditMarkBox>(
				tr::ayu_DeletedMarkText(),
				settings->deletedMark(),
				QString("🧹"),
				[=](const QString &value)
				{
					AyuSettings::getInstance().setDeletedMark(value);
				}
			);
			Ui::show(std::move(box));
		});

	AddButtonWithLabel(
		container,
		tr::ayu_EditedMarkText(),
		AyuSettings::getInstance().editedMarkChanges(),
		st::settingsButtonNoIcon
	)->addClickHandler(
		[=]()
		{
			auto box = Box<EditMarkBox>(
				tr::ayu_EditedMarkText(),
				settings->editedMark(),
				tr::lng_edited(tr::now),
				[=](const QString &value)
				{
					AyuSettings::getInstance().setEditedMark(value);
				}
			);
			Ui::show(std::move(box));
		});

	AddSettingToggle(container, tr::ayu_ReplaceMarksWithIcons(), &AyuSettings::replaceBottomInfoWithIcons, &AyuSettings::setReplaceBottomInfoWithIcons);

	AddSectionDivider(container);

	AddSettingToggle(container, tr::ayu_HideShareButton(), &AyuSettings::hideFastShare, &AyuSettings::setHideFastShare);
	AddSettingToggle(container, tr::ayu_SimpleQuotesAndReplies(), &AyuSettings::simpleQuotesAndReplies, &AyuSettings::setSimpleQuotesAndReplies);

	AddSectionDivider(container);
}

void SetupWideMessagesMultiplier(not_null<Ui::VerticalLayout*> container,
								 not_null<Window::SessionController*> controller) {
	auto *settings = &AyuSettings::getInstance();

	container->add(
		object_ptr<Button>(container,
						   tr::ayu_SettingsWideMultiplier(),
						   st::settingsButtonNoIcon)
	)->setAttribute(Qt::WA_TransparentForMouseEvents);

	auto wideMultiplierSlider = MakeSliderWithLabel(
		container,
		st::autoDownloadLimitSlider,
		st::settingsScaleLabel,
		0,
		st::settingsScaleLabel.style.font->width("8%%%"));
	container->add(std::move(wideMultiplierSlider.widget), st::recentStickersLimitPadding);
	const auto slider = wideMultiplierSlider.slider;
	const auto label = wideMultiplierSlider.label;

	const auto updateLabel = [=](double val)
	{
		label->setText(QString::number(val, 'f', 2));
	};

	constexpr auto kSizeAmount = 61; // (4.00 - 1.00) / 0.05 + 1
	constexpr auto kMinSize = 1.00;
	constexpr auto kStep = 0.05;

	const auto valueToIndex = [=](double value)
	{
		return static_cast<int>(std::round((value - kMinSize) / kStep));
	};
	const auto indexToValue = [=](int index)
	{
		return kMinSize + index * kStep;
	};

	updateLabel(settings->wideMultiplier());

	slider->setPseudoDiscrete(
		kSizeAmount,
		[=](int index) { return index; },
		valueToIndex(settings->wideMultiplier()),
		[=](int index)
		{
			updateLabel(indexToValue(index));
		},
		[=](int index)
		{
			updateLabel(indexToValue(index));
			AyuSettings::getInstance().setWideMultiplier(indexToValue(index));

			// fix slider
			crl::on_main([=]
			{
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_settings_need_restart(),
					.confirmed = []
					{
						Core::Restart();
					},
					.confirmText = tr::lng_settings_restart_now(),
					.cancelText = tr::lng_settings_restart_later(),
				}));
			});
		});

	AddSkip(container);
	AddDividerText(container, tr::ayu_SettingsWideMultiplierDescription());
	AddSkip(container);
}

void SetupContextMenuElements(not_null<Ui::VerticalLayout*> container,
							  not_null<Window::SessionController*> controller) {
	auto *settings = &AyuSettings::getInstance();

	AddSubsectionTitle(container, tr::ayu_ContextMenuElementsHeader());

	const auto options = std::vector{
		tr::ayu_SettingsContextMenuItemHidden(tr::now),
		tr::ayu_SettingsContextMenuItemShown(tr::now),
		tr::ayu_SettingsContextMenuItemExtended(tr::now),
	};

	AddChooseButtonWithIconAndRightText(
		container,
		controller,
		static_cast<int>(settings->showReactionsPanelInContextMenu()),
		options,
		tr::ayu_SettingsContextMenuReactionsPanel(),
		tr::ayu_SettingsContextMenuTitle(),
		st::menuIconReactions,
		[=](int index)
		{
			AyuSettings::getInstance().setShowReactionsPanelInContextMenu(static_cast<ContextMenuVisibility>(index));
		});
	AddChooseButtonWithIconAndRightText(
		container,
		controller,
		static_cast<int>(settings->showViewsPanelInContextMenu()),
		options,
		tr::ayu_SettingsContextMenuViewsPanel(),
		tr::ayu_SettingsContextMenuTitle(),
		st::menuIconShowInChat,
		[=](int index)
		{
			AyuSettings::getInstance().setShowViewsPanelInContextMenu(static_cast<ContextMenuVisibility>(index));
		});

	AddChooseButtonWithIconAndRightText(
		container,
		controller,
		static_cast<int>(settings->showHideMessageInContextMenu()),
		options,
		tr::ayu_ContextHideMessage(),
		tr::ayu_SettingsContextMenuTitle(),
		st::menuIconClear,
		[=](int index)
		{
			AyuSettings::getInstance().setShowHideMessageInContextMenu(static_cast<ContextMenuVisibility>(index));
		});
	AddChooseButtonWithIconAndRightText(
		container,
		controller,
		static_cast<int>(settings->showUserMessagesInContextMenu()),
		options,
		tr::ayu_UserMessagesMenuText(),
		tr::ayu_SettingsContextMenuTitle(),
		st::menuIconTTL,
		[=](int index)
		{
			AyuSettings::getInstance().setShowUserMessagesInContextMenu(static_cast<ContextMenuVisibility>(index));
		});
	AddChooseButtonWithIconAndRightText(
		container,
		controller,
		static_cast<int>(settings->showMessageDetailsInContextMenu()),
		options,
		tr::ayu_MessageDetailsPC(),
		tr::ayu_SettingsContextMenuTitle(),
		st::menuIconInfo,
		[=](int index)
		{
			AyuSettings::getInstance().setShowMessageDetailsInContextMenu(static_cast<ContextMenuVisibility>(index));
		});
	AddChooseButtonWithIconAndRightText(
		container,
		controller,
		static_cast<int>(settings->showRepeatMessageInContextMenu()),
		options,
		tr::ayu_RepeatMessage(),
		tr::ayu_SettingsContextMenuTitle(),
		st::menuIconRestore,
		[=](int index)
		{
			AyuSettings::getInstance().setShowRepeatMessageInContextMenu(static_cast<ContextMenuVisibility>(index));
		});
	if (settings->filtersEnabled()) {
		AddChooseButtonWithIconAndRightText(
			container,
			controller,
			static_cast<int>(settings->showAddFilterInContextMenu()),
			options,
			tr::ayu_RegexFilterQuickAdd(),
			tr::ayu_SettingsContextMenuTitle(),
			st::menuIconAddToFolder,
			[=](int index)
			{
				AyuSettings::getInstance().setShowAddFilterInContextMenu(static_cast<ContextMenuVisibility>(index));
			});
	}

	AddSkip(container);
	AddDividerText(container, tr::ayu_SettingsContextMenuDescription());
	AddSkip(container);
}

void SetupMessageFieldElements(not_null<Ui::VerticalLayout*> container) {
	AddSubsectionTitle(container, tr::ayu_MessageFieldElementsHeader());

	AddSettingToggle(container, tr::ayu_MessageFieldElementAttach(), &AyuSettings::showAttachButtonInMessageField, &AyuSettings::setShowAttachButtonInMessageField, st::messageFieldAttachIcon);
	AddSettingToggle(container, tr::ayu_MessageFieldElementCommands(), &AyuSettings::showCommandsButtonInMessageField, &AyuSettings::setShowCommandsButtonInMessageField, st::messageFieldCommandsIcon);
	AddSettingToggle(container, tr::ayu_MessageFieldElementTTL(), &AyuSettings::showAutoDeleteButtonInMessageField, &AyuSettings::setShowAutoDeleteButtonInMessageField, st::messageFieldTTLIcon);
	AddSettingToggle(container, tr::ayu_MessageFieldElementEmoji(), &AyuSettings::showEmojiButtonInMessageField, &AyuSettings::setShowEmojiButtonInMessageField, st::messageFieldEmojiIcon);
	AddSettingToggle(container, tr::ayu_MessageFieldElementVoice(), &AyuSettings::showMicrophoneButtonInMessageField, &AyuSettings::setShowMicrophoneButtonInMessageField, st::messageFieldVoiceIcon);

	AddSectionDivider(container);
}

void SetupMessageFieldPopups(not_null<Ui::VerticalLayout*> container) {
	AddSubsectionTitle(container, tr::ayu_MessageFieldPopupsHeader());

	AddSettingToggle(container, tr::ayu_MessageFieldElementAttach(), &AyuSettings::showAttachPopup, &AyuSettings::setShowAttachPopup, st::messageFieldAttachIcon);
	AddSettingToggle(container, tr::ayu_MessageFieldElementEmoji(), &AyuSettings::showEmojiPopup, &AyuSettings::setShowEmojiPopup, st::messageFieldEmojiIcon);
}

void AyuChats::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	AddSkip(content);
	SetupStickersAndEmojiSettings(content);
	SetupRecentStickersLimit(content);

	SetupGroupsAndChannels(content, controller);

	SetupMarks(content);
	SetupWideMessagesMultiplier(content, controller);

	SetupContextMenuElements(content, controller);
	SetupMessageFieldElements(content);

	SetupMessageFieldPopups(content);
	AddSkip(content);

	ResizeFitChild(this, content);
}

} // namespace Settings
