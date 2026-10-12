/* FFI smoke test: linkage, null-safety, error reporting. No network. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "voice_asr.h"

int main(void) {
    VasrEngine *engine = vasr_engine_new();
    assert(engine != NULL);

    VasrCancel *cancel = vasr_cancel_new();
    assert(cancel != NULL);
    assert(!vasr_cancel_is_cancelled(cancel));
    vasr_cancel(cancel);
    assert(vasr_cancel_is_cancelled(cancel));

    /* Bad config JSON must fail cleanly with VASR_JSON. */
    VasrTranscript *bad = vasr_transcribe(engine, "{nope", "/tmp/x.ogg",
                                          "x.ogg", NULL, NULL);
    assert(bad == NULL);
    assert(vasr_last_error_code() == VASR_JSON);
    assert(strstr(vasr_last_error_message(), "config") != NULL);

    /* Null engine must fail cleanly, not crash. */
    VasrTranscript *null_engine = vasr_transcribe(
        NULL, "{}", "/tmp/x.ogg", "x.ogg", NULL, NULL);
    assert(null_engine == NULL);
    assert(vasr_last_error_code() == VASR_NULL_ARG);

    /* Null callback must fail synchronously. */
    VasrJob *null_cb = vasr_transcribe_async(engine, "{}", "/tmp/x.ogg",
                                             "x.ogg", NULL, NULL, NULL, NULL);
    assert(null_cb == NULL);
    assert(vasr_last_error_code() == VASR_NULL_ARG);

    /* Freeing NULL handles is a no-op. */
    vasr_transcript_free(NULL);
    vasr_job_free(NULL);
    vasr_cancel_free(NULL);
    vasr_engine_free(NULL);

    /* Null job reads as done. */
    assert(vasr_job_is_done(NULL));

    vasr_cancel_free(cancel);
    vasr_engine_free(engine);
    printf("FFI smoke: ok\n");
    return 0;
}
