// This is the source code of freshGram.
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <string>
#include <utility>
#include <vector>

namespace AyuRecognition {

struct HttpRequest {
	QString method;
	QString url;
	QString contentType;
	std::vector<std::pair<QString, QString>> headers;
	QByteArray body;
};

struct GroqSegment {
	float start = 0;
	float end = 0;
	QString text;
};

struct GroqTranscript {
	QString text;
	std::vector<GroqSegment> segments;
	QString requestId;
	bool ok = false;
};

[[nodiscard]] QString InferAudioMime(
	const QString &filename,
	const QString &mime);
[[nodiscard]] QString GeminiLanguageCode(const QString &language);
[[nodiscard]] HttpRequest BuildGroqRequest(
	const QString &base,
	const QString &key,
	const QString &model,
	const QString &language,
	float temperature,
	const QString &filename,
	const QString &mime,
	const QByteArray &fileBytes);
[[nodiscard]] GroqTranscript ParseGroqResponse(const QByteArray &body);
[[nodiscard]] HttpRequest BuildGeminiUploadStart(
	const QString &base,
	const QString &key,
	int contentLength,
	const QString &mime);
[[nodiscard]] HttpRequest BuildGeminiUploadFinish(
	const QString &uploadUrl,
	const QString &mime,
	const QByteArray &bytes);
[[nodiscard]] HttpRequest BuildGeminiInteraction(
	const QString &base,
	const QString &key,
	const QString &model,
	const QString &uri,
	const QString &mime,
	const QString &language,
	const QString &prompt);
[[nodiscard]] QString ExtractGeminiText(const QByteArray &body);
[[nodiscard]] HttpRequest BuildGeminiDelete(
	const QString &base,
	const QString &key,
	const QString &name);
[[nodiscard]] QString DescribeCloudError(
	int httpStatus,
	const QString &bodySnippet);

} // namespace AyuRecognition
