// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_general.h"

#include "lang_auto.h"
#include "lang/lang_keys.h"
#include "ayu/ayu_settings.h"
#include "ayu/features/recognition/speech_recognition.h"
#include "ayu/features/recognition/text_recognition.h"
#include "ayu/features/recognition/transcription_config.h"
#include "ayu/features/recognition/voice_asr_engine.h"
#include "ayu/ui/boxes/recognition_models_box.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "base/platform/base_platform_info.h"
#include "core/application.h"
#include "crl/crl_async.h"
#include "crl/crl_on_main.h"
#include "lang/lang_text_entity.h"
#include "platform/platform_translate_provider.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QRegularExpression>

#include <QtCore/QFile>

#include <memory>
#include <vector>

namespace Settings {

using namespace Builder;
using namespace AyuBuilder;

namespace {

void BuildTranslator(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	builder.addSubsectionTitle(tr::lng_translate_settings_subtitle());

	auto *settings = &AyuSettings::getInstance();

	const auto options = std::vector{
		std::pair(TranslationProvider::Telegram, QString("Telegram")),
		std::pair(TranslationProvider::Google, QString("Google")),
		std::pair(TranslationProvider::Yandex, QString("Yandex")),
	};
	const auto nativeAvailable = Platform::IsTranslateProviderAvailable();
	auto availableOptions = options;
	if (nativeAvailable) {
		availableOptions.push_back(std::pair(
			TranslationProvider::Native,
			[] {
				if constexpr (Platform::IsMac()) {
					return QString("macOS");
				} else if constexpr (Platform::IsWindows()) {
					return QString("Windows");
				} else {
					return QString("Linux");
				}
			}()));
	}
	auto optionLabels = std::vector<QString>();
	optionLabels.reserve(availableOptions.size());
	for (const auto &option : availableOptions) {
		optionLabels.push_back(option.second);
	}

	const auto getIndex = [=](TranslationProvider val) {
		const auto i = ranges::find(
			availableOptions,
			val,
			&std::pair<TranslationProvider, QString>::first);
		return (i != end(availableOptions))
			? int(i - begin(availableOptions))
			: 0;
	};

	auto currentVal = AyuSettings::getInstance().translationProviderValue()
		| rpl::map(getIndex)
		| rpl::map([=](int val) { return availableOptions[val].second; });

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
								const auto option = availableOptions[index].first;
								AyuSettings::getInstance().setTranslationProvider(option);

								if constexpr (Platform::IsMac()) {
									if (option == TranslationProvider::Native) {
										controller->showToast(Ui::Toast::Config{
											.text = tr::lng_translate_settings_use_platform_mac_about(tr::now, tr::rich),
											.duration = 6 * crl::time(1000)
										});
									}
								}
							};
							SingleChoiceBox(box, {
								.title = tr::ayu_TranslationProvider(),
								.options = optionLabels,
								.initialSelection = getIndex(settings->translationProvider()),
								.callback = save,
							});
						}));
			}
		},
	});
	if (button) {
		ayu.addBetaBadge(button);
	}
}

