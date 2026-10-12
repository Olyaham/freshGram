// This is the source code of freshGram.
#include "ayu/features/recognition/voice_asr_engine.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>

namespace AyuRecognition {

EngineLibrary::~EngineLibrary() {
	Unload();
}

void EngineLibrary::Unload() {
	if (_engine && _engineFree) {
		_engineFree(_engine);
		_engine = nullptr;
	}
	if (_library.isLoaded()) {
		_library.unload();
	}
	_loaded = false;
	_path = QString();
}

QStringList EngineLibrary::SearchPaths(const QString &customPath) {
	auto paths = QStringList();
	if (!customPath.isEmpty()) {
		paths.push_back(customPath);
	}
	const auto appDir = QCoreApplication::applicationDirPath();
	paths.push_back(appDir + u"/voice-asr/voice_asr"_q);
	paths.push_back(u"./tdata/voice-asr/voice_asr"_q);
	return paths;
}

bool EngineLibrary::IsAvailable(const QString &customPath) {
	for (const auto &path : SearchPaths(customPath)) {
		if (QLibrary::isLibrary(path)) {
			return true;
		}
	}
	return false;
}

bool EngineLibrary::ResolveAll() {
	_engineNew = reinterpret_cast<EngineNew>(_library.resolve("vasr_engine_new"));
	_engineFree = reinterpret_cast<EngineFree>(_library.resolve("vasr_engine_free"));
	_cancelNew = reinterpret_cast<CancelNew>(_library.resolve("vasr_cancel_new"));
	_cancelFree = reinterpret_cast<CancelFree>(_library.resolve("vasr_cancel_free"));
	_cancelCall = reinterpret_cast<CancelCall>(_library.resolve("vasr_cancel"));
	_transcribe = reinterpret_cast<Transcribe>(_library.resolve("vasr_transcribe"));
	_transcriptFree = reinterpret_cast<TranscriptFree>(_library.resolve("vasr_transcript_free"));
	_transcriptText = reinterpret_cast<TranscriptText>(_library.resolve("vasr_transcript_text"));
	_lastCode = reinterpret_cast<LastCode>(_library.resolve("vasr_last_error_code"));
	_lastMessage = reinterpret_cast<LastMessage>(_library.resolve("vasr_last_error_message"));
	return _engineNew
		&& _engineFree
		&& _cancelNew
		&& _cancelFree
		&& _cancelCall
		&& _transcribe
		&& _transcriptFree
		&& _transcriptText
		&& _lastCode
		&& _lastMessage;
}

bool EngineLibrary::Load(const QString &customPath) {
	for (const auto &path : SearchPaths(customPath)) {
		if (_loaded && _path == path) {
			return true;
		}
		Unload();
		_library.setFileName(path);
		if (!_library.load() || !ResolveAll()) {
			Unload();
			continue;
		}
		_engine = _engineNew();
		if (!_engine) {
			Unload();
			continue;
		}
		_loaded = true;
		_path = path;
		return true;
	}
	return false;
}

bool EngineLibrary::IsLoaded() const {
	return _loaded;
}

void *EngineLibrary::CreateCancel() {
	return (_loaded && _cancelNew) ? _cancelNew() : nullptr;
}

void EngineLibrary::Cancel(void *token) {
	if (token && _cancelCall) {
		_cancelCall(token);
	}
}

void EngineLibrary::FreeCancel(void *token) {
	if (token && _cancelFree) {
		_cancelFree(token);
	}
}

EngineResult EngineLibrary::TranscribeBlocking(
		const std::string &configJson,
		const QString &audioPath,
		const QString &filename,
		const QString &mime,
		void *cancel) {
	auto result = EngineResult();
	if (!IsLoaded()) {
		result.code = -1;
		result.error = u"Engine library not found."_q;
		return result;
	}
	const auto path = audioPath.toUtf8();
	const auto name = filename.toUtf8();
	const auto type = mime.toUtf8();
	const auto transcript = _transcribe(
		_engine,
		configJson.c_str(),
		path.constData(),
		name.constData(),
		type.isEmpty() ? nullptr : type.constData(),
		cancel);
	if (!transcript) {
		result.code = _lastCode();
		result.error = QString::fromUtf8(_lastMessage());
		return result;
	}
	result.ok = true;
	result.text = QString::fromUtf8(_transcriptText(transcript));
	_transcriptFree(transcript);
	return result;
}

} // namespace AyuRecognition
