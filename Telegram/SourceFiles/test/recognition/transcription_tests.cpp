#include "ayu/features/recognition/cloud_transcribe.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>

#include <iostream>
#include <stdexcept>

namespace {

void Check(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

const auto kBoundary = QString::fromUtf8(
	"----FreshGramVoiceBoundary7MA4YWxkTrZu0G");

void CaseGroqRequest() {
	const auto request = AyuRecognition::BuildGroqRequest(
		QString::fromUtf8("https://api.groq.com/openai/v1"),
		QString::fromUtf8("K"),
		QString::fromUtf8("whisper-large-v3-turbo"),
		QString::fromUtf8("ru"),
		0.0f,
		QString::fromUtf8("a.ogg"),
		QString::fromUtf8("audio/ogg"),
		QByteArray("DATA"));
	Check(request.method == QString::fromUtf8("POST"), "groq method");
	Check(
		request.url
			== QString::fromUtf8("https://api.groq.com/openai/v1/audio/transcriptions"),
		"groq url");
	Check(
		request.contentType
			== QString::fromUtf8("multipart/form-data; boundary=") + kBoundary,
		"groq content type");
	Check(request.headers.size() == 1, "groq headers");
	Check(request.headers[0].first == QString::fromUtf8("Authorization"), "auth name");
	Check(request.headers[0].second == QString::fromUtf8("Bearer K"), "auth value");
	auto want = QByteArray();
	const auto part = [&](const char *name, const QByteArray &value) {
		want += "--" + kBoundary.toUtf8() + "\r\n";
		want += "Content-Disposition: form-data; name=\"" + QByteArray(name) + "\"\r\n\r\n";
		want += value + "\r\n";
	};
	part("model", "whisper-large-v3-turbo");
	part("response_format", "verbose_json");
	part("language", "ru");
	part("temperature", "0");
	part("timestamp_granularities[]", "segment");
	want += "--" + kBoundary.toUtf8() + "\r\n";
	want += "Content-Disposition: form-data; name=\"file\"; filename=\"a.ogg\"\r\n";
	want += "Content-Type: audio/ogg\r\n\r\n";
	want += "DATA\r\n";
	want += "--" + kBoundary.toUtf8() + "--\r\n";
	if (request.body != want) {
		std::cout << "got=" << request.body.toStdString() << '\n';
	}
	Check(request.body == want, "groq body mismatch");
}

void CaseGroqParse() {
	const auto parsed = AyuRecognition::ParseGroqResponse(QByteArray(
		"{\"text\":\" Hi \",\"segments\":["
		"{\"start\":0,\"end\":1.5,\"text\":\" Hi \"},"
		"{\"start\":1.5,\"end\":2,\"text\":\" \"}],"
		"\"x_groq\":{\"id\":\"r1\"}}"));
	Check(parsed.ok, "groq parsed");
	Check(parsed.text == QString::fromUtf8("Hi"), "groq text");
	Check(parsed.segments.size() == 1, "groq segments");
	Check(parsed.segments[0].end == 1.5f, "groq end");
	Check(parsed.requestId == QString::fromUtf8("r1"), "groq request id");
	const auto broken = AyuRecognition::ParseGroqResponse(QByteArray("{nope"));
	Check(!broken.ok, "groq broken");
}

void CaseGeminiInteraction() {
	const auto plain = AyuRecognition::BuildGeminiInteraction(
		QString::fromUtf8("https://example.test"),
		QString::fromUtf8("K"),
		QString::fromUtf8("m"),
		QString::fromUtf8("https://example.test/f"),
		QString::fromUtf8("audio/ogg"),
		QString::fromUtf8("ru"),
		QString());
	Check(
		plain.url == QString::fromUtf8("https://example.test/v1beta/interactions"),
		"gemini url");
	Check(
		plain.body
			== QByteArray(
				"{\"model\":\"m\",\"input\":[{\"type\":\"audio\","
				"\"uri\":\"https://example.test/f\",\"mime_type\":\"audio/ogg\"}],"
				"\"generation_config\":{\"transcription_config\":{"
				"\"language_codes\":[\"ru-RU\"],\"mode\":{\"type\":\"verbatim\"}}}}"),
		"gemini body without prompt");
	const auto prompted = AyuRecognition::BuildGeminiInteraction(
		QString::fromUtf8("https://example.test"),
		QString::fromUtf8("K"),
		QString::fromUtf8("m"),
		QString::fromUtf8("https://example.test/f"),
		QString::fromUtf8("audio/ogg"),
		QString::fromUtf8("auto"),
		QString::fromUtf8("Say it"));
	Check(
		prompted.body
			== QByteArray(
				"{\"model\":\"m\",\"input\":[{\"type\":\"text\",\"text\":\"Say it\"},"
				"{\"type\":\"audio\",\"uri\":\"https://example.test/f\","
				"\"mime_type\":\"audio/ogg\"}],"
				"\"generation_config\":{\"transcription_config\":{"
				"\"language_codes\":[],\"mode\":{\"type\":\"verbatim\"}}}}"),
		"gemini body with prompt");
}

void CaseGeminiExtract() {
	Check(
		AyuRecognition::ExtractGeminiText(QByteArray("{\"output_text\":\"  Hi \"}"))
			== QString::fromUtf8("Hi"),
		"gemini direct");
	Check(
		AyuRecognition::ExtractGeminiText(QByteArray(
			"{\"steps\":[{\"content\":[{\"type\":\"text\",\"text\":\"  One. \"}]},"
			"{\"content\":[{\"type\":\"text\",\"text\":\"Two.\"}]}]}"))
			== QString::fromUtf8("One.\nTwo."),
		"gemini steps");
	Check(
		AyuRecognition::ExtractGeminiText(QByteArray("{\"steps\":[]}")).isEmpty(),
		"gemini empty");
}

void CaseMime() {
	using AyuRecognition::InferAudioMime;
	const auto ogg = QString::fromUtf8("audio/ogg");
	Check(
		InferAudioMime(QString::fromUtf8("voice.oga"), QString()) == ogg,
		"mime suffix");
	Check(
		InferAudioMime(
			QString::fromUtf8("a.bin"),
			QString::fromUtf8("audio/mpeg"))
			== QString::fromUtf8("audio/mpeg"),
		"mime explicit");
	Check(
		InferAudioMime(
			QString::fromUtf8("round.mp4"),
			QString::fromUtf8("video/mp4"))
			.isEmpty(),
		"mime video");
	Check(
		InferAudioMime(QString::fromUtf8("file.unknownext"), QString()).isEmpty(),
		"mime unknown");
}

void CaseError() {
	using AyuRecognition::DescribeCloudError;
	Check(
		DescribeCloudError(0, QString()) == QString::fromUtf8("Network error."),
		"error network");
	Check(
		DescribeCloudError(401, QString())
			== QString::fromUtf8("Bad API key (HTTP 401)."),
		"error auth");
	Check(
		DescribeCloudError(429, QString::fromUtf8("slow"))
			== QString::fromUtf8("Rate limited, retry later."),
		"error rate");
	Check(
		DescribeCloudError(500, QString())
			== QString::fromUtf8("Server error (HTTP 500)."),
		"error server");
	const auto body = QString::fromUtf8(QByteArray(400, 'x'));
	const auto limited = DescribeCloudError(400, body);
	Check(
		limited
			== QString::fromUtf8("HTTP error 400: ") + QString::fromUtf8(QByteArray(300, 'x')),
		"error truncation");
}

} // namespace

int main(int argc, char **argv) {
	try {
		Check(argc == 2, "usage: transcription_tests case");
		[[maybe_unused]] const auto app = QCoreApplication(argc, argv);
		const auto name = QString::fromUtf8(argv[1]);
		if (name == QString::fromUtf8("groq_request")) {
			CaseGroqRequest();
		} else if (name == QString::fromUtf8("groq_parse")) {
			CaseGroqParse();
		} else if (name == QString::fromUtf8("gemini_interaction")) {
			CaseGeminiInteraction();
		} else if (name == QString::fromUtf8("gemini_extract")) {
			CaseGeminiExtract();
		} else if (name == QString::fromUtf8("mime")) {
			CaseMime();
		} else if (name == QString::fromUtf8("error")) {
			CaseError();
		} else {
			throw std::runtime_error("unknown case");
		}
		std::cout << "PASS " << name.toStdString() << '\n';
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