void FillTranscriptionCredentialsBox(
		not_null<Ui::GenericBox*> box,
		TranscriptionProvider provider) {
	auto *settings = &AyuSettings::getInstance();
	box->setTitle(tr::ayu_TranscriptionCredentials());
	const auto layout = box->verticalLayout();
	Ui::InputField *groqKey = nullptr;
	Ui::InputField *geminiKey = nullptr;
	Ui::InputField *geminiPrompt = nullptr;
	Ui::InputField *customBase = nullptr;
	Ui::InputField *customModel = nullptr;
	Ui::InputField *customKey = nullptr;
	if (provider == TranscriptionProvider::Groq) {
		groqKey = layout->add(object_ptr<Ui::InputField>(
			layout,
			st::defaultInputField,
			rpl::single(tr::ayu_TranscriptionGroqKey(tr::now)),
			settings->transcriptionGroqKey()));
	} else if (provider == TranscriptionProvider::Gemini) {
		geminiKey = layout->add(object_ptr<Ui::InputField>(
			layout,
			st::defaultInputField,
			rpl::single(tr::ayu_TranscriptionGeminiKey(tr::now)),
			settings->transcriptionGeminiKey()));
		geminiPrompt = layout->add(object_ptr<Ui::InputField>(
			layout,
			st::defaultInputField,
			rpl::single(tr::ayu_TranscriptionPrompt(tr::now)),
			settings->transcriptionPrompt()));
	} else if (provider == TranscriptionProvider::Custom) {
		customBase = layout->add(object_ptr<Ui::InputField>(
			layout,
			st::defaultInputField,
			rpl::single(tr::ayu_TranscriptionCustomBase(tr::now)),
			settings->transcriptionCustomBase()));
		customModel = layout->add(object_ptr<Ui::InputField>(
			layout,
			st::defaultInputField,
			rpl::single(tr::ayu_TranscriptionCustomModel(tr::now)),
			settings->transcriptionCustomModel()));
		customKey = layout->add(object_ptr<Ui::InputField>(
			layout,
			st::defaultInputField,
			rpl::single(tr::ayu_TranscriptionCustomKey(tr::now)),
			settings->transcriptionCustomKey()));
	}
	const auto enginePath = layout->add(object_ptr<Ui::InputField>(
		layout,
		st::defaultInputField,
		rpl::single(tr::ayu_TranscriptionEnginePath(tr::now)),
		settings->transcriptionEnginePath()));
	const auto save = [=] {
		if (groqKey) {
			settings->setTranscriptionGroqKey(groqKey->getLastText().trimmed());
		}
		if (geminiKey) {
			settings->setTranscriptionGeminiKey(geminiKey->getLastText().trimmed());
		}
		if (geminiPrompt) {
			settings->setTranscriptionPrompt(geminiPrompt->getLastText().trimmed());
		}
		if (customBase) {
			settings->setTranscriptionCustomBase(customBase->getLastText().trimmed());
		}
		if (customModel) {
			settings->setTranscriptionCustomModel(customModel->getLastText().trimmed());
		}
		if (customKey) {
			settings->setTranscriptionCustomKey(customKey->getLastText().trimmed());
		}
		settings->setTranscriptionEnginePath(enginePath->getLastText().trimmed());
		Ui::Toast::Show(tr::ayu_TranscriptionSaved(tr::now));
	};
	if (provider == TranscriptionProvider::Groq
		|| provider == TranscriptionProvider::Gemini
		|| provider == TranscriptionProvider::Custom) {
		const auto testing = std::make_shared<bool>(false);
		box->addButton(tr::ayu_TranscriptionTest(), [=] {
			if (*testing) {
				return;
			}
			*testing = true;
			auto config = AyuRecognition::TranscriptionConfig();
			auto engine = AyuRecognition::EngineProvider::Groq;
			if (provider == TranscriptionProvider::Gemini) {
				engine = AyuRecognition::EngineProvider::Gemini;
			} else if (provider == TranscriptionProvider::Custom) {
				engine = AyuRecognition::EngineProvider::Custom;
			}
			config.provider = engine;
			config.groqKey = groqKey ? groqKey->getLastText().trimmed() : QString();
			config.groqModel = settings->transcriptionGroqModel();
			config.geminiKey = geminiKey ? geminiKey->getLastText().trimmed() : QString();
			config.prompt = geminiPrompt ? geminiPrompt->getLastText().trimmed() : QString();
			config.geminiModel = settings->transcriptionGeminiModel();
			config.customBase = customBase ? customBase->getLastText().trimmed() : QString();
			config.customModel = customModel ? customModel->getLastText().trimmed() : QString();
			config.customKey = customKey ? customKey->getLastText().trimmed() : QString();
			config.language = settings->transcriptionLanguage();
			config.enginePath = enginePath->getLastText().trimmed();
			crl::async([=] {
				const auto silence = std::vector<float>(16000, 0.f);
				const auto wav = AyuRecognition::UniqueTempPath(u".wav"_q);
				auto result = AyuRecognition::EngineResult();
				if (AyuRecognition::WriteWavFile(wav, silence)) {
					auto engine = AyuRecognition::EngineLibrary();
					if (engine.Load(config.enginePath)) {
						auto *token = engine.CreateCancel();
						result = engine.TranscribeBlocking(
							AyuRecognition::BuildVoiceAsrConfigJson(config),
							wav,
							u"audio.wav"_q,
							u"audio/wav"_q,
							token);
						engine.FreeCancel(token);
					} else {
						result.code = -1;
					}
				} else {
					result.code = 4;
					result.error = u"Could not save the file."_q;
				}
				QFile::remove(wav);
				crl::on_main([=] {
					*testing = false;
					if (result.ok) {
						Ui::Toast::Show(tr::ayu_TranscriptionTestOk(tr::now));
					} else if (result.code == -1) {
						Ui::Toast::Show(tr::ayu_TranscriptionNoEngine(tr::now));
					} else {
						Ui::Toast::Show(tr::ayu_TranscribeFailed(
							tr::now,
							lt_error,
							AyuRecognition::DescribeEngineError(result.code, result.error)));
					}
				});
			});
		});
	}
	box->addButton(tr::lng_settings_save(), [=] {
		save();
		box->closeBox();
	});
}

