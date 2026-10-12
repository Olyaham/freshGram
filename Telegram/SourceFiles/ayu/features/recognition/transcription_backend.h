// This is the source code of freshGram.
#pragma once

#include "ayu/features/recognition/local_transcribe.h"
#include "ayu/features/recognition/transcription_config.h"

namespace Main {
class Session;
} // namespace Main

class HistoryItem;

namespace AyuRecognition {

enum class BackendDestination {
	Server,
	StaticLocal,
	Engine,
};

struct BackendRoute {
	BackendDestination destination = BackendDestination::Server;
	EngineProvider engine = EngineProvider::Telegram;
};

[[nodiscard]] BackendRoute ResolveTranscription(
	not_null<HistoryItem*> item);
void TranscribeWithBackend(
	not_null<HistoryItem*> item,
	EngineProvider engine,
	Fn<void(LocalTranscript)> done);
void CancelBackendTranscription(not_null<HistoryItem*> item);

} // namespace AyuRecognition
