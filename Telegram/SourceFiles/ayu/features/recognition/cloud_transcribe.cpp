// This is the source code of freshGram.
#include "ayu/features/recognition/cloud_transcribe.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <array>
#include <cstdio>

namespace AyuRecognition {
namespace {

constexpr auto kBoundary = "----FreshGramVoiceBoundary7MA4YWxkTrZu0G";
constexpr auto kGroqTranscriptions = "/audio/transcriptions";

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

void AddFormField(
		QByteArray &to,
		const QByteArray &name,
		const QByteArray &value) {
	to += "--" + QByteArray(kBoundary) + "\r\n";
	to += "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
	to += value + "\r\n";
}

QString Trimmed(const QJsonValue &value) {
	return value.toString().trimmed();
}

void CollectGeminiText(const QJsonValue &value, QStringList &parts) {
	if (value.isObject()) {
		const auto object = value.toObject();
		if (object.value(u"type"_q).toString() == u"text"_q) {
			const auto text = object.value(u"text"_q).toString().trimmed();
			if (!text.isEmpty()) {
				parts.push_back(text);
			}
		}
		for (const auto &nested : object) {
			CollectGeminiText(nested, parts);
		}
	} else if (value.isArray()) {
		for (const auto &nested : value.toArray()) {
			CollectGeminiText(nested, parts);
		}
	}
}

} // namespace

QString InferAudioMime(const QString &filename, const QString &mime) {
	if (mime.startsWith(u"audio/"_q)) {
		return mime;
	}
	const auto suffix = filename.section('.', -1).toLower();
	if (suffix == u"ogg"_q || suffix == u"oga"_q) {
		return u"audio/ogg"_q;
	} else if (suffix == u"opus"_q) {
		return u"audio/opus"_q;
	} else if (suffix == u"mp3"_q) {
		return u"audio/mp3"_q;
	} else if (suffix == u"m4a"_q) {
		return u"audio/m4a"_q;
	} else if (suffix == u"wav"_q) {
		return u"audio/wav"_q;
	} else if (suffix == u"flac"_q) {
		return u"audio/flac"_q;
	} else if (suffix == u"webm"_q) {
		return u"audio/webm"_q;
	}
	return QString();
}

QString GeminiLanguageCode(const QString &language) {
	const auto trimmed = language.trimmed();
	const auto code = trimmed.toLower();
	if (code.isEmpty() || code == u"auto"_q) {
		return QString();
	} else if (code == u"ru"_q) {
		return u"ru-RU"_q;
	} else if (code == u"en"_q) {
		return u"en-US"_q;
	} else if (code == u"uk"_q) {
		return u"uk-UA"_q;
	} else if (code == u"be"_q) {
		return u"be-BY"_q;
	}
	return trimmed;
}

HttpRequest BuildGroqRequest(
		const QString &base,
		const QString &key,
		const QString &model,
		const QString &language,
		float temperature,
		const QString &filename,
		const QString &mime,
		const QByteArray &fileBytes) {
	auto request = HttpRequest();
	request.method = u"POST"_q;
	request.url = base + QString::fromLatin1(kGroqTranscriptions);
	request.contentType = u"multipart/form-data; boundary="_q
		+ QString::fromLatin1(kBoundary);
	request.headers.emplace_back(
		u"Authorization"_q,
		u"Bearer "_q + key);
	auto body = QByteArray();
	AddFormField(body, "model", model.toUtf8());
	AddFormField(body, "response_format", "verbose_json");
	if (!language.trimmed().isEmpty() && language.trimmed() != u"auto"_q) {
		AddFormField(body, "language", language.trimmed().toUtf8());
	}
	AddFormField(body, "temperature", QString::number(double(temperature)).toUtf8());
	AddFormField(body, "timestamp_granularities[]", "segment");
	body += "--" + QByteArray(kBoundary) + "\r\n";
	body += "Content-Disposition: form-data; name=\"file\"; filename=\""
		+ filename.toUtf8()
		+ "\"\r\n";
	body += "Content-Type: "
		+ (mime.isEmpty() ? QByteArray("application/octet-stream") : mime.toUtf8())
		+ "\r\n\r\n";
	body += fileBytes + "\r\n";
	body += "--" + QByteArray(kBoundary) + "--\r\n";
	request.body = body;
	return request;
}

GroqTranscript ParseGroqResponse(const QByteArray &body) {
	auto result = GroqTranscript();
	const auto document = QJsonDocument::fromJson(body);
	if (!document.isObject()) {
		return result;
	}
	const auto object = document.object();
	result.text = Trimmed(object.value(u"text"_q));
	for (const auto &entry : object.value(u"segments"_q).toArray()) {
		const auto segment = entry.toObject();
		auto part = GroqSegment();
		part.start = float(segment.value(u"start"_q).toDouble());
		part.end = float(segment.value(u"end"_q).toDouble());
		part.text = Trimmed(segment.value(u"text"_q));
		if (!part.text.isEmpty()) {
			result.segments.push_back(part);
		}
	}
	result.requestId = object.value(u"x_groq"_q).toObject().value(u"id"_q).toString();
	result.ok = true;
	return result;
}

HttpRequest BuildGeminiUploadStart(
		const QString &base,
		const QString &key,
		int contentLength,
		const QString &mime) {
	auto request = HttpRequest();
	request.method = u"POST"_q;
	request.url = base + u"/upload/v1beta/files"_q;
	request.contentType = u"application/json"_q;
	request.headers.emplace_back(u"x-goog-api-key"_q, key);
	request.headers.emplace_back(u"X-Goog-Upload-Protocol"_q, u"resumable"_q);
	request.headers.emplace_back(u"X-Goog-Upload-Command"_q, u"start"_q);
	request.headers.emplace_back(
		u"X-Goog-Upload-Header-Content-Length"_q,
		QString::number(contentLength));
	request.headers.emplace_back(u"X-Goog-Upload-Header-Content-Type"_q, mime);
	auto json = std::string("{\"file\":{\"display_name\":\"freshgram\"}}");
	request.body = QByteArray(json.data(), int(json.size()));
	return request;
}

HttpRequest BuildGeminiUploadFinish(
		const QString &uploadUrl,
		const QString &mime,
		const QByteArray &bytes) {
	auto request = HttpRequest();
	request.method = u"POST"_q;
	request.url = uploadUrl;
	request.contentType = mime;
	request.headers.emplace_back(u"X-Goog-Upload-Offset"_q, u"0"_q);
	request.headers.emplace_back(u"X-Goog-Upload-Command"_q, u"upload, finalize"_q);
	request.body = bytes;
	return request;
}

HttpRequest BuildGeminiInteraction(
		const QString &base,
		const QString &key,
		const QString &model,
		const QString &uri,
		const QString &mime,
		const QString &language,
		const QString &prompt) {
	auto request = HttpRequest();
	request.method = u"POST"_q;
	request.url = base + u"/v1beta/interactions"_q;
	request.contentType = u"application/json"_q;
	request.headers.emplace_back(u"x-goog-api-key"_q, key);
	auto json = std::string("{\"model\":");
	WriteJsonString(json, model);
	json += ",\"input\":[";
	const auto clean = prompt.trimmed();
	if (!clean.isEmpty()) {
		json += "{\"type\":\"text\",\"text\":";
		WriteJsonString(json, clean);
		json += "},";
	}
	json += "{\"type\":\"audio\",\"uri\":";
	WriteJsonString(json, uri);
	json += ",\"mime_type\":";
	WriteJsonString(json, mime);
	json += "}],\"generation_config\":{\"transcription_config\":{";
	json += "\"language_codes\":[";
	const auto code = GeminiLanguageCode(language);
	if (!code.isEmpty()) {
		WriteJsonString(json, code);
	}
	json += "],\"mode\":{\"type\":\"verbatim\"}}}}";
	request.body = QByteArray(json.data(), int(json.size()));
	return request;
}

QString ExtractGeminiText(const QByteArray &body) {
	const auto document = QJsonDocument::fromJson(body);
	if (!document.isObject()) {
		return QString();
	}
	const auto object = document.object();
	const auto direct = object.value(u"output_text"_q).toString().trimmed();
	if (!direct.isEmpty()) {
		return direct;
	}
	auto parts = QStringList();
	if (object.contains(u"steps"_q)) {
		CollectGeminiText(object.value(u"steps"_q), parts);
	}
	if (parts.isEmpty() && object.contains(u"outputs"_q)) {
		CollectGeminiText(object.value(u"outputs"_q), parts);
	}
	return parts.join(u"\n"_q);
}

HttpRequest BuildGeminiDelete(
		const QString &base,
		const QString &key,
		const QString &name) {
	auto request = HttpRequest();
	request.method = u"DELETE"_q;
	request.url = base + u"/v1beta/"_q + name;
	request.headers.emplace_back(u"x-goog-api-key"_q, key);
	return request;
}

QString DescribeCloudError(int httpStatus, const QString &bodySnippet) {
	const auto snippet = bodySnippet.trimmed().mid(0, 300);
	switch (httpStatus) {
	case 0: return u"Network error."_q;
	case 401:
	case 403: return u"Bad API key (HTTP %1)."_q.arg(httpStatus);
	case 404: return u"Not found (HTTP 404)."_q;
	case 429: return u"Rate limited, retry later."_q;
	}
	if (httpStatus >= 500) {
		return u"Server error (HTTP %1)."_q.arg(httpStatus);
	}
	return snippet.isEmpty()
		? u"HTTP error %1."_q.arg(httpStatus)
		: u"HTTP error %1: %2"_q.arg(httpStatus).arg(snippet);
}

} // namespace AyuRecognition