void BuildTranscription(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	const auto controller = builder.controller();
	auto *settings = &AyuSettings::getInstance();
	const auto engine = AyuRecognition::EngineLibrary::IsAvailable(
		settings->transcriptionEnginePath());

	auto options = std::vector<std::pair<TranscriptionProvider, QString>>{
		{ TranscriptionProvider::Telegram, u"Telegram"_q },
		{ TranscriptionProvider::Auto, tr::ayu_TranscriptionAuto(tr::now) },
		{ TranscriptionProvider::Groq, u"Groq"_q },
		{ TranscriptionProvider::Gemini, u"Gemini"_q },
		{ TranscriptionProvider::Custom, tr::ayu_TranscriptionCustom(tr::now) },
	};
	if (AyuRecognition::SpeechRecognitionAvailable() || engine) {
		options.emplace_back(
			TranscriptionProvider::WhisperLocal,
			tr::ayu_TranscriptionWhisper(tr::now));
	}
	auto optionLabels = std::vector<QString>();
	optionLabels.reserve(options.size());
	for (const auto &option : options) {
		optionLabels.push_back(option.second);
	}
	auto scopeOptions = std::vector<std::pair<TranscriptionProvider, QString>>{
		{ TranscriptionProvider::Default, tr::ayu_TranscriptionUseDefault(tr::now) },
		{ TranscriptionProvider::Telegram, u"Telegram"_q },
		{ TranscriptionProvider::Auto, tr::ayu_TranscriptionAuto(tr::now) },
		{ TranscriptionProvider::Groq, u"Groq"_q },
		{ TranscriptionProvider::Gemini, u"Gemini"_q },
		{ TranscriptionProvider::Custom, tr::ayu_TranscriptionCustom(tr::now) },
	};
	if (AyuRecognition::SpeechRecognitionAvailable() || engine) {
		scopeOptions.emplace_back(
			TranscriptionProvider::WhisperLocal,
			tr::ayu_TranscriptionWhisper(tr::now));
	}
	auto scopeLabels = std::vector<QString>();
	scopeLabels.reserve(scopeOptions.size());
	for (const auto &option : scopeOptions) {
		scopeLabels.push_back(option.second);
	}

	const auto getIndex = [=](TranscriptionProvider val) {
		const auto i = ranges::find(
			options,
			val,
			&std::pair<TranscriptionProvider, QString>::first);
		return (i != end(options))
			? int(i - begin(options))
			: 0;
	};
	const auto getScopeIndex = [=](TranscriptionProvider val) {
		const auto i = ranges::find(
			scopeOptions,
			val,
			&std::pair<TranscriptionProvider, QString>::first);
		return (i != end(scopeOptions))
			? int(i - begin(scopeOptions))
			: 0;
	};

	builder.addSubsectionTitle(tr::ayu_TranscriptionSubtitle());
	builder.addButton({
		.id = u"ayu/transcriptionProvider"_q,
		.altIds = { u"ayu/whisperProvider"_q, u"ayu/voiceToText"_q },
		.title = tr::ayu_TranscriptionProvider(),
		.st = &st::settingsButtonNoIcon,
		.label = settings->transcriptionProviderValue()
			| rpl::map(getIndex)
			| rpl::map([=](int val) { return options[val].second; }),
		.onClick = [=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = tr::ayu_TranscriptionProvider(),
					.options = optionLabels,
					.initialSelection = getIndex(settings->transcriptionProvider()),
					.callback = [=](int index) {
						settings->setTranscriptionProvider(options[index].first);
					},
				});
			}));
		},
	});
	builder.addButton({
		.id = u"ayu/transcriptionChats"_q,
		.title = tr::ayu_TranscriptionChats(),
		.st = &st::settingsButtonNoIcon,
		.label = settings->transcriptionProviderChatsValue()
			| rpl::map(getScopeIndex)
			| rpl::map([=](int val) { return scopeOptions[val].second; }),
		.onClick = [=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = tr::ayu_TranscriptionChats(),
					.options = scopeLabels,
					.initialSelection = getScopeIndex(
						settings->transcriptionProviderChats()),
					.callback = [=](int index) {
						settings->setTranscriptionProviderChats(
							scopeOptions[index].first);
					},
				});
			}));
		},
	});
	builder.addButton({
		.id = u"ayu/transcriptionSecret"_q,
		.title = tr::ayu_TranscriptionSecret(),
		.st = &st::settingsButtonNoIcon,
		.label = settings->transcriptionProviderSecretValue()
			| rpl::map(getScopeIndex)
			| rpl::map([=](int val) { return scopeOptions[val].second; }),
		.onClick = [=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = tr::ayu_TranscriptionSecret(),
					.options = scopeLabels,
					.initialSelection = getScopeIndex(
						settings->transcriptionProviderSecret()),
					.callback = [=](int index) {
						settings->setTranscriptionProviderSecret(
							scopeOptions[index].first);
					},
				});
			}));
		},
	});
	builder.addButton({
		.id = u"ayu/transcriptionPersonal"_q,
		.title = tr::ayu_TranscriptionPersonal(),
		.st = &st::settingsButtonNoIcon,
		.label = settings->transcriptionProviderPersonalValue()
			| rpl::map(getScopeIndex)
			| rpl::map([=](int val) { return scopeOptions[val].second; }),
		.onClick = [=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = tr::ayu_TranscriptionPersonal(),
					.options = scopeLabels,
					.initialSelection = getScopeIndex(
						settings->transcriptionProviderPersonal()),
					.callback = [=](int index) {
						settings->setTranscriptionProviderPersonal(
							scopeOptions[index].first);
					},
				});
			}));
		},
	});
	builder.addButton({
		.id = u"ayu/transcriptionCredentials"_q,
		.title = tr::ayu_TranscriptionCredentials(),
		.st = &st::settingsButtonNoIcon,
		.label = settings->transcriptionProviderValue()
			| rpl::map(getIndex)
			| rpl::map([=](int val) { return options[val].second; }),
		.onClick = [=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				FillTranscriptionCredentialsBox(box, settings->transcriptionProvider());
			}));
		},
		.shown = settings->transcriptionProviderValue()
			| rpl::map([](TranscriptionProvider value) {
				return value == TranscriptionProvider::Groq
					|| value == TranscriptionProvider::Gemini
					|| value == TranscriptionProvider::Custom;
			}),
	});
	if (!AyuRecognition::SpeechRecognitionAvailable() && !engine) {
		return;
	}
	builder.addButton({
		.id = u"ayu/whisperModel"_q,
		.altIds = { u"ayu/whisperModels"_q, u"ayu/speechModel"_q },
		.title = tr::ayu_WhisperModel(),
		.st = &st::settingsButtonNoIcon,
		.label = settings->whisperModelValue()
			| rpl::map([](QString file) {
				if (file.isEmpty()) {
					return tr::ayu_RecognitionOff(tr::now);
				}
				file.remove(QRegularExpression(u"^ggml-|-q\\d+_\\d+\\.bin$"_q));
				return file;
			}),
		.onClick = [=] {
			controller->show(Box<AyuUi::RecognitionModelsBox>(
				&controller->session(),
				AyuRecognition::ModelKind::Whisper));
		},
		.shown = settings->transcriptionProviderValue()
			| rpl::map([](TranscriptionProvider value) {
				return value == TranscriptionProvider::WhisperLocal;
			}),
	});
}

