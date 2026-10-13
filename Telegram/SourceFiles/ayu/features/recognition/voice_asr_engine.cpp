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
	_capabilities = 0;
	_path = QString();
}

QStringList EngineLibrary::SearchPaths(const QString &customPath, bool local) {
	auto paths = QStringList();
	if (!customPath.isEmpty()) {
		return { QFileInfo(customPath).absoluteFilePath() };
	}
	const auto appDir = QCoreApplication::applicationDirPath();
#if defined Q_OS_WIN
	const auto name = u"voice_asr.dll"_q;
#elif defined Q_OS_MAC
	const auto name = u"libvoice_asr.dylib"_q;
#else
	const auto name = u"libvoice_asr.so"_q;
#endif
	const auto flavor = local ? u"local/"_q : u"cloud/"_q;
	paths.push_back(appDir + u"/voice-asr/"_q + flavor + name);
	paths.push_back(QDir::currentPath() + u"/tdata/voice-asr/"_q + flavor + name);
#ifdef FRESHGRAM_ASR_SYSTEM_DIR
	paths.push_back(QString::fromUtf8(FRESHGRAM_ASR_SYSTEM_DIR) + u"/"_q + flavor + name);
#endif
	if (!local) {
		paths.push_back(appDir + u"/voice-asr/"_q + name);
		paths.push_back(QDir::currentPath() + u"/tdata/voice-asr/"_q + name);
	}
	return paths;
}

bool EngineLibrary::IsAvailable(const QString &customPath, bool local) {
	for (const auto &path : SearchPaths(customPath, local)) {
		const auto info = QFileInfo(path);
		if (info.isFile() && info.isReadable() && QLibrary::isLibrary(path)) {
			return true;
		}
	}
	return false;
}

bool EngineLibrary::ResolveAll() {
	const auto abi = reinterpret_cast<decltype(&vasr_abi_version)>(
		_library.resolve("vasr_abi_version"));
	const auto capabilities = reinterpret_cast<decltype(&vasr_capabilities)>(
		_library.resolve("vasr_capabilities"));
	if (!abi || abi() != 1 || !capabilities) {
		return false;
	}
	_capabilities = capabilities();
	_engineNew = reinterpret_cast<EngineNew>(_library.resolve("vasr_engine_new"));
	_engineFree = reinterpret_cast<EngineFree>(_library.resolve("vasr_engine_free"));
	_cancelNew = reinterpret_cast<CancelNew>(_library.resolve("vasr_cancel_new"));
	_cancelFree = reinterpret_cast<CancelFree>(_library.resolve("vasr_cancel_free"));
	_cancelCall = reinterpret_cast<CancelCall>(_library.resolve("vasr_cancel"));
	_transcribe = reinterpret_cast<Transcribe>(_library.resolve("vasr_transcribe"));
	_transcriptFree = reinterpret_cast<TranscriptFree>(_library.resolve("vasr_transcript_free"));
	_transcriptText = reinterpret_cast<TranscriptText>(_library.resolve("vasr_transcript_text"));
	_transcriptDetails = reinterpret_cast<TranscriptDetails>(_library.resolve("vasr_transcript_details"));
	_models = reinterpret_cast<JsonCall>(_library.resolve("vasr_models"));
	_generate = reinterpret_cast<JsonCall>(_library.resolve("vasr_generate"));
	_stringFree = reinterpret_cast<StringFree>(_library.resolve("vasr_string_free"));
	if ((_capabilities & VASR_CAP_MODELS) && (!_models || !_stringFree)) {
		return false;
	}
	if ((_capabilities & VASR_CAP_LLM) && (!_generate || !_stringFree)) {
		return false;
	}
	if ((_capabilities & VASR_CAP_PROCESSING) && !_transcriptDetails) {
		return false;
	}
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

bool EngineLibrary::Load(const QString &customPath, bool local) {
	const auto required = local ? VASR_CAP_LOCAL : VASR_CAP_NETWORK;
	for (const auto &path : SearchPaths(customPath, local)) {
		if (_loaded && _path == path && (_capabilities & required)) {
			return true;
		}
		Unload();
		_library.setLoadHints(local
			? QLibrary::LoadHints()
			: QLibrary::LoadHints(QLibrary::PreventUnloadHint));
		_library.setFileName(path);
		if (!_library.load() || !ResolveAll()) {
			Unload();
			continue;
		}
		if (!(_capabilities & required)) {
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

unsigned int EngineLibrary::Capabilities() const {
	return _capabilities;
}

void EngineLibrary::CancelCurrent() {
	_cancelled.store(true);
	const auto lock = std::lock_guard(_cancelMutex);
	if (_cancel) {
		_cancelCall(_cancel);
	}
}

EngineResult EngineLibrary::TranscribeBlocking(
		const std::string &configJson,
		const QString &audioPath,
		const QString &filename,
		const QString &mime) {
	auto result = EngineResult();
	if (!IsLoaded()) {
		result.code = -1;
		result.error = u"Engine library not found."_q;
		return result;
	}
	{
		const auto lock = std::lock_guard(_cancelMutex);
		_cancel = _cancelNew();
		if (!_cancel) {
			result.code = VASR_INTERNAL;
			result.error = u"Could not create the cancellation token."_q;
			return result;
		}
		if (_cancelled.load()) {
			_cancelCall(_cancel);
		}
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
		_cancel);
	if (!transcript) {
		result.code = _lastCode();
		result.error = QString::fromUtf8(_lastMessage());
	} else {
		result.ok = true;
		result.text = QString::fromUtf8(_transcriptText(transcript));
		if ((_capabilities & VASR_CAP_PROCESSING) && _transcriptDetails) {
			result.details = QString::fromUtf8(_transcriptDetails(transcript));
		}
		_transcriptFree(transcript);
	}
	{
		const auto lock = std::lock_guard(_cancelMutex);
		_cancelFree(_cancel);
		_cancel = nullptr;
	}
	return result;
}

EngineResult EngineLibrary::DiscoverModelsBlocking(const std::string &requestJson) {
	return CallJsonBlocking((_capabilities & VASR_CAP_MODELS) ? _models : nullptr, requestJson);
}

EngineResult EngineLibrary::GenerateBlocking(const std::string &requestJson) {
	return CallJsonBlocking((_capabilities & VASR_CAP_LLM) ? _generate : nullptr, requestJson);
}

EngineResult EngineLibrary::CallJsonBlocking(
		JsonCall call,
		const std::string &requestJson) {
	auto result = EngineResult();
	if (!IsLoaded() || !call || !_stringFree) {
		result.code = VASR_UNSUPPORTED;
		result.error = u"The AI module does not support this operation."_q;
		return result;
	}
	{
		const auto lock = std::lock_guard(_cancelMutex);
		_cancel = _cancelNew();
		if (!_cancel) {
			result.code = VASR_INTERNAL;
			return result;
		}
		if (_cancelled.load()) _cancelCall(_cancel);
	}
	const auto value = call(_engine, requestJson.c_str(), _cancel);
	if (value) {
		result.ok = true;
		result.text = QString::fromUtf8(value);
		_stringFree(value);
	} else {
		result.code = _lastCode();
		result.error = QString::fromUtf8(_lastMessage());
	}
	{
		const auto lock = std::lock_guard(_cancelMutex);
		_cancelFree(_cancel);
		_cancel = nullptr;
	}
	return result;
}

} // namespace AyuRecognition
