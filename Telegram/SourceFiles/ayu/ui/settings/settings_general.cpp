// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_general.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "core/application.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace AyBuilder;

namespace {

void BuildTranslator(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle(tr::lng_translate_settings_subtitle());

	auto *settings = &AyuSettings::getInstance();

	const auto options = std::vector{
		QString("Telegram"),
		QString("Google"),
		QString("Yandex")
	};

	const auto getIndex = [=](TranslationProvider val) {
		return static_cast<int>(val);
	};

	auto currentVal = AyuSettings::getInstance().translationProviderValue()
		| rpl::map(getIndex)
		| rpl::map([=](int val) { return options[val]; });

	const auto button = builder.addButton({
		.id = u"ayu/translationProvider"_q,
		.title = tr::ayu_TranslationProvider(),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentVal),
		.onClick = [=] {
			if (const auto controller = Core::App().activeWindow()->sessionController()) {
				controller->show(Box(
					[=](not_null<Ui::GenericBox*> box) {
						const auto save = [=](int index) {
							AyuSettings::getInstance().setTranslationProvider(
								static_cast<TranslationProvider>(index));
						};
						SingleChoiceBox(box, {
							.title = tr::ayu_TranslationProvider(),
							.options = options,
							.initialSelection = getIndex(settings->translationProvider()),
							.callback = save,
						});
					}));
			}
		},
		.keywords = { u"translate"_q, u"translation"_q, u"provider"_q },
	});
	if (button) {
		ayu.addBetaBadge(button);
	}
}

void BuildShowPeerId(SectionBuilder &builder) {
	auto *settings = &AyuSettings::getInstance();

	const auto options = std::vector{
		QString(tr::ayu_SettingsShowID_Hide(tr::now)),
		QString("Telegram API"),
		QString("Bot API")
	};

	auto currentVal = AyuSettings::getInstance().showPeerIdValue()
		| rpl::map([=](PeerIdDisplay val) {
			return options[static_cast<int>(val)];
		});

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"ayu/showIdAndDc"_q,
		.title = tr::ayu_SettingsShowID(),
		.st = &st::settingsButtonNoIcon,
		.label = std::move(currentVal),
		.onClick = [=] {
			controller->show(Box(
				[=](not_null<Ui::GenericBox*> box) {
					const auto save = [=](int index) {
						AyuSettings::getInstance().setShowPeerId(
							static_cast<PeerIdDisplay>(index));
					};
					SingleChoiceBox(box, {
						.title = tr::ayu_SettingsShowID(),
						.options = options,
						.initialSelection = static_cast<int>(settings->showPeerId()),
						.callback = save,
					});
				}));
		},
		.keywords = { u"peer"_q, u"id"_q },
	});
}