void BuildTextRecognition(SectionBuilder &builder) {
	if (!AyuRecognition::TextRecognitionAvailable()) {
		return;
	}
	const auto controller = builder.controller();
	auto *settings = &AyuSettings::getInstance();

	builder.addSubsectionTitle(tr::ayu_TextRecognitionSubtitle());
	builder.addButton({
		.id = u"ayu/ocrLanguages"_q,
		.altIds = { u"ayu/ocr"_q, u"ayu/tesseract"_q, u"ayu/textRecognition"_q },
		.title = tr::ayu_ManageOcrLanguages(),
		.st = &st::settingsButtonNoIcon,
		.label = settings->ocrLanguagesValue()
			| rpl::map([](QString codes) {
				if (codes.isEmpty()) {
					return tr::ayu_RecognitionOff(tr::now);
				} else if (codes == AyuRecognition::kAutoOcrLanguages.utf16()) {
					return tr::ayu_OcrAutoShort(tr::now);
				}
				return codes.replace('+', u", "_q);
			}),
		.onClick = [=] {
			controller->show(Box<AyuUi::RecognitionModelsBox>(
				&controller->session(),
				AyuRecognition::ModelKind::Tesseract));
		},
	});
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
		.id = u"ayu/showPeerId"_q,
		.altIds = { u"ayu/showIdAndDc"_q },
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
	});
}

