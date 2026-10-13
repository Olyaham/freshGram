// This is the source code of freshGram.
#include "ayu/features/recognition/transcription_backend.h"

#include "ayu/features/recognition/recognition_audio.h"
#include "ayu/features/recognition/recognition_models.h"
#include "ayu/features/recognition/voice_asr_engine.h"
#include "ayu/secret/secret_peer.h"
#include "ayu/ayu_settings.h"
#include "base/weak_ptr.h"
#include "crl/crl_async.h"
#include "crl/crl_on_main.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>

#include <algorithm>
#include <atomic>
#include <map>

namespace AyuRecognition {
namespace {

constexpr auto kMaxDocumentSize = int64(200) * 1024 * 1024;

struct BackendJob {
	base::weak_ptr<Main::Session> session;
	FullMsgId id;
	void *address = nullptr;
	uint64 generation = 0;
	EngineProvider engine = EngineProvider::Telegram;
	DocumentId documentId = 0;
	std::shared_ptr<Data::DocumentMedia> media;
	std::shared_ptr<std::atomic<bool>> cancelled;
	std::shared_ptr<EngineLibrary> library;
	TranscriptionConfig config;
	Fn<void(LocalTranscript)> done;
	rpl::lifetime lifetime;
};

std::map<std::pair<void*, FullMsgId>, std::shared_ptr<BackendJob>> BackendJobs;
std::atomic<uint64> BackendGeneration = 0;

void CancelJob(const std::shared_ptr<BackendJob> &job) {
	job->cancelled->store(true);
	job->library->CancelCurrent();
	job->lifetime.destroy();
	job->done = nullptr;
}

[[nodiscard]] LocalTranscript BackendFailure(const QString &error) {
	auto result = LocalTranscript();
	result.status = LocalTranscribeStatus::Failed;
	result.error = error;
	return result;
}

void FinishBackend(
		const std::shared_ptr<BackendJob> &job,
		LocalTranscript result) {
	const auto key = std::make_pair(job->address, job->id);
	const auto i = BackendJobs.find(key);
	if (i == end(BackendJobs) || i->second->generation != job->generation) {
		return;
	}
	BackendJobs.erase(i);
	job->lifetime.destroy();
	const auto done = std::move(job->done);
	const auto alive = (job->session.get() != nullptr);
	if (alive && done) {
		done(std::move(result));
	}
}

} // namespace

TranscriptionConfig CurrentTranscriptionConfig(EngineProvider engine) {
	const auto &settings = AyuSettings::getInstance();
	auto config = TranscriptionConfig();
	config.provider = engine;
	config.groqKey = settings.transcriptionGroqKey();
	config.groqModel = settings.transcriptionGroqModel();
	config.geminiKey = settings.transcriptionGeminiKey();
	config.geminiModel = settings.transcriptionGeminiModel();
	config.customBase = settings.transcriptionCustomBase();
	config.customModel = settings.transcriptionCustomModel();
	config.customKey = settings.transcriptionCustomKey();
	config.language = settings.transcriptionLanguage();
	config.prompt = settings.transcriptionPrompt();
	if (engine != EngineProvider::WhisperLocal) {
		config.shadow = static_cast<EngineProvider>(settings.transcriptionShadowProvider());
		config.cleanupProvider = static_cast<EngineProvider>(settings.transcriptionCleanupProvider());
		config.cleanupModel = settings.transcriptionCleanupModel();
	}
	config.localModel = SelectedWhisperModelPath();
	config.threads = std::clamp(QThread::idealThreadCount() / 2, 1, 8);
	config.useGpu = settings.transcriptionLocalUseGpu();
	config.enginePath = (engine == EngineProvider::WhisperLocal)
		? settings.transcriptionLocalEnginePath()
		: settings.transcriptionEnginePath();
	return config;
}

namespace {

void StartBackend(const std::shared_ptr<BackendJob> &job) {
	const auto session = job->session.get();
	if (!session) {
		FinishBackend(job, BackendFailure(QString()));
		return;
	}
	const auto document = session->data().document(job->documentId);
	const auto originalPath = document->filepath(true);
	const auto bytes = job->media->bytes();
	if (job->engine == EngineProvider::WhisperLocal
		&& job->config.localModel.isEmpty()) {
		FinishBackend(job, LocalTranscript{
			.status = LocalTranscribeStatus::NoModel,
		});
		return;
	}
	crl::async([=] {
		auto temp = QTemporaryDir(QDir::tempPath() + u"/freshgram-asr-XXXXXX"_q);
		const auto run = [&]() -> LocalTranscript {
			if (job->cancelled->load()) {
				return { .status = LocalTranscribeStatus::Cancelled };
			} else if (!temp.isValid()) {
				return BackendFailure(u"Could not create the temporary directory."_q);
			} else if (job->engine == EngineProvider::Telegram) {
				return BackendFailure(u"Telegram transcription is unavailable for this message."_q);
			} else if (!job->library->Load(
					job->config.enginePath,
					job->engine == EngineProvider::WhisperLocal)) {
				return BackendFailure(u"Install or select a compatible ASR module in Settings."_q);
			}
			auto path = originalPath;
			if ((job->config.shadow != EngineProvider::Telegram
				|| job->config.cleanupProvider != EngineProvider::Telegram)
				&& !(job->library->Capabilities() & VASR_CAP_PROCESSING)) {
				return BackendFailure(u"Update the ASR module to use transcript processing."_q);
			}
			if (path.isEmpty() || !QFileInfo::exists(path)) {
				path = temp.filePath(u"input"_q);
				auto input = QFile(path);
				if (bytes.isEmpty() || !input.open(QIODevice::WriteOnly)
					|| input.write(bytes) != bytes.size()) {
					return BackendFailure(u"Could not save the audio."_q);
				}
			}
			if (QFileInfo(path).size() > kMaxDocumentSize) {
				return BackendFailure(u"The audio file is too large."_q);
			}
			const auto audio = DecodeAudioFile(path, *job->cancelled);
			if (job->cancelled->load()) {
				return { .status = LocalTranscribeStatus::Cancelled };
			} else if (audio.samples.empty() || !audio.error.isEmpty()) {
				return BackendFailure(audio.error);
			}
			const auto wav = temp.filePath(u"audio.wav"_q);
			if (!WriteWavFile(wav, audio.samples)) {
				return BackendFailure(u"Could not save the decoded audio."_q);
			}
			const auto result = job->library->TranscribeBlocking(
				BuildVoiceAsrConfigJson(job->config),
				wav,
				u"audio.wav"_q,
				u"audio/wav"_q);
			if (job->cancelled->load()) {
				return { .status = LocalTranscribeStatus::Cancelled };
			} else if (!result.ok) {
				return BackendFailure(DescribeEngineError(result.code, result.error));
			}
			return { .status = LocalTranscribeStatus::Done, .text = result.text, .details = result.details };
		};
		const auto transcript = run();
		crl::on_main([=] {
			FinishBackend(job, transcript);
		});
	});
}

} // namespace

BackendRoute ResolveTranscription(not_null<HistoryItem*> item) {
	const auto &settings = AyuSettings::getInstance();
	const auto peer = item->history()->peer;
	auto provider = TranscriptionProvider::Default;
	if (AyuSecret::IsSecretPeer(peer)) {
		provider = settings.transcriptionProviderSecret();
	} else if (peer->isUser()) {
		provider = settings.transcriptionProviderPersonal();
	} else {
		provider = settings.transcriptionProviderChats();
	}
	if (provider == TranscriptionProvider::Default) {
		provider = settings.transcriptionProvider();
	}
	if (provider == TranscriptionProvider::Default) {
		provider = TranscriptionProvider::Telegram;
	}
	const auto serverUnavailable = AyuSecret::IsSecretPeer(peer) || item->isLocal();
	switch (provider) {
	case TranscriptionProvider::Telegram:
		return { serverUnavailable ? BackendDestination::Engine : BackendDestination::Server,
			EngineProvider::Telegram };
	case TranscriptionProvider::WhisperLocal:
		return { BackendDestination::Engine, EngineProvider::WhisperLocal };
	case TranscriptionProvider::Groq:
		return { BackendDestination::Engine, EngineProvider::Groq };
	case TranscriptionProvider::Gemini:
		return { BackendDestination::Engine, EngineProvider::Gemini };
	case TranscriptionProvider::Custom:
		return { BackendDestination::Engine, EngineProvider::Custom };
	case TranscriptionProvider::Auto:
		break;
	case TranscriptionProvider::Default:
		return { BackendDestination::Server, EngineProvider::Telegram };
	}
	if (!serverUnavailable && item->history()->session().premium()) {
		return { BackendDestination::Server, EngineProvider::Telegram };
	}
	if (!settings.transcriptionGroqKey().isEmpty()) {
		return { BackendDestination::Engine, EngineProvider::Groq };
	}
	if (!settings.transcriptionGeminiKey().isEmpty()) {
		return { BackendDestination::Engine, EngineProvider::Gemini };
	}
	if (!settings.transcriptionCustomBase().isEmpty()
		&& !settings.transcriptionCustomModel().isEmpty()) {
		return { BackendDestination::Engine, EngineProvider::Custom };
	}
	if (!SelectedWhisperModelPath().isEmpty()
		&& EngineLibrary::IsAvailable(settings.transcriptionLocalEnginePath(), true)) {
		return { BackendDestination::Engine, EngineProvider::WhisperLocal };
	}
	return { serverUnavailable ? BackendDestination::Engine : BackendDestination::Server,
		EngineProvider::Telegram };
}

bool UsesExternalTranscription(not_null<HistoryItem*> item) {
	return ResolveTranscription(item).destination == BackendDestination::Engine;
}

void TranscribeWithBackend(
		not_null<HistoryItem*> item,
		EngineProvider engine,
		Fn<void(LocalTranscript)> done) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	if (!document
		|| !(document->isVoiceMessage() || document->isVideoMessage())) {
		auto result = LocalTranscript();
		result.error = u"Not a voice message."_q;
		done(std::move(result));
		return;
	} else if (document->size > kMaxDocumentSize) {
		done(BackendFailure(u"The file is too large."_q));
		return;
	}
	CancelBackendTranscription(item);
	const auto session = &item->history()->session();
	const auto job = std::make_shared<BackendJob>();
	job->session = base::make_weak(session);
	job->id = item->fullId();
	job->address = static_cast<void*>(session);
	job->generation = ++BackendGeneration;
	job->engine = engine;
	job->config = CurrentTranscriptionConfig(engine);
	job->library = std::make_shared<EngineLibrary>();
	job->documentId = document->id;
	job->media = document->createMediaView();
	job->cancelled = std::make_shared<std::atomic<bool>>(false);
	job->done = std::move(done);
	BackendJobs.emplace(std::make_pair(job->address, job->id), job);

	if (job->media->loaded(true)) {
		StartBackend(job);
		return;
	}
	session->downloaderTaskFinished(
	) | rpl::filter([=] {
		const auto session = job->session.get();
		return !session
			|| (job->media->loaded()
				|| !session->data().document(job->documentId)->loading());
	}) | rpl::take(1) | rpl::on_next([=] {
		StartBackend(job);
	}, job->lifetime);
	document->save(::Data::FileOrigin(job->id), QString());
}

void CancelBackendTranscription(not_null<const HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto key = std::make_pair(
		static_cast<void*>(session),
		item->fullId());
	const auto i = BackendJobs.find(key);
	if (i == end(BackendJobs)) {
		return;
	}
	const auto job = i->second;
	BackendJobs.erase(i);
	CancelJob(job);
}

void CancelBackendTranscriptions(not_null<Main::Session*> session) {
	for (auto i = BackendJobs.begin(); i != BackendJobs.end();) {
		if (i->first.first != session.get()) {
			++i;
			continue;
		}
		const auto job = i->second;
		i = BackendJobs.erase(i);
		CancelJob(job);
	}
}

} // namespace AyuRecognition
