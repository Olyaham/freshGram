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
#include "ayu/data/ayu_vault.h"
#include "ayu/secret/secret_vault.h"
#include "ayu/features/recognition/text_recognition.h"
#include "ayu/features/recognition/transcription_config.h"
#include "ayu/features/recognition/transcription_backend.h"
#include "ayu/features/recognition/voice_asr_engine.h"
#include "ayu/ui/boxes/recognition_models_box.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "base/platform/base_platform_info.h"
#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "crl/crl_async.h"
#include "crl/crl_on_main.h"
#include "lang/lang_text_entity.h"
#include "platform/platform_translate_provider.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_menu_icons.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>

#include <array>
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

void AddModelDiscovery(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::InputField*> field,
		Fn<AyuRecognition::TranscriptionConfig()> readConfig,
		bool textGeneration = false) {
	struct State {
		std::shared_ptr<AyuRecognition::EngineLibrary> active;
		std::string request;
		bool showWhenReady = false;
		bool closed = false;
	};
	const auto state = std::make_shared<State>();
	const auto weak = base::make_weak(box.get());
	const auto cancel = [=] {
		state->closed = true;
		if (state->active) state->active->CancelCurrent();
	};
	box->lifetime().add(cancel);
	box->boxClosing() | rpl::on_next(cancel, box->lifetime());
	const auto load = [=](bool force, bool show) {
		if (state->closed) {
			return;
		}
		const auto config = readConfig();
		if (!config.networkError.isEmpty()) {
			if (show) Ui::Toast::Show(config.networkError);
			return;
		}
		const auto request = AyuRecognition::BuildModelDiscoveryJson(config, false);
		if (state->active) {
			if (!force && request == state->request) {
				state->showWhenReady = state->showWhenReady || show;
			} else {
				state->active->CancelCurrent();
			}
			return;
		}
		const auto engine = std::make_shared<AyuRecognition::EngineLibrary>();
		state->active = engine;
		state->request = request;
		state->showWhenReady = show;
		if (show) {
			Ui::Toast::Show(tr::ayu_TranscriptionModelsLoading(tr::now));
		}
		crl::async([=] {
			auto result = AyuRecognition::EngineResult();
			if (engine->Load(config.enginePath)) {
				result = engine->DiscoverModelsBlocking(
					AyuRecognition::BuildModelDiscoveryJson(config, force));
			} else {
				result.code = -1;
			}
			crl::on_main([=] {
				if (!weak || state->closed) {
					return;
				}
				state->active.reset();
				const auto show = base::take(state->showWhenReady);
				if (!show || request != AyuRecognition::BuildModelDiscoveryJson(readConfig(), false)) {
					return;
				}
				if (!result.ok) {
					Ui::Toast::Show(result.code == -1
						? tr::ayu_TranscriptionNoEngine(tr::now)
						: AyuRecognition::DescribeEngineError(result.code, result.error));
					return;
				}
				const auto catalog = QJsonDocument::fromJson(result.text.toUtf8()).object();
				auto models = std::vector<QString>();
				auto labels = std::vector<QString>();
				for (const auto model : catalog.value(u"models"_q).toArray()) {
					const auto object = model.toObject();
					const auto id = object.value(u"id"_q).toString();
					const auto metadata = object.value(u"metadata"_q).toObject();
					const auto supports = metadata.value(textGeneration
						? u"supports_text_generation"_q : u"supports_transcription"_q);
					const auto input = metadata.value(u"input"_q);
					const auto output = metadata.value(u"output"_q);
					if (id.isEmpty() || (supports.isBool() && !supports.toBool())
						|| (output.isArray() && !output.toArray().contains(u"text"_q))
						|| (input.isArray() && !input.toArray().contains(textGeneration
							? u"text"_q : u"audio"_q))) {
						continue;
					}
					models.push_back(id);
					auto label = id;
					const auto context = metadata.value(u"context_tokens"_q).toInteger();
					const auto limit = metadata.value(u"output_tokens"_q).toInteger();
					if (context > 0 || limit > 0) {
						label += u"\n"_q + tr::ayu_TranscriptionModelLimits(
							tr::now,
							lt_context,
							context > 0 ? QString::number(context) : u"?"_q,
							lt_output,
							limit > 0 ? QString::number(limit) : u"?"_q);
					}
					labels.push_back(std::move(label));
				}
				if (models.empty()) {
					Ui::Toast::Show(tr::ayu_TranscriptionNoModels(tr::now));
					return;
				}
				if (catalog.value(u"stale"_q).toBool()) {
					Ui::Toast::Show(tr::ayu_TranscriptionModelsCached(tr::now));
				}
				const auto selected = ranges::find(models, field->getLastText().trimmed());
				const auto index = selected == end(models) ? -1 : int(selected - begin(models));
				box->getDelegate()->show(Box([=](not_null<Ui::GenericBox*> choose) {
					SingleChoiceBox(choose, {
						.title = tr::ayu_TranscriptionModel(),
						.options = labels,
						.initialSelection = index,
						.callback = crl::guard(box, [=](int chosen) {
							if (!state->closed) {
								field->setTextWithTags({ .text = models[chosen] });
							}
						}),
					});
				}));
			});
		});
	};
	const auto choose = box->addRow(object_ptr<Ui::RoundButton>(
		box->verticalLayout(),
		tr::ayu_TranscriptionChooseApiModel(),
		st::defaultLightButton), st::boxRowPadding);
	choose->addClickHandler([=] { load(false, true); });
	const auto refresh = box->addRow(object_ptr<Ui::RoundButton>(
		box->verticalLayout(),
		tr::ayu_TranscriptionFetchModels(),
		st::defaultLightButton), st::boxRowPadding);
	refresh->addClickHandler([=] { load(true, true); });
	crl::on_main(crl::guard(box, [=] {
		const auto config = readConfig();
		const auto ready = config.provider == AyuRecognition::EngineProvider::Custom
			? !config.customBase.trimmed().isEmpty()
			: config.provider == AyuRecognition::EngineProvider::Gemini
			? !config.geminiKey.trimmed().isEmpty()
			: !config.groqKey.trimmed().isEmpty();
		if (ready) {
			load(false, false);
		}
	}));
}

