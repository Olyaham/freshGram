#pragma once

#include <QtCore/QString>

#include <string>

namespace AyuRecognition {

[[nodiscard]] QString SealTranscriptionSecret(
	const QString &plain,
	QString &stored,
	const std::string &context);
[[nodiscard]] QString OpenTranscriptionSecret(
	const QString &stored,
	const std::string &context);

} // namespace AyuRecognition
