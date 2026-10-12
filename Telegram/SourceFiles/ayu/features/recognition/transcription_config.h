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
};

[[nodiscard]] std::string BuildVoiceAsrConfigJson(
	const TranscriptionConfig &config);
[[nodiscard]] QString DescribeEngineError(int code, const QString &detail);
[[nodiscard]] bool WriteWavFile(
	const QString &path,
	const std::vector<float> &samples);
[[nodiscard]] QString UniqueTempPath(const QString &suffix);

} // namespace AyuRecognition