void FillTranscriptionCredentialsBox(
		not_null<Ui::GenericBox*> box,
		TranscriptionProvider provider) {
	auto *settings = &AyuSettings::getInstance();
	box->setTitle(provider == TranscriptionProvider::Gemini
		? tr::ayu_TranscriptionGemini()
		: provider == TranscriptionProvider::Groq
		? tr::ayu_TranscriptionGroq()
		: provider == TranscriptionProvider::Custom
		? tr::ayu_TranscriptionCustom()
		: tr::ayu_TranscriptionWhisper());
	const auto layout = box->verticalLayout();
	const auto rowPadding = style::margins(
		st::boxRowPadding.left(),
		st::boxLittleSkip / 2,
		st::boxRowPadding.right(),
		st::boxLittleSkip / 2);
	box->addSkip(st::boxLittleSkip);
	Ui::PasswordInput *groqKey = nullptr;
	Ui::PasswordInput *geminiKey = nullptr;
	Ui::InputField *groqModel = nullptr;
	Ui::InputField *geminiModel = nullptr;
	Ui::InputField *geminiPrompt = nullptr;
	Ui::InputField *customBase = nullptr;
	Ui::InputField *customModel = nullptr;
	Ui::PasswordInput *customKey = nullptr;
	const auto addField = [=](
			rpl::producer<QString> placeholder,
			const QString &value) {
		return box->addRow(object_ptr<Ui::InputField>(
			layout, st::defaultInputField, std::move(placeholder), value),
			rowPadding);
	};
	const auto addKey = [=](rpl::producer<QString> placeholder, const QString &value) {
		const auto row = box->addRow(
			object_ptr<Ui::RpWidget>(layout), rowPadding);
		row->resize(row->width(), st::defaultInputField.heightMin);
		const auto field = Ui::CreateChild<Ui::PasswordInput>(
			row, st::defaultInputField, std::move(placeholder), value);
		row->sizeValue() | rpl::on_next([=](QSize size) {
			field->resize(size.width(), field->height());
		}, row->lifetime());
		return field;
	};
	if (provider == TranscriptionProvider::Groq) {
		groqKey = addKey(tr::ayu_TranscriptionGroqKey(), settings->transcriptionGroqKey());
		groqModel = addField(
			tr::ayu_TranscriptionModel(), settings->transcriptionGroqModel());
	} else if (provider == TranscriptionProvider::Gemini) {
		geminiKey = addKey(tr::ayu_TranscriptionGeminiKey(), settings->transcriptionGeminiKey());
		geminiModel = addField(
			tr::ayu_TranscriptionModel(), settings->transcriptionGeminiModel());
		geminiPrompt = addField(
			tr::ayu_TranscriptionPrompt(), settings->transcriptionPrompt());
	} else if (provider == TranscriptionProvider::Custom) {
		customBase = addField(
			tr::ayu_TranscriptionCustomBase(), settings->transcriptionCustomBase());
		customModel = addField(
			tr::ayu_TranscriptionCustomModel(), settings->transcriptionCustomModel());
		customKey = addKey(tr::ayu_TranscriptionCustomKey(), settings->transcriptionCustomKey());
	}
	const auto local = (provider == TranscriptionProvider::WhisperLocal);
	const auto language = addField(
		tr::ayu_TranscriptionLanguage(), settings->transcriptionLanguage());
	Ui::Checkbox *gpu = nullptr;
	if (local) {
		gpu = box->addRow(object_ptr<Ui::Checkbox>(
			layout, tr::ayu_TranscriptionAllowGpu(tr::now),
			settings->transcriptionLocalUseGpu()), rowPadding);
	}
	auto enginePath = static_cast<Ui::InputField*>(nullptr);
	if (local) {
		enginePath = addField(
			tr::ayu_TranscriptionEnginePath(),
			settings->transcriptionLocalEnginePath());
		const auto chooseModule = box->addRow(object_ptr<Ui::RoundButton>(
			layout, tr::ayu_TranscriptionChooseModule(), st::defaultLightButton),
			rowPadding);
		chooseModule->addClickHandler([=] {
			FileDialog::GetOpenPath(
				box.get(),
				tr::ayu_TranscriptionChooseModule(tr::now),
				tr::ayu_TranscriptionModuleFilter(tr::now),
				crl::guard(box, [=](FileDialog::OpenResult &&result) {
					if (!result.paths.isEmpty()) {
						enginePath->setTextWithTags({ .text = result.paths.front() });
					}
				}));
		});
	}
	const auto readConfig = [=] {
		const auto engine = local ? AyuRecognition::EngineProvider::WhisperLocal
			: provider == TranscriptionProvider::Gemini
			? AyuRecognition::EngineProvider::Gemini
			: provider == TranscriptionProvider::Custom
			? AyuRecognition::EngineProvider::Custom
			: AyuRecognition::EngineProvider::Groq;
		auto config = AyuRecognition::CurrentTranscriptionConfig(engine);
		if (groqKey) config.groqKey = groqKey->getLastText().trimmed();
		if (groqModel) config.groqModel = groqModel->getLastText().trimmed();
		if (geminiKey) config.geminiKey = geminiKey->getLastText().trimmed();
		if (geminiModel) config.geminiModel = geminiModel->getLastText().trimmed();
		if (geminiPrompt) config.prompt = geminiPrompt->getLastText().trimmed();
		if (customBase) config.customBase = customBase->getLastText().trimmed();
		if (customModel) config.customModel = customModel->getLastText().trimmed();
		if (customKey) config.customKey = customKey->getLastText().trimmed();
		config.language = language->getLastText().trimmed();
		if (enginePath) config.enginePath = enginePath->getLastText().trimmed();
		if (gpu) config.useGpu = gpu->checked();
		AyuRecognition::ApplyTranscriptionNetworkPolicy(config);
		return config;
	};
	const auto validate = [=](const AyuRecognition::TranscriptionConfig &config, bool testing) {
		if (testing && !config.networkError.isEmpty()) {
			Ui::Toast::Show(config.networkError);
			return false;
		}
		if (customBase) {
			const auto url = QUrl(config.customBase);
			if (!url.isValid() || url.host().isEmpty()
				|| (url.scheme() != u"https"_q && url.scheme() != u"http"_q)) {
				customBase->showError();
				return false;
			}
		}
		const auto model = groqModel ? groqModel : geminiModel ? geminiModel : customModel;
		if (model && model->getLastText().trimmed().isEmpty()) {
			model->showError();
			return false;
		}
		const auto key = groqKey ? groqKey : geminiKey;
		if (testing && key && key->getLastText().trimmed().isEmpty()) {
			key->showError();
			return false;
		}
		return true;
	};
	if (!local) {
		AddModelDiscovery(box, groqModel ? groqModel : geminiModel ? geminiModel : customModel, readConfig);
	}
	const auto save = [=] {
		if (!validate(readConfig(), false)) {
			return false;
		}
		if ((groqKey || geminiKey || customKey)
			&& (!AyuVault::HasKey() || !AyuSecret::Vault::Available())) {
			Ui::Toast::Show(Lang::Hard::SecureSaveError());
			return false;
		}
		if (groqKey) {
			settings->setTranscriptionGroqKey(groqKey->getLastText().trimmed());
		}
		if (geminiKey) {
			settings->setTranscriptionGeminiKey(geminiKey->getLastText().trimmed());
		}
		if (geminiPrompt) {
			settings->setTranscriptionPrompt(geminiPrompt->getLastText().trimmed());
		}
		if (groqModel) settings->setTranscriptionGroqModel(groqModel->getLastText().trimmed());
		if (geminiModel) settings->setTranscriptionGeminiModel(geminiModel->getLastText().trimmed());
		if (customBase) {
			settings->setTranscriptionCustomBase(customBase->getLastText().trimmed());
		}
		if (customModel) {
			settings->setTranscriptionCustomModel(customModel->getLastText().trimmed());
		}
		if (customKey) {
			settings->setTranscriptionCustomKey(customKey->getLastText().trimmed());
		}
		settings->setTranscriptionLanguage(language->getLastText().trimmed());
		if (local) {
			settings->setTranscriptionLocalEnginePath(enginePath->getLastText().trimmed());
			settings->setTranscriptionLocalUseGpu(gpu->checked());
		}
		Ui::Toast::Show(tr::ayu_TranscriptionSaved(tr::now));
		return true;
	};
	if (provider == TranscriptionProvider::Groq
		|| provider == TranscriptionProvider::Gemini
		|| provider == TranscriptionProvider::Custom || local) {
		struct TestState {
			std::shared_ptr<AyuRecognition::EngineLibrary> active;
			bool closed = false;
		};
		const auto testing = std::make_shared<TestState>();
		const auto weak = base::make_weak(box.get());
		box->lifetime().add([=] {
			testing->closed = true;
			if (testing->active) testing->active->CancelCurrent();
		});
		box->boxClosing() | rpl::on_next([=] {
			testing->closed = true;
			if (testing->active) testing->active->CancelCurrent();
		}, box->lifetime());
		const auto test = box->addRow(object_ptr<Ui::RoundButton>(
			layout,
			local ? tr::ayu_TranscriptionLocalTest() : tr::ayu_TranscriptionTest(),
			st::defaultLightButton),
			rowPadding);
		test->addClickHandler([=] {
			if (testing->active) {
				testing->active->CancelCurrent();
				return;
			}
			auto config = readConfig();
			if (!validate(config, true)) return;
			config.shadow = AyuRecognition::EngineProvider::Telegram;
			config.cleanupProvider = AyuRecognition::EngineProvider::Telegram;
			const auto engine = std::make_shared<AyuRecognition::EngineLibrary>();
			testing->active = engine;
			Ui::Toast::Show(tr::ayu_TranscriptionTesting(tr::now));
			crl::async([=] {
				const auto silence = std::vector<float>(16000, 0.f);
				auto temp = QTemporaryDir(QDir::tempPath() + u"/freshgram-asr-test-XXXXXX"_q);
				const auto wav = temp.filePath(u"audio.wav"_q);
				auto result = AyuRecognition::EngineResult();
				if (temp.isValid() && AyuRecognition::WriteWavFile(wav, silence)) {
					if (engine->Load(config.enginePath, local)) {
						result = engine->TranscribeBlocking(
							AyuRecognition::BuildVoiceAsrConfigJson(config),
							wav,
							u"audio.wav"_q,
							u"audio/wav"_q);
					} else {
						result.code = -1;
					}
				} else {
					result.code = 4;
					result.error = u"Could not save the file."_q;
				}
				const auto capabilities = engine->Capabilities();
				crl::on_main([=] {
					if (!weak || testing->closed) return;
					testing->active.reset();
					if (result.ok) {
						auto backends = QStringList{ u"CPU"_q };
						if (capabilities & VASR_CAP_CUDA) backends.push_back(u"CUDA"_q);
						if (capabilities & VASR_CAP_VULKAN) backends.push_back(u"Vulkan"_q);
						if (capabilities & VASR_CAP_METAL) backends.push_back(u"Metal"_q);
						if (capabilities & VASR_CAP_HIPBLAS) backends.push_back(u"HIP"_q);
						if (capabilities & VASR_CAP_SYCL) backends.push_back(u"SYCL"_q);
						if (capabilities & VASR_CAP_COREML) backends.push_back(u"CoreML"_q);
						Ui::Toast::Show(local ? tr::ayu_TranscriptionCapabilities(
							tr::now, lt_backend, backends.join(u", "_q))
							: tr::ayu_TranscriptionTestOk(tr::now));
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
	box->addSkip(st::boxLittleSkip);
	box->addButton(tr::lng_settings_save(), [=] {
		if (save()) box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

void BuildTranscriptProcessing(SectionBuilder &builder) {
	builder.addSubsectionTitle(tr::ayu_TranscriptionProcessingSubtitle());
	const auto controller = builder.controller();
	auto *settings = &AyuSettings::getInstance();
	const auto values = std::vector<int>{ 0, 2, 3, 4 };
	const auto labels = std::vector<QString>{
		tr::ayu_RecognitionOff(tr::now), u"Groq"_q, u"Gemini"_q,
		tr::ayu_TranscriptionCustom(tr::now),
	};
	const auto index = [=](int value) {
		const auto found = ranges::find(values, value);
		return found == end(values) ? 0 : int(found - begin(values));
	};
	for (const auto cleanup : { false, true }) {
		builder.addButton({
			.id = cleanup ? u"ayu/transcriptionCleanup"_q : u"ayu/transcriptionShadow"_q,
			.title = cleanup ? tr::ayu_TranscriptionCleanup() : tr::ayu_TranscriptionShadow(),
			.st = &st::settingsButtonNoIcon,
			.label = (cleanup ? settings->transcriptionCleanupProviderValue()
				: settings->transcriptionShadowProviderValue())
				| rpl::map([=](int value) { return labels[index(value)]; }),
			.onClick = [=] {
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					SingleChoiceBox(box, {
						.title = cleanup ? tr::ayu_TranscriptionCleanup() : tr::ayu_TranscriptionShadow(),
						.options = labels,
						.initialSelection = index(cleanup ? settings->transcriptionCleanupProvider()
							: settings->transcriptionShadowProvider()),
						.callback = [=](int selected) {
							if (cleanup) settings->setTranscriptionCleanupProvider(values[selected]);
							else settings->setTranscriptionShadowProvider(values[selected]);
						},
					});
				}));
			},
		});
	}
	builder.addButton({
		.id = u"ayu/transcriptionCleanupModel"_q,
		.title = tr::ayu_TranscriptionCleanupModel(),
		.st = &st::settingsButtonNoIcon,
		.label = settings->transcriptionCleanupModelValue(),
		.onClick = [=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				box->setTitle(tr::ayu_TranscriptionCleanupModel());
				const auto field = box->addRow(object_ptr<Ui::InputField>(
					box->verticalLayout(), st::defaultInputField,
					tr::ayu_TranscriptionModel(), settings->transcriptionCleanupModel()),
					st::boxRowPadding);
				const auto provider = static_cast<AyuRecognition::EngineProvider>(
					settings->transcriptionCleanupProvider());
				if (provider != AyuRecognition::EngineProvider::Telegram) {
					AddModelDiscovery(box, field, [=] {
						return AyuRecognition::CurrentTranscriptionConfig(provider);
					}, true);
				}
				box->addButton(tr::lng_settings_save(), [=] {
					const auto model = field->getLastText().trimmed();
					if (model.isEmpty()) {
						field->showError();
						return;
					}
					settings->setTranscriptionCleanupModel(model);
					box->closeBox();
				});
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			}));
		},
		.shown = settings->transcriptionCleanupProviderValue()
			| rpl::map([](int value) { return value != 0; }),
	});
}

void BuildTranscription(SectionBuilder &builder) {
	const auto controller = builder.controller();
	auto *settings = &AyuSettings::getInstance();

	auto options = std::vector<std::pair<TranscriptionProvider, QString>>{
		{ TranscriptionProvider::Telegram, u"Telegram"_q },
		{ TranscriptionProvider::Auto, tr::ayu_TranscriptionAuto(tr::now) },
		{ TranscriptionProvider::Groq, u"Groq"_q },
		{ TranscriptionProvider::Gemini, u"Gemini"_q },
		{ TranscriptionProvider::Custom, tr::ayu_TranscriptionCustom(tr::now) },
	};
	{
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
	{
		scopeOptions.emplace_back(
			TranscriptionProvider::WhisperLocal,
			tr::ayu_TranscriptionWhisper(tr::now));
	}
	auto scopeLabels = std::vector<QString>();
	scopeLabels.reserve(scopeOptions.size());
	for (const auto &option : scopeOptions) {
		scopeLabels.push_back(option.second);
	}
	auto secretLabels = scopeLabels;
	secretLabels[0] = tr::ayu_TranscriptionSecretDefault(tr::now);
	secretLabels[2] = tr::ayu_TranscriptionSecretAuto(tr::now);

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
			| rpl::map([=](int val) { return secretLabels[val]; }),
		.onClick = [=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				SingleChoiceBox(box, {
					.title = tr::ayu_TranscriptionSecret(),
					.options = secretLabels,
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
	builder.addDividerText(tr::ayu_TranscriptionSecretPolicy());
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
	for (const auto provider : { TranscriptionProvider::Groq,
		TranscriptionProvider::Gemini, TranscriptionProvider::Custom,
		TranscriptionProvider::WhisperLocal }) {
		builder.addButton({
			.id = u"ayu/transcriptionCredentials/"_q + QString::number(int(provider)),
			.title = rpl::single(options[getIndex(provider)].second),
			.st = &st::settingsButtonNoIcon,
			.label = tr::ayu_TranscriptionCredentials(),
			.onClick = [=] {
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					FillTranscriptionCredentialsBox(box, provider);
				}));
			},
		});
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
		.shown = rpl::combine(
			settings->transcriptionProviderValue(),
			settings->transcriptionProviderChatsValue(),
			settings->transcriptionProviderSecretValue(),
			settings->transcriptionProviderPersonalValue()
		) | rpl::map([](
				TranscriptionProvider global,
				TranscriptionProvider chats,
				TranscriptionProvider secret,
				TranscriptionProvider personal) {
				return ranges::any_of(
					std::array{ global, chats, secret, personal },
					[](TranscriptionProvider value) {
						return value == TranscriptionProvider::WhisperLocal
							|| value == TranscriptionProvider::Auto;
					});
			}),
	});
	BuildTranscriptProcessing(builder);
	builder.addDividerText(tr::ayu_TranscriptionNetworkPolicy());
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

	BuildTranscription(builder);
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
