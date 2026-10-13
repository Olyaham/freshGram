/* voice_asr.h — C ABI of the voice-asr transcription engine.
 *
 * Threading and lifetime contract (binding):
 * - Opaque pointers come from the vasr_*_new/create calls and die with the
 *   matching vasr_*_free. Double free and use-after-free are host bugs.
 * - String results borrow their owning object; use them before freeing it.
 * - The async callback fires EXACTLY ONCE, on a worker thread, never on the
 *   calling thread. Marshal to your UI thread yourself.
 * - The engine must outlive every job started from it: free jobs first.
 * - Every function is panic-safe; on failure sync calls return NULL/0 and
 *   set the thread-local error (vasr_last_error_*). Async failures arrive
 *   as a NULL transcript plus vasr_job_error_*.
 */

#ifndef VOICE_ASR_H
#define VOICE_ASR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stable error codes. Never renumbered. */
#define VASR_OK 0
#define VASR_NULL_ARG 1
#define VASR_UTF8 2
#define VASR_JSON 3
#define VASR_IO 4
#define VASR_HTTP 5
#define VASR_API 6
#define VASR_TIMEOUT 7
#define VASR_CANCELLED 8
#define VASR_MODEL 9
#define VASR_UNSUPPORTED 10
#define VASR_RANGE 11
#define VASR_INTERNAL 99

typedef struct VasrEngine VasrEngine;
typedef struct VasrCancel VasrCancel;
typedef struct VasrTranscript VasrTranscript;
typedef struct VasrJob VasrJob;

#define VASR_CAP_LOCAL 1
#define VASR_CAP_CUDA 2
#define VASR_CAP_VULKAN 4
#define VASR_CAP_METAL 8
#define VASR_CAP_HIPBLAS 16
#define VASR_CAP_COREML 32
#define VASR_CAP_SYCL 64
#define VASR_CAP_MODELS 128
#define VASR_CAP_LLM 256
#define VASR_CAP_PROCESSING 512
#define VASR_CAP_NETWORK 1024

uint32_t vasr_abi_version(void);
uint32_t vasr_capabilities(void);

/* Completion callback for vasr_transcribe_async. */
typedef void (*VasrCallback)(VasrTranscript *transcript, void *userdata);

/* Engine lifetime. NULL on failure (see vasr_last_error_*). */
VasrEngine *vasr_engine_new(void);
void vasr_engine_free(VasrEngine *engine);

/* Cancellation token. */
VasrCancel *vasr_cancel_new(void);
void vasr_cancel_free(VasrCancel *token);
void vasr_cancel(VasrCancel *token);
bool vasr_cancel_is_cancelled(VasrCancel *token);

/* Blocking transcription.
 * config_json: AsrConfig as JSON (see README for the schema).
 * audio_path: existing local audio file. filename: original name (drives
 *   MIME inference). mime: nullable ("audio/ogg", NULL to infer).
 * cancel: nullable; NULL runs to completion.
 * Returns NULL on failure. */
VasrTranscript *vasr_transcribe(VasrEngine *engine,
                                const char *config_json,
                                const char *audio_path,
                                const char *filename,
                                const char *mime,
                                VasrCancel *cancel);

/* Non-blocking transcription. Returns NULL on bad arguments (synchronously).
 * The callback receives the transcript (NULL on failure) plus userdata. */
VasrJob *vasr_transcribe_async(VasrEngine *engine,
                               const char *config_json,
                               const char *audio_path,
                               const char *filename,
                               const char *mime,
                               VasrCancel *cancel,
                               VasrCallback callback,
                               void *userdata);

/* vasr_job_free joins the worker: by return the callback has fired. */
void vasr_job_free(VasrJob *job);
void vasr_job_cancel(VasrJob *job);
bool vasr_job_is_done(VasrJob *job);
/* Async failure detail. Message borrows the job; read after is_done. */
int32_t vasr_job_error_code(VasrJob *job);
const char *vasr_job_error_message(VasrJob *job);

/* Transcript accessors. Pointers borrow the transcript object. request_id
 * may be NULL. Times are milliseconds; -1/NULL on out-of-range index. */
void vasr_transcript_free(VasrTranscript *transcript);
const char *vasr_transcript_text(VasrTranscript *transcript);
const char *vasr_transcript_details(VasrTranscript *transcript);
const char *vasr_transcript_provider(VasrTranscript *transcript);
const char *vasr_transcript_model(VasrTranscript *transcript);
const char *vasr_transcript_request_id(VasrTranscript *transcript);
int32_t vasr_transcript_segment_count(VasrTranscript *transcript);
const char *vasr_transcript_segment_text(VasrTranscript *transcript, int32_t index);
int64_t vasr_transcript_segment_start_ms(VasrTranscript *transcript, int32_t index);
int64_t vasr_transcript_segment_end_ms(VasrTranscript *transcript, int32_t index);

/* Optional ABI-1 extensions, gated by VASR_CAP_MODELS / VASR_CAP_LLM.
 * Requests and owned results are JSON; see README for schemas.
 * Blocking calls; cancellation is nullable. Free each returned string exactly
 * once with vasr_string_free (NULL is allowed). Engine must remain alive. */
char *vasr_models(VasrEngine *engine, const char *request_json, VasrCancel *cancel);
char *vasr_generate(VasrEngine *engine, const char *request_json, VasrCancel *cancel);
void vasr_string_free(char *value);

/* Sync last-error access (calling thread only). */
int32_t vasr_last_error_code(void);
const char *vasr_last_error_message(void);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_ASR_H */
