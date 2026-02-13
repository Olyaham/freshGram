// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2025
#include "settings_general.h"

#include "lang_auto.h"
#include "settings_ayu_utils.h"
#include "ayu/ayu_settings.h"
#include "core/application.h"
#include "settings/settings_common.h"
#include "styles/style_settings.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

namespace Settings {

rpl::producer<QString> AyuGeneral::title() {
	return tr::ayu_CategoryGeneral();
}

AyuGeneral::AyuGeneral(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
	: Section(parent) {
	setupContent(controller);
}

void SetupTranslator(not_null<Ui::VerticalLayout*> container,
					 not_null<Window::SessionController*> controller) {
	auto *settings = &AyuSettings::getInstance();

	AddSubsectionTitle(container, tr::lng_translate_settings_subtitle()/*rpl::single(QString("Translate Messages"))*/);

	const auto options = std::vector{
		QString("Telegram"),
		QString("Google"),
		QString("Yandex")
	};

	const auto getIndex = [=](const QString &val)
	{
		if (val == "telegram") {
			return 0;
		}
		if (val == "google") {
			return 1;
		}
		if (val == "yandex") {
			return 2;
		}
		return 0;
	};

	auto currentVal = AyuSettings::getInstance().translationProviderChanges() | rpl::map(getIndex) | rpl::map(
		[=](int val)
		{
			return options[val];
		});

	const auto button = AddButtonWithLabel(
		container,
		asBeta(tr::ayu_TranslationProvider()),
		currentVal,
		st::settingsButtonNoIcon);
	button->addClickHandler(
		[=]
		{
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box)
				{
					const auto save = [=](int index)
					{
						const auto provider = (index == 0) ? "telegram" : (index == 1) ? "google" : "yandex";

						AyuSettings::getInstance().setTranslationProvider(provider);
					};
					SingleChoiceBox(box,
									{
										.title = tr::ayu_TranslationProvider(),
										.options = options,
										.initialSelection = getIndex(settings->translationProvider()),
										.callback = save,
									});
				}));
		});
}

void SetupShowPeerId(not_null<Ui::VerticalLayout*> container,
					 not_null<Window::SessionController*> controller) {
	auto *settings = &AyuSettings::getInstance();

	const auto options = std::vector{
		QString(tr::ayu_SettingsShowID_Hide(tr::now)),
		QString("Telegram API"),
		QString("Bot API")
	};

	auto currentVal = AyuSettings::getInstance().showPeerIdChanges() | rpl::map(
		[=](PeerIdDisplay val)
		{
			return options[static_cast<int>(val)];
		});

	const auto button = AddButtonWithLabel(
		container,
		tr::ayu_SettingsShowID(),
		currentVal,
		st::settingsButtonNoIcon);
	button->addClickHandler(
		[=]
		{
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box)
				{
					const auto save = [=](int index)
					{
						AyuSettings::getInstance().setShowPeerId(static_cast<PeerIdDisplay>(index));
					};
					SingleChoiceBox(box,
									{
										.title = tr::ayu_SettingsShowID(),
										.options = options,
										.initialSelection = static_cast<int>(settings->showPeerId()),
										.callback = save,
									});
				}));
		});
}

void SetupQoLToggles(not_null<Ui::VerticalLayout*> container, not_null<Window::SessionController*> controller) {
	auto *settings = &AyuSettings::getInstance();

	SetupTranslator(container, controller);
	AddSectionDivider(container);

	AddSubsectionTitle(container, tr::ayu_CategoryGeneral());

	AddSettingToggle(container, tr::ayu_DisableStories(), &AyuSettings::disableStories, &AyuSettings::setDisableStories);

	std::vector checkboxes = {
		NestedEntry{
			tr::ayu_CollapseSimilarChannels(tr::now),
			[] { return AyuSettings::getInstance().collapseSimilarChannels(); },
			[](bool v) { AyuSettings::getInstance().setCollapseSimilarChannels(v); }
		},
		NestedEntry{
			tr::ayu_HideSimilarChannelsTab(tr::now),
			[] { return AyuSettings::getInstance().hideSimilarChannels(); },
			[](bool v) { AyuSettings::getInstance().setHideSimilarChannels(v); }
		}
	};

	AddCollapsibleToggle(container, tr::ayu_DisableSimilarChannels(), checkboxes, true);


	AddSettingToggle(container, tr::ayu_DisableNotificationsDelay(), &AyuSettings::disableNotificationsDelay, &AyuSettings::setDisableNotificationsDelay);

	AddSectionDivider(container);

	AddButtonWithIcon(
		container,
		tr::ayu_FilterZalgo(),
		st::settingsButtonNoIcon
	)->toggleOn(
		rpl::single(settings->filterZalgo())
	)->toggledValue(
	) | rpl::filter(
		[=](bool enabled)
		{
			return (enabled != settings->filterZalgo());
		}) | on_next(
		[=](bool enabled)
		{
			AyuSettings::getInstance().setFilterZalgo(enabled);

			// restart because hooks for filter are in Peer::setName which can be updated only on restart
			// same for HistoryItem::setText
			controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_settings_need_restart(),
				.confirmed = []
				{
					Core::Restart();
				},
				.confirmText = tr::lng_settings_restart_now(),
				.cancelText = tr::lng_settings_restart_later(),
			}));
		},
		container->lifetime());

	AddSettingToggle(container, tr::ayu_ImproveLinkPreviews(), &AyuSettings::improveLinkPreviews, &AyuSettings::setImproveLinkPreviews);
	AddSettingToggle(container, tr::ayu_SettingsShowMessageSeconds(), &AyuSettings::showMessageSeconds, &AyuSettings::setShowMessageSeconds);

	SetupShowPeerId(container, controller);

	AddSectionDivider(container);

	AddSubsectionTitle(container, rpl::single(QString("Webview")));

	AddSettingToggle(container, tr::ayu_SettingsSpoofWebviewAsAndroid(), &AyuSettings::spoofWebviewAsAndroid, &AyuSettings::setSpoofWebviewAsAndroid);

	std::vector webviewCheckboxes = {
		NestedEntry{
			tr::ayu_SettingsIncreaseWebviewHeight(tr::now),
			[] { return AyuSettings::getInstance().increaseWebviewHeight(); },
			[](bool v) { AyuSettings::getInstance().setIncreaseWebviewHeight(v); }
		},
		NestedEntry{
			tr::ayu_SettingsIncreaseWebviewWidth(tr::now),
			[] { return AyuSettings::getInstance().increaseWebviewWidth(); },
			[](bool v) { AyuSettings::getInstance().setIncreaseWebviewWidth(v); }
		}
	};

	AddCollapsibleToggle(container, tr::ayu_SettingsBiggerWindow(), webviewCheckboxes, false);

	AddSectionDivider(container);

	// todo: move into a single checkbox with dropdown
	AddSubsectionTitle(container, tr::ayu_ConfirmationsTitle());

	AddSettingToggle(container, tr::ayu_StickerConfirmation(), &AyuSettings::stickerConfirmation, &AyuSettings::setStickerConfirmation);
	AddSettingToggle(container, tr::ayu_GIFConfirmation(), &AyuSettings::gifConfirmation, &AyuSettings::setGifConfirmation);
	AddSettingToggle(container, tr::ayu_VoiceConfirmation(), &AyuSettings::voiceConfirmation, &AyuSettings::setVoiceConfirmation);
}

void AyuGeneral::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	AddSkip(content);
	SetupQoLToggles(content, controller);
	AddSkip(content);

	ResizeFitChild(this, content);
}

} // namespace Settings