void BuildQoLToggles(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	BuildTranslator(builder, ayu);
	ayu.addSectionDivider();

	builder.addSubsectionTitle({
		.id = u"ayu/generalSection"_q,
		.title = tr::ayu_CategoryGeneral(),
	});

	ayu.addSettingToggle({
		.id = u"ayu/hideStories"_q,
		.title = tr::ayu_DisableStories(),
		.getter = &AyuSettings::disableStories,
		.setter = &AyuSettings::setDisableStories,
		.keywords = { u"stories"_q },
	});

	ayu.addCollapsibleToggle({
		.id = u"ayu/similarChannels"_q,
		.title = tr::ayu_DisableSimilarChannels(),
		.checkboxes = {
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
		},
		.toggledWhenAll = true,
		.keywords = { u"similar"_q, u"channels"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/disableNotificationsDelay"_q,
		.title = tr::ayu_DisableNotificationsDelay(),
		.getter = &AyuSettings::disableNotificationsDelay,
		.setter = &AyuSettings::setDisableNotificationsDelay,
		.keywords = { u"notifications"_q, u"delay"_q },
	});

	ayu.addSectionDivider();

	const auto controller = builder.controller();
	const auto zalgoButton = builder.addButton({
		.id = u"ayu/filterZalgo"_q,
		.title = tr::ayu_FilterZalgo(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filterZalgo()),
		.keywords = { u"zalgo"_q, u"filter"_q },
	});
	if (zalgoButton) {
		zalgoButton->toggledValue(
		) | rpl::filter(
			[=](bool enabled) {
				return (enabled != settings->filterZalgo());
			}
		) | on_next(
			[=](bool enabled) {
				AyuSettings::getInstance().setFilterZalgo(enabled);
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_settings_need_restart(),
					.confirmed = [] { Core::Restart(); },
					.confirmText = tr::lng_settings_restart_now(),
					.cancelText = tr::lng_settings_restart_later(),
				}));
			},
			zalgoButton->lifetime());
		ayu.addBetaBadge(zalgoButton);
	}

	ayu.addSettingToggle({
		.id = u"ayu/improveLinkPreviews"_q,
		.title = tr::ayu_ImproveLinkPreviews(),
		.getter = &AyuSettings::improveLinkPreviews,
		.setter = &AyuSettings::setImproveLinkPreviews,
		.keywords = { u"link"_q, u"preview"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/formatTimeWithSeconds"_q,
		.title = tr::ayu_SettingsShowMessageSeconds(),
		.getter = &AyuSettings::showMessageSeconds,
		.setter = &AyuSettings::setShowMessageSeconds,
		.keywords = { u"seconds"_q, u"time"_q },
	});

	BuildShowPeerId(builder);

	ayu.addSectionDivider();

	builder.addSubsectionTitle({
		.id = u"ayu/webviewSection"_q,
		.title = rpl::single(QString("Webview")),
		.keywords = { u"webview"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/spoofWebviewAsAndroid"_q,
		.title = tr::ayu_SettingsSpoofWebviewAsAndroid(),
		.getter = &AyuSettings::spoofWebviewAsAndroid,
		.setter = &AyuSettings::setSpoofWebviewAsAndroid,
		.keywords = { u"webview"_q, u"android"_q, u"spoof"_q },
	});

	ayu.addCollapsibleToggle({
		.id = u"ayu/biggerWindow"_q,
		.title = tr::ayu_SettingsBiggerWindow(),
		.checkboxes = {
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
		},
		.toggledWhenAll = false,
		.keywords = { u"webview"_q, u"window"_q, u"size"_q },
	});

	ayu.addSectionDivider();

	builder.addSubsectionTitle({
		.id = u"ayu/confirmationsSection"_q,
		.title = tr::ayu_ConfirmationsTitle(),
		.keywords = { u"confirmation"_q },
	});

	ayu.addSettingToggle({
		.id = u"ayu/stickerConfirmation"_q,
		.title = tr::ayu_StickerConfirmation(),
		.getter = &AyuSettings::stickerConfirmation,
		.setter = &AyuSettings::setStickerConfirmation,
		.keywords = { u"sticker"_q, u"confirmation"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/gifConfirmation"_q,
		.title = tr::ayu_GIFConfirmation(),
		.getter = &AyuSettings::gifConfirmation,
		.setter = &AyuSettings::setGifConfirmation,
		.keywords = { u"gif"_q, u"confirmation"_q },
	});
	ayu.addSettingToggle({
		.id = u"ayu/voiceConfirmation"_q,
		.title = tr::ayu_VoiceConfirmation(),
		.getter = &AyuSettings::voiceConfirmation,
		.setter = &AyuSettings::setVoiceConfirmation,
		.keywords = { u"voice"_q, u"confirmation"_q },
	});
}

const auto kMeta = BuildHelper({
	.id = AyuGeneral::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryGeneral,
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSkip();
	BuildQoLToggles(builder, ayu);
	builder.addSkip();
});

} // namespace

rpl::producer<QString> AyuGeneral::title() {
	return tr::ayu_CategoryGeneral();
}

AyuGeneral::AyuGeneral(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuGeneral::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type AyuGeneralId() {
	return AyuGeneral::Id();
}

} // namespace Settings
