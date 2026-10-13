// This is the source code of freshGram.
#include "ayu/features/recognition/transcription_config.h"

#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QtEndian>

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
	json += config.useGpu ? ",\"use_gpu\":true}" : ",\"use_gpu\":false}";
	if (config.shadow != EngineProvider::Telegram) {
		json += ",\"shadow\":\"";
		json += ProviderName(config.shadow);
		json += '"';
	}
	if (config.cleanupProvider != EngineProvider::Telegram) {
		json += ",\"cleanup\":{\"provider\":\"";
		json += ProviderName(config.cleanupProvider);
		json += "\",\"model\":";
		WriteJsonString(json, config.cleanupModel);
		json += '}';
	}
	json += '}';
	return json;
}

std::string BuildModelDiscoveryJson(
		const TranscriptionConfig &config,
		bool force) {
	const auto gemini = config.provider == EngineProvider::Gemini;
	const auto custom = config.provider == EngineProvider::Custom;
	const auto key = gemini ? config.geminiKey
		: custom ? config.customKey : config.groqKey;
	const auto base = gemini ? QString::fromUtf8(kGeminiBase)
		: custom ? config.customBase : QString::fromUtf8(kGroqBase);
	return QJsonDocument(QJsonObject{
		{ u"options"_q, QJsonObject{
			{ u"provider"_q, QString::fromUtf8(ProviderName(config.provider)) },
			{ u"protocol"_q, gemini ? u"gemini"_q : u"openai_compatible"_q },
			{ u"base_url"_q, base },
			{ u"secret_name"_q, key.isEmpty() ? QJsonValue() : QJsonValue(u"API_KEY"_q) },
			{ u"metadata_url"_q, custom ? QJsonValue()
				: QJsonValue(u"https://models.dev/api.json"_q) },
			{ u"metadata_provider"_q, custom ? QJsonValue()
				: QJsonValue(gemini ? u"google"_q : u"groq"_q) },
		} },
		{ u"secrets"_q, QJsonObject{ { u"API_KEY"_q, key } } },
		{ u"force"_q, force },
	}).toJson(QJsonDocument::Compact).toStdString();
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
	for (auto i = size_t(0); i != samples.size(); ++i) {
		const auto value = std::isfinite(samples[i]) ? samples[i] : 0.f;
		const auto clamped = std::clamp(value, -1.f, 1.f);
		qToLittleEndian(qint16(std::lround(clamped * 32767.f)), pcm.data() + 2 * i);
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
