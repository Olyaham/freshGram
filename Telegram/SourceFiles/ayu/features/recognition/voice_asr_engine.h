// This is the source code of freshGram.
#pragma once

#include "voice_asr.h"

#include <QtCore/QLibrary>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <atomic>
#include <mutex>
#include <string>

namespace AyuRecognition {

struct EngineResult {
	bool ok = false;
	QString text;
	QString error;
	int code = 0;
	QString details;
};

class EngineLibrary {
public:
	EngineLibrary() = default;
	EngineLibrary(const EngineLibrary&) = delete;
	EngineLibrary &operator=(const EngineLibrary&) = delete;
	~EngineLibrary();

	[[nodiscard]] static bool IsAvailable(
		const QString &customPath,
		bool local = false);
	[[nodiscard]] static QStringList SearchPaths(
		const QString &customPath,
		bool local = false);
	bool Load(const QString &customPath, bool local = false);
	[[nodiscard]] bool IsLoaded() const;
	[[nodiscard]] unsigned int Capabilities() const;
	[[nodiscard]] EngineResult TranscribeBlocking(
		const std::string &configJson,
		const QString &audioPath,
		const QString &filename,
		const QString &mime);
	void CancelCurrent();
	[[nodiscard]] EngineResult DiscoverModelsBlocking(const std::string &requestJson);
	[[nodiscard]] EngineResult GenerateBlocking(const std::string &requestJson);

private:
	using EngineNew = decltype(&vasr_engine_new);
	using EngineFree = decltype(&vasr_engine_free);
	using CancelNew = decltype(&vasr_cancel_new);
	using CancelFree = decltype(&vasr_cancel_free);
	using CancelCall = decltype(&vasr_cancel);
	using Transcribe = decltype(&vasr_transcribe);
	using TranscriptFree = decltype(&vasr_transcript_free);
	using TranscriptText = decltype(&vasr_transcript_text);
	using TranscriptDetails = decltype(&vasr_transcript_details);
	using JsonCall = decltype(&vasr_models);
	using StringFree = decltype(&vasr_string_free);
	using LastCode = decltype(&vasr_last_error_code);
	using LastMessage = decltype(&vasr_last_error_message);

	QLibrary _library;
	VasrEngine *_engine = nullptr;
	VasrCancel *_cancel = nullptr;
	std::mutex _cancelMutex;
	std::atomic<bool> _cancelled = false;
	unsigned int _capabilities = 0;
	bool _loaded = false;
	QString _path;
	EngineNew _engineNew = nullptr;
	EngineFree _engineFree = nullptr;
	CancelNew _cancelNew = nullptr;
	CancelFree _cancelFree = nullptr;
	CancelCall _cancelCall = nullptr;
	Transcribe _transcribe = nullptr;
	TranscriptFree _transcriptFree = nullptr;
	TranscriptText _transcriptText = nullptr;
	TranscriptDetails _transcriptDetails = nullptr;
	JsonCall _models = nullptr;
	JsonCall _generate = nullptr;
	StringFree _stringFree = nullptr;
	LastCode _lastCode = nullptr;
	LastMessage _lastMessage = nullptr;

	void Unload();
	[[nodiscard]] bool ResolveAll();
	[[nodiscard]] EngineResult CallJsonBlocking(
		JsonCall call,
		const std::string &requestJson);

};

} // namespace AyuRecognition
