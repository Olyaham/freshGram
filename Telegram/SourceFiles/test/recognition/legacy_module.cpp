#include "voice_asr.h"

uint32_t vasr_abi_version() { return 1; }
uint32_t vasr_capabilities() { return LEGACY_CAPABILITIES; }
VasrEngine *vasr_engine_new() {
	static auto storage = 0;
	return reinterpret_cast<VasrEngine*>(&storage);
}
void vasr_engine_free(VasrEngine *) {}
VasrCancel *vasr_cancel_new() { return nullptr; }
void vasr_cancel_free(VasrCancel *) {}
void vasr_cancel(VasrCancel *) {}
VasrTranscript *vasr_transcribe(
		VasrEngine *, const char *, const char *, const char *, const char *, VasrCancel *) {
	return nullptr;
}
void vasr_transcript_free(VasrTranscript *) {}
const char *vasr_transcript_text(VasrTranscript *) { return ""; }
int32_t vasr_last_error_code() { return VASR_UNSUPPORTED; }
const char *vasr_last_error_message() { return "legacy fixture"; }
