// This is the source code of freshGram.
#pragma once

#include <QtCore/QString>

#include <string>
#include <vector>

namespace AyuRecognition {

enum class EngineProvider {
	Telegram = 0,
	WhisperLocal = 1,
	Groq = 2,
	Gemini = 3,
	Custom = 4,
};

struct TranscriptionConfig {
	EngineProvider provider = EngineProvider::Telegram;
	QString groqKey;
	QString groqModel;
	QString geminiKey;
	QString geminiModel;
	QString customBase;
	QString customModel;
	QString customKey;
	QString language;
	QString prompt;
	QString enginePath;
	QString localModel;
	int threads = 4;
	bool useGpu = true;
	EngineProvider shadow = EngineProvider::Telegram;
	EngineProvider cleanupProvider = EngineProvider::Telegram;
	QString cleanupModel;
	QString proxyUrl;
	QString networkError;
};

[[nodiscard]] std::string BuildVoiceAsrConfigJson(
	const TranscriptionConfig &config);
[[nodiscard]] std::string BuildModelDiscoveryJson(
	const TranscriptionConfig &config,
	bool force);
[[nodiscard]] QString DescribeEngineError(int code, const QString &detail);
[[nodiscard]] bool WriteWavFile(
	const QString &path,
	const std::vector<float> &samples);
[[nodiscard]] QString UniqueTempPath(const QString &suffix);
void InitializeTranscriptionTemporaryFiles();
[[nodiscard]] QString TranscriptionTemporaryPath();

} // namespace AyuRecognition
