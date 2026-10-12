// This is the source code of freshGram.
#include "ayu/features/recognition/transcription_config.h"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFile>

#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace AyuRecognition {
namespace {

constexpr auto kGroqBase = "https://api.groq.com/openai/v1";
constexpr auto kGeminiBase = "https://generativelanguage.googleapis.com";
constexpr auto kGroqTimeout = 120;
constexpr auto kGeminiTimeout = 180;
constexpr auto kGeminiTotalTimeout = 420;
constexpr auto kCustomTimeout = 120;
constexpr auto kSampleRate = 16000;

void EscapeJsonInto(std::string &to, const QString &value) {
	const auto utf8 = value.toUtf8();
	for (const auto c : utf8) {
		switch (c) {
		case '"': to += "\\\""; break;
		case '\\': to += "\\\\"; break;
		case '\n': to += "\\n"; break;
		case '\r': to += "\\r"; break;
		case '\t': to += "\\t"; break;
		default:
			if (c >= 0 && c < 0x20) {
				auto buffer = std::array<char, 8>();
				snprintf(buffer.data(), buffer.size(), "\\u%04x", c);
				to += buffer.data();
			} else {
				to += c;
			}
		}
	}
}

void WriteJsonString(std::string &to, const QString &value) {
	to += '"';
	EscapeJsonInto(to, value);
	to += '"';
}

const char *ProviderName(EngineProvider provider) {
	switch (provider) {
	case EngineProvider::Groq: return "groq";
	case EngineProvider::Gemini: return "gemini";
	case EngineProvider::Custom: return "custom";
	case EngineProvider::WhisperLocal: return "local";
	case EngineProvider::Telegram: return "telegram";
	}
	return "groq";
}

} // namespace

std::string BuildVoiceAsrConfigJson(const TranscriptionConfig &config) {
	auto json = std::string("{\"provider\":\"");
	json += ProviderName(config.provider);
	json += "\",\"fallback\":null,\"language\":";
	WriteJsonString(
		json,
		config.language.isEmpty() ? QString(u"auto"_q) : config.language);
	json += ",\"groq\":{\"api_base\":\"";
	json += kGroqBase;
	json += "\",\"api_key\":";
	WriteJsonString(json, config.groqKey);
	json += ",\"model\":";
	WriteJsonString(json, config.groqModel);
	json += ",\"temperature\":0.0,\"timeout_secs\":";
	json += std::to_string(kGroqTimeout);
	json += "},\"gemini\":{\"api_base\":\"";
	json += kGeminiBase;
	json += "\",\"api_key\":";
	WriteJsonString(json, config.geminiKey);
	json += ",\"model\":";
	WriteJsonString(json, config.geminiModel);
	json += ",\"prompt\":";
	WriteJsonString(json, config.prompt);
	json += ",\"timeout_secs\":";
	json += std::to_string(kGeminiTimeout);
	json += ",\"total_deadline_secs\":";
	json += std::to_string(kGeminiTotalTimeout);
	json += "},\"custom\":{\"api_base\":";
	WriteJsonString(json, config.customBase);
	json += ",\"api_key\":";
	WriteJsonString(json, config.customKey);
	json += ",\"model\":";
	WriteJsonString(json, config.customModel);
	json += ",\"temperature\":0.0,\"timeout_secs\":";
	json += std::to_string(kCustomTimeout);
	json += "},\"local\":{\"model_path\":";
	WriteJsonString(json, config.localModel);
	json += ",\"n_threads\":";
	json += std::to_string(config.threads);
	json += ",\"use_gpu\":true}}";
	return json;
}

QString DescribeEngineError(int code, const QString &detail) {
	switch (code) {
	case 8: return u"Cancelled."_q;
	case 7: return u"Timed out."_q;
	case 5: return u"Network error: %1"_q.arg(detail);
	case 6: return u"API error: %1"_q.arg(detail);
	case 9: return u"Engine failed: %1"_q.arg(detail);
	case 10: return u"Not supported: %1"_q.arg(detail);
	case 4: return u"File error: %1"_q.arg(detail);
	case 3: return u"Bad response: %1"_q.arg(detail);
	}
	return detail.isEmpty()
		? u"Error %1."_q.arg(code)
		: u"Error %1: %2"_q.arg(code).arg(detail);
}

bool WriteWavFile(const QString &path, const std::vector<float> &samples) {
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	const auto dataSize = quint32(samples.size() * 2);
	auto header = QByteArray(44, 0);
	{
		auto stream = QDataStream(&header, QIODevice::WriteOnly);
		stream.setByteOrder(QDataStream::LittleEndian);
		stream.writeRawData("RIFF", 4);
		stream << quint32(36 + dataSize);
		stream.writeRawData("WAVE", 4);
		stream.writeRawData("fmt ", 4);
		stream << quint32(16) << quint16(1) << quint16(1) << quint32(kSampleRate)
			<< quint32(kSampleRate * 2) << quint16(2) << quint16(16);
		stream.writeRawData("data", 4);
		stream << dataSize;
	}
	if (file.write(header) != 44) {
		return false;
	}
	auto pcm = QByteArray();
	pcm.resize(qsizetype(dataSize));
	const auto channels = reinterpret_cast<qint16*>(pcm.data());
	for (auto i = size_t(0); i != samples.size(); ++i) {
		const auto clamped = std::max(-1.f, std::min(1.f, samples[i]));
		channels[i] = qint16(std::lround(clamped * 32767.f));
	}
	return file.write(pcm) == qint64(pcm.size());
}

QString UniqueTempPath(const QString &suffix) {
	static auto counter = std::atomic<int>(0);
	return QDir::tempPath()
		+ u"/freshgram-voice-"_q
		+ QString::number(QCoreApplication::applicationPid())
		+ u"-"_q
		+ QString::number(++counter)
		+ suffix;
}

} // namespace AyuRecognition
