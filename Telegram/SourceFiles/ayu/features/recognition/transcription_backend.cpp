// This is the source code of freshGram.
#include "ayu/features/recognition/transcription_backend.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/recognition/recognition_audio.h"
#include "ayu/features/recognition/recognition_models.h"
#include "ayu/features/recognition/speech_recognition.h"
#include "ayu/secret/secret_peer.h"
#include "ayu/features/recognition/voice_asr_engine.h"
#include "base/weak_ptr.h"
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
#include <QtCore/QThread>

#include <atomic>
#include <algorithm>
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
	QString inputPath;
	QString wavPath;
	std::shared_ptr<Data::DocumentMedia> media;
	std::shared_ptr<std::atomic<bool>> cancelled;
	void *cancelToken = nullptr;
	Fn<void(LocalTranscript)> done;
	rpl::lifetime lifetime;
};

std::map<std::pair<void*, FullMsgId>, std::shared_ptr<BackendJob>> BackendJobs;
std::atomic<uint64> BackendGeneration = 0;

EngineLibrary &BackendEngine() {
	static auto engine = EngineLibrary();
	if (!engine.IsLoaded()) {
		engine.Load(AyuSettings::getInstance().transcriptionEnginePath());
	}
	return engine;
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
	QFile::remove(job->inputPath);
	QFile::remove(job->wavPath);
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

[[nodiscard]] TranscriptionConfig BackendConfig(EngineProvider engine) {
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
	config.localModel = SelectedWhisperModelPath();
	config.threads = std::clamp(QThread::idealThreadCount() / 2, 1, 8);
	return config;
}

void StartBackend(const std::shared_ptr<BackendJob> &job) {
	const auto session = job->session.get();
	if (!session) {
		FinishBackend(job, BackendFailure(QString()));
		return;
	}
	const auto document = session->data().document(job->documentId);
	auto path = document->filepath(true);
	if (path.isEmpty() || !QFileInfo::exists(path)) {
		const auto bytes = job->media->bytes();
		if (bytes.isEmpty()) {
			FinishBackend(job, BackendFailure(u"No data."_q));
			return;
		}
		job->inputPath = UniqueTempPath(u".bin"_q);
		auto input = QFile(job->inputPath);
		if (!input.open(QIODevice::WriteOnly)
			|| input.write(bytes) != bytes.size()) {
			FinishBackend(job, BackendFailure(u"Could not save the file."_q));
			return;
		}
		path = job->inputPath;
	}
	job->wavPath = UniqueTempPath(u".wav"_q);
	const auto config = BuildVoiceAsrConfigJson(BackendConfig(job->engine));
	if (job->engine == EngineProvider::WhisperLocal
		&& SelectedWhisperModelPath().isEmpty()) {
		FinishBackend(job, LocalTranscript{
			.status = LocalTranscribeStatus::NoModel,
		});
		return;
	}
	auto &engine = BackendEngine();
	crl::async([=] {
		// WHY: every provider receives the same decoded WAV, so cloud
		// endpoints never see a mislabeled container and no per-backend
		// remux can leak temp files.
		const auto cancelled = std::atomic<bool>(false);
		const auto audio = DecodeAudioFile(path, cancelled);
		if (job->cancelled->load()) {
			FinishBackend(job, BackendFailure(u"Cancelled."_q));
			return;
		} else if (audio.samples.empty()) {
			FinishBackend(job, BackendFailure(audio.error));
			return;
		}
		if (!WriteWavFile(job->wavPath, audio.samples)) {
			FinishBackend(job, BackendFailure(u"Could not save the file."_q));
			return;
		}
		job->cancelToken = engine.CreateCancel();
		const auto result = engine.TranscribeBlocking(
			config,
			job->wavPath,
			u"audio.wav"_q,
			u"audio/wav"_q,
			job->cancelToken);
		engine.FreeCancel(job->cancelToken);
		job->cancelToken = nullptr;
		if (job->cancelled->load()) {
			FinishBackend(job, BackendFailure(u"Cancelled."_q));
			return;
		}
		auto transcript = LocalTranscript();
		if (result.ok) {
			transcript.status = LocalTranscribeStatus::Done;
			transcript.text = result.text;
		} else {
			transcript = BackendFailure(DescribeEngineError(result.code, result.error));
		}
		crl::on_main([=] {
			FinishBackend(job, std::move(transcript));
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
	const auto engine = EngineLibrary::IsAvailable(settings.transcriptionEnginePath());
	switch (provider) {
	case TranscriptionProvider::Telegram:
		return { BackendDestination::Server, EngineProvider::Telegram };
	case TranscriptionProvider::WhisperLocal:
		if (engine) {
			return { BackendDestination::Engine, EngineProvider::WhisperLocal };
		}
		return SpeechRecognitionAvailable()
			? BackendRoute{ BackendDestination::StaticLocal, EngineProvider::WhisperLocal }
			: BackendRoute{ BackendDestination::Server, EngineProvider::Telegram };
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
	if (item->history()->session().premium()) {
		return { BackendDestination::Server, EngineProvider::Telegram };
	}
	if (!settings.transcriptionGroqKey().isEmpty()) {
		return { BackendDestination::Engine, EngineProvider::Groq };
	}
	if (!settings.transcriptionGeminiKey().isEmpty()) {
		return { BackendDestination::Engine, EngineProvider::Gemini };
	}
	if (!settings.transcriptionCustomBase().isEmpty()
		&& !settings.transcriptionCustomKey().isEmpty()) {
		return { BackendDestination::Engine, EngineProvider::Custom };
	}
	if (engine) {
		return { BackendDestination::Engine, EngineProvider::WhisperLocal };
	}
	return SpeechRecognitionAvailable()
		? BackendRoute{ BackendDestination::StaticLocal, EngineProvider::WhisperLocal }
		: BackendRoute{ BackendDestination::Server, EngineProvider::Telegram };
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
	job->documentId = document->id;
	job->media = document->createMediaView();
	job->cancelled = std::make_shared<std::atomic<bool>>(false);
	job->done = std::move(done);
	BackendJobs.emplace(std::make_pair(job->address, job->id), job);

	if (job->media->loaded(true)) {
		StartBackend(job);
		return;
	}
	document->save(::Data::FileOrigin(job->id), QString());
	session->downloaderTaskFinished(
	) | rpl::filter([=] {
		const auto session = job->session.get();
		return !session
			|| (job->media->loaded()
				|| !session->data().document(job->documentId)->loading());
	}) | rpl::take(1) | rpl::on_next([=] {
		StartBackend(job);
	}, job->lifetime);
}

void CancelBackendTranscription(not_null<HistoryItem*> item) {
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
	job->cancelled->store(true);
	if (job->cancelToken) {
		BackendEngine().Cancel(job->cancelToken);
	}
	job->lifetime.destroy();
	QFile::remove(job->inputPath);
	QFile::remove(job->wavPath);
}

} // namespace AyuRecognition