void BuildQoLToggles(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	auto *settings = &AyuSettings::getInstance();

	BuildTranslator(builder, ayu);
	ayu.addSectionDivider();

	BuildTranscription(builder, ayu);
	ayu.addSectionDivider();

	if (AyuRecognition::TextRecognitionAvailable()) {
		BuildTextRecognition(builder);
		ayu.addSectionDivider();
	}

	builder.addSubsectionTitle(tr::ayu_CategoryGeneral());

	const auto controller = builder.controller();
	ayu.addSettingToggle({
		.id = u"ayu/localPinnedChats"_q,
		.title = tr::ayu_LocalPinnedChats(),
		.getter = &AyuSettings::localPinnedChats,
		.setter = &AyuSettings::setLocalPinnedChats,
	});
	ayu.addToggle({
		.id = u"ayu/disableStories"_q,
		.altIds = { u"ayu/hideStories"_q },
		.title = tr::ayu_DisableStories(),
		.getter = [=] { return settings->disableStories(); },
		.setter = [=](bool enabled) {
			AyuSettings::getInstance().setDisableStories(enabled);
			ShowRestartPrompt(controller);
		},
	});

	ayu.addSettingToggle({
		.id = u"ayu/disableOpenLinkWarning"_q,
		.title = tr::ayu_DisableOpenLinkWarning(),
		.getter = &AyuSettings::disableOpenLinkWarning,
		.setter = &AyuSettings::setDisableOpenLinkWarning,
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
	});

	ayu.addSettingToggle({
		.id = u"ayu/disableNotificationsDelay"_q,
		.title = tr::ayu_DisableNotificationsDelay(),
		.getter = &AyuSettings::disableNotificationsDelay,
		.setter = &AyuSettings::setDisableNotificationsDelay,
	});

	ayu.addSectionDivider();

	const auto zalgoButton = builder.addButton({
		.id = u"ayu/filterZalgo"_q,
		.title = tr::ayu_FilterZalgo(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filterZalgo()),
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
				ShowRestartPrompt(controller);
			},
			zalgoButton->lifetime());
		ayu.addBetaBadge(zalgoButton);
	}

	ayu.addSettingToggle({
		.id = u"ayu/improveLinkPreviews"_q,
		.title = tr::ayu_ImproveLinkPreviews(),
		.getter = &AyuSettings::improveLinkPreviews,
		.setter = &AyuSettings::setImproveLinkPreviews,
	});
	ayu.addCollapsibleToggle({
		.id = u"ayu/confirmations"_q,
		.title = tr::ayu_ConfirmationsTitle(),
		.checkboxes = {
			NestedEntry{
				tr::ayu_StickerConfirmation(tr::now),
				[] { return AyuSettings::getInstance().stickerConfirmation(); },
				[](bool v) { AyuSettings::getInstance().setStickerConfirmation(v); }
			},
			NestedEntry{
				tr::ayu_GIFConfirmation(tr::now),
				[] { return AyuSettings::getInstance().gifConfirmation(); },
				[](bool v) { AyuSettings::getInstance().setGifConfirmation(v); }
			},
			NestedEntry{
				tr::ayu_VoiceConfirmation(tr::now),
				[] { return AyuSettings::getInstance().voiceConfirmation(); },
				[](bool v) { AyuSettings::getInstance().setVoiceConfirmation(v); }
			},
			NestedEntry{
				tr::ayu_RoundConfirmation(tr::now),
				[] { return AyuSettings::getInstance().roundConfirmation(); },
				[](bool v) { AyuSettings::getInstance().setRoundConfirmation(v); }
			}
		},
		.toggledWhenAll = false,
	});
	ayu.addSettingToggle({
		.id = u"ayu/showMessageSeconds"_q,
		.altIds = { u"ayu/formatTimeWithSeconds"_q },
		.title = tr::ayu_SettingsShowMessageSeconds(),
		.getter = &AyuSettings::showMessageSeconds,
		.setter = &AyuSettings::setShowMessageSeconds,
	});
	ayu.addSettingToggle({
		.id = u"ayu/showTimeZone"_q,
		.altIds = { u"ayu/timeZoneName"_q },
		.title = tr::ayu_SettingsShowTimeZone(),
		.getter = &AyuSettings::showTimeZone,
		.setter = &AyuSettings::setShowTimeZone,
	});

	BuildShowPeerId(builder);

	ayu.addSectionDivider();

	builder.addSubsectionTitle(rpl::single(QString("Webview")));

	ayu.addSettingToggle({
		.id = u"ayu/spoofWebviewAsAndroid"_q,
		.title = tr::ayu_SettingsSpoofWebviewAsAndroid(),
		.getter = &AyuSettings::spoofWebviewAsAndroid,
		.setter = &AyuSettings::setSpoofWebviewAsAndroid,
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
	});
	ayu.addSectionDivider();

	builder.addSubsectionTitle(tr::lng_settings_privacy_title());

	ayu.addSettingToggle({
		.id = u"ayu/keepDraftsLocal"_q,
		.altIds = { u"ayu/draftsSync"_q },
		.title = tr::ayu_KeepDraftsLocal(),
		.getter = &AyuSettings::keepDraftsLocal,
		.setter = &AyuSettings::setKeepDraftsLocal,
	});
	ayu.addSettingToggle({
		.id = u"ayu/reduceNetworkTracking"_q,
		.altIds = { u"ayu/temporaryKeyLifetime"_q },
		.title = tr::ayu_ReduceNetworkTracking(),
		.getter = &AyuSettings::reduceNetworkTracking,
		.setter = &AyuSettings::setReduceNetworkTracking,
	});

	builder.addSkip();
	builder.addDividerText(tr::ayu_NetworkPrivacyDescription());

	ayu.addSettingToggle({
		.id = u"ayu/enableDebugLogging"_q,
		.title = tr::ayu_EnableDebugLogging(),
		.getter = &AyuSettings::enableDebugLogging,
		.setter = &AyuSettings::setEnableDebugLogging,
	});

	builder.addSkip();
	builder.addDividerText(tr::ayu_DebugLoggingDescription());
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
