// This is the source code of freshGram.
#pragma once

#include <QtCore/QLibrary>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <string>

namespace AyuRecognition {

struct EngineResult {
	bool ok = false;
	QString text;
	QString error;
	int code = 0;
};

class EngineLibrary {
public:
	EngineLibrary() = default;
	EngineLibrary(const EngineLibrary&) = delete;
	EngineLibrary &operator=(const EngineLibrary&) = delete;
	~EngineLibrary();

	[[nodiscard]] static bool IsAvailable(const QString &customPath);
	[[nodiscard]] static QStringList SearchPaths(const QString &customPath);
	bool Load(const QString &customPath);
	[[nodiscard]] bool IsLoaded() const;
	[[nodiscard]] EngineResult TranscribeBlocking(
		const std::string &configJson,
		const QString &audioPath,
		const QString &filename,
		const QString &mime,
		void *cancel);
	void *CreateCancel();
	void Cancel(void *token);
	void FreeCancel(void *token);

private:
	using EngineNew = void* (*)();
	using EngineFree = void (*)(void*);
	using CancelNew = void* (*)();
	using CancelFree = void (*)(void*);
	using CancelCall = void (*)(void*);
	using Transcribe = void* (*)(void*, const char*, const char*, const char*, const char*, void*);
	using TranscriptFree = void (*)(void*);
	using TranscriptText = const char* (*)(void*);
	using LastCode = int (*)();
	using LastMessage = const char* (*)();

	QLibrary _library;
	void *_engine = nullptr;
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
	LastCode _lastCode = nullptr;
	LastMessage _lastMessage = nullptr;

	void Unload();
	[[nodiscard]] bool ResolveAll();

};

} // namespace AyuRecognition
