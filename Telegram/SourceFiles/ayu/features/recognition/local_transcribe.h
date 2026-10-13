// This is the source code of freshGram.
#pragma once

class HistoryItem;

namespace AyuRecognition {

enum class LocalTranscribeStatus {
	Done,
	NoModel,
	Failed,
	Cancelled,
};

struct LocalTranscript {
	LocalTranscribeStatus status = LocalTranscribeStatus::Failed;
	QString text; // Empty if there was no speech.
	QString error;
	QString details;
};

} // namespace AyuRecognition
