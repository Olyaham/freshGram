//! Isolated C ABI.
//!
//! This is the ONLY module in the crate allowed to contain `unsafe` blocks
//! (`grep -rn unsafe src` must show this file and nothing else). Rules:
//!
//! - Every `extern "C"` entry validates its pointers BEFORE touching them.
//! - Every entry body runs inside `catch_unwind`; panics become
//!   `VASR_INTERNAL`, never an unwind across the boundary.
//! - Raw pointers are dereferenced in small blocks, each with a `SAFETY:`
//!   comment citing the exact caller contract from `include/voice_asr.h`.
//! - Strings handed out borrow from their owning object; the header states
//!   the lifetime. No pointer to stack or temporary memory ever escapes.
//! - The async callback fires exactly once, on a worker thread; the engine
//!   must outlive its jobs (same as any Qt object ownership rule).

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_void};
use std::panic::AssertUnwindSafe;
use std::ptr;
use std::sync::{
    Arc, Mutex,
    atomic::{AtomicBool, Ordering},
};

use crate::config::AsrConfig;
use crate::engine::Engine;
use crate::providers::TranscribeInput;
use crate::types::{CancelToken, Transcript, TranscriptError};

// ---------------------------------------------------------------------------
// Stable error codes (mirrored in voice_asr.h; never renumber).
// ---------------------------------------------------------------------------

pub const VASR_OK: i32 = 0;
pub const VASR_NULL_ARG: i32 = 1;
pub const VASR_UTF8: i32 = 2;
pub const VASR_JSON: i32 = 3;
pub const VASR_IO: i32 = 4;
pub const VASR_HTTP: i32 = 5;
pub const VASR_API: i32 = 6;
pub const VASR_TIMEOUT: i32 = 7;
pub const VASR_CANCELLED: i32 = 8;
pub const VASR_MODEL: i32 = 9;
pub const VASR_UNSUPPORTED: i32 = 10;
pub const VASR_RANGE: i32 = 11;
pub const VASR_INTERNAL: i32 = 99;

// ---------------------------------------------------------------------------
// Opaque handles. Fields are private: from C these are single pointers.
// ---------------------------------------------------------------------------

/// Owns the tokio runtime and the shared HTTP client.
pub struct VasrEngine {
    inner: Engine,
}

/// Shared cancellation flag.
pub struct VasrCancel {
    inner: CancelToken,
}

/// Owning transcript with cached C strings. Any `*const char` accessor below
/// borrows from the object it was taken from and dies with it.
pub struct VasrTranscript {
    inner: Transcript,
    text: CString,
    provider: CString,
    model: CString,
    request_id: Option<CString>,
    seg_texts: Vec<CString>,
}

/// In-flight async job. The worker thread is joined by `vasr_job_free`.
pub struct VasrJob {
    cancel: CancelToken,
    done: Arc<AtomicBool>,
    error: Arc<Mutex<(i32, CString)>>,
    handle: Option<std::thread::JoinHandle<()>>,
}

/// Async completion callback: transcript (null on failure), host userdata.
/// Invoked exactly once, on a worker thread — never on the caller's thread.
pub type VasrCallback = Option<extern "C" fn(*mut VasrTranscript, *mut c_void)>;

#[cfg(feature = "local")]
// WHY: whisper-rs 0.16.0 casts its boxed callback to the wrong type;
// a borrowed atomic avoids that cast and the leaked allocation.
pub(crate) fn decode_local(
    state: &mut whisper_rs::WhisperState,
    mut params: whisper_rs::FullParams<'_, '_>,
    samples: &[f32],
    cancelled: &AtomicBool,
) -> Result<(), whisper_rs::WhisperError> {
    unsafe extern "C" fn abort(data: *mut c_void) -> bool {
        // SAFETY: decode_local holds this AtomicBool throughout synchronous full().
        unsafe { &*data.cast::<AtomicBool>() }.load(Ordering::Relaxed)
    }
    // SAFETY: full() joins computation before returning; the callback only reads the flag.
    unsafe {
        params.set_abort_callback(Some(abort));
        params.set_abort_callback_user_data(ptr::from_ref(cancelled).cast_mut().cast());
    }
    state.full(params, samples)
}

impl VasrTranscript {
    fn wrap(transcript: Transcript) -> Self {
        let text = CString::new(transcript.text.clone()).unwrap_or_default();
        let provider = CString::new(transcript.provider.clone()).unwrap_or_default();
        let model = CString::new(transcript.model.clone()).unwrap_or_default();
        let request_id = transcript
            .request_id
            .as_ref()
            .map(|id| CString::new(id.clone()).unwrap_or_default());
        let seg_texts = transcript
            .segments
            .iter()
            .map(|segment| CString::new(segment.text.clone()).unwrap_or_default())
            .collect();
        Self {
            inner: transcript,
            text,
            provider,
            model,
            request_id,
            seg_texts,
        }
    }
}

// ---------------------------------------------------------------------------
// Error reporting: thread-local for sync calls, job-retained for async.
// ---------------------------------------------------------------------------

thread_local! {
    static LAST_ERROR: std::cell::RefCell<(i32, CString)> =
        std::cell::RefCell::new((VASR_OK, CString::new("").unwrap_or_default()));
}

fn set_error(code: i32, message: String) {
    let text = CString::new(message).unwrap_or_default();
    LAST_ERROR.with(|slot| {
        *slot.borrow_mut() = (code, text);
    });
}

fn error_of(error: TranscriptError) -> (i32, String) {
    match error {
        TranscriptError::Cancelled => (VASR_CANCELLED, "cancelled".to_string()),
        TranscriptError::Transport(message) => (VASR_HTTP, message),
        TranscriptError::Api { status, message } => {
            (VASR_API, format!("api error {status}: {message}"))
        }
        TranscriptError::Parse(message) => (VASR_JSON, message),
        TranscriptError::Io(message) => (VASR_IO, message),
        TranscriptError::Model(message) => (VASR_MODEL, message),
        TranscriptError::Timeout => (VASR_TIMEOUT, "timeout".to_string()),
        TranscriptError::Unsupported(message) => (VASR_UNSUPPORTED, message),
    }
}

/// Run `body`, converting panics into `VASR_INTERNAL` instead of unwinding.
fn guard<R>(default: R, body: impl FnOnce() -> R) -> R {
    match std::panic::catch_unwind(AssertUnwindSafe(body)) {
        Ok(value) => value,
        Err(_) => {
            set_error(VASR_INTERNAL, "internal panic".to_string());
            default
        }
    }
}

fn arg_str<'a>(ptr: *const c_char) -> Result<&'a str, (i32, String)> {
    if ptr.is_null() {
        return Err((VASR_NULL_ARG, "null string argument".to_string()));
    }
    // SAFETY: non-null checked above; the header contract requires a valid
    // NUL-terminated UTF-8 string from the caller.
    unsafe { CStr::from_ptr(ptr) }
        .to_str()
        .map_err(|_| (VASR_UTF8, "invalid utf-8 argument".to_string()))
}

fn opt_str(ptr: *const c_char) -> Result<Option<String>, (i32, String)> {
    if ptr.is_null() {
        Ok(None)
    } else {
        Ok(Some(arg_str(ptr)?.to_string()))
    }
}

fn engine_of<'a>(ptr: *mut VasrEngine) -> Result<&'a VasrEngine, (i32, String)> {
    if ptr.is_null() {
        return Err((VASR_NULL_ARG, "null engine".to_string()));
    }
    // SAFETY: non-null checked above; the caller guarantees a live object
    // from `vasr_engine_new` that has not been freed yet.
    Ok(unsafe { &*ptr })
}

fn cancel_of<'a>(ptr: *mut VasrCancel) -> Result<&'a VasrCancel, (i32, String)> {
    if ptr.is_null() {
        return Err((VASR_NULL_ARG, "null cancel token".to_string()));
    }
    // SAFETY: same ownership contract as `engine_of`.
    Ok(unsafe { &*ptr })
}

// Pointers ferried to the worker thread. `Send` is by documented contract:
// the engine outlives its jobs, and userdata is passed through untouched.
struct SendEngine(*mut VasrEngine);
// SAFETY: see above; `vasr_transcribe_async` documents the lifetime rule.
// The worker only reads through this pointer while the host owns the engine.
// The pointer itself is never dereferenced after `vasr_engine_free`.
unsafe impl Send for SendEngine {}

impl SendEngine {
    // Move the whole wrapper (not the bare field: edition 2021+ disjoint
    // captures would otherwise smuggle the raw pointer past this impl).
    fn get(self) -> *mut VasrEngine {
        self.0
    }
}

struct SendUser(*mut c_void);
// SAFETY: opaque host cookie, never inspected, delivered to the host's own
// callback on exactly one thread.
unsafe impl Send for SendUser {}

impl SendUser {
    fn get(self) -> *mut c_void {
        self.0
    }
}

// ---------------------------------------------------------------------------
// Engine lifetime.
// ---------------------------------------------------------------------------

#[unsafe(no_mangle)]
pub extern "C" fn vasr_abi_version() -> u32 {
    1
}

#[unsafe(no_mangle)]
pub extern "C" fn vasr_capabilities() -> u32 {
    u32::from(cfg!(feature = "local"))
        | (u32::from(cfg!(feature = "local-cuda")) << 1)
        | (u32::from(cfg!(feature = "local-vulkan")) << 2)
        | (u32::from(cfg!(feature = "local-metal")) << 3)
        | (u32::from(cfg!(feature = "local-hipblas")) << 4)
        | (u32::from(cfg!(feature = "local-coreml")) << 5)
        | (u32::from(cfg!(feature = "local-sycl")) << 6)
}

#[unsafe(no_mangle)]
pub extern "C" fn vasr_engine_new() -> *mut VasrEngine {
    guard(ptr::null_mut(), || match Engine::new() {
        Ok(inner) => Box::into_raw(Box::new(VasrEngine { inner })),
        Err(error) => {
            let (code, message) = error_of(error);
            set_error(code, message);
            ptr::null_mut()
        }
    })
}

/// Destroy an engine created by [`vasr_engine_new`].
///
/// # Safety
/// If non-null, the pointer must be a live object from `vasr_engine_new`
/// that has not been freed yet. Every job started from it must already be
/// freed: their worker threads borrow this engine.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vasr_engine_free(engine: *mut VasrEngine) {
    guard((), || {
        if engine.is_null() {
            return;
        }
        // SAFETY: non-null checked; the pointer came from `vasr_engine_new`
        // and freeing twice would already violate the header contract.
        // The host must free all jobs first (their threads borrow this).
        unsafe {
            drop(Box::from_raw(engine));
        }
    });
}

// ---------------------------------------------------------------------------
// Cancellation.
// ---------------------------------------------------------------------------

#[unsafe(no_mangle)]
pub extern "C" fn vasr_cancel_new() -> *mut VasrCancel {
    guard(ptr::null_mut(), || {
        Box::into_raw(Box::new(VasrCancel {
            inner: CancelToken::new(),
        }))
    })
}

/// Destroy a token from [`vasr_cancel_new`].
///
/// # Safety
/// If non-null, the pointer must be a live object from `vasr_cancel_new`
/// that has not been freed yet.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vasr_cancel_free(token: *mut VasrCancel) {
    guard((), || {
        if token.is_null() {
            return;
        }
        // SAFETY: ownership contract, same as `vasr_engine_free`.
        unsafe {
            drop(Box::from_raw(token));
        }
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn vasr_cancel(token: *mut VasrCancel) {
    guard((), || {
        let result = cancel_of(token).map(|t| t.inner.cancel());
        if let Err((code, message)) = result {
            set_error(code, message);
        }
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn vasr_cancel_is_cancelled(token: *mut VasrCancel) -> bool {
    guard(false, || match cancel_of(token) {
        Ok(t) => t.inner.is_cancelled(),
        Err((code, message)) => {
            set_error(code, message);
            false
        }
    })
}

// ---------------------------------------------------------------------------
// Synchronous transcription.
// ---------------------------------------------------------------------------

#[unsafe(no_mangle)]
pub extern "C" fn vasr_transcribe(
    engine: *mut VasrEngine,
    config_json: *const c_char,
    audio_path: *const c_char,
    filename: *const c_char,
    mime: *const c_char,
    cancel: *mut VasrCancel,
) -> *mut VasrTranscript {
    guard(ptr::null_mut(), || {
        let result = (|| -> Result<*mut VasrTranscript, (i32, String)> {
            let handle = engine_of(engine)?;
            let config_text = arg_str(config_json)?;
            let config: AsrConfig = serde_json::from_str(config_text)
                .map_err(|e| (VASR_JSON, format!("config: {e}")))?;
            let input = TranscribeInput {
                audio_path: arg_str(audio_path)?.into(),
                filename: arg_str(filename)?.to_string(),
                mime: opt_str(mime)?,
            };
            let token = if cancel.is_null() {
                CancelToken::new()
            } else {
                cancel_of(cancel)?.inner.clone()
            };
            match handle.inner.transcribe(&config, &input, &token) {
                Ok(transcript) => Ok(Box::into_raw(Box::new(VasrTranscript::wrap(transcript)))),
                Err(error) => Err(error_of(error)),
            }
        })();
        match result {
            Ok(ptr) => ptr,
            Err((code, message)) => {
                set_error(code, message);
                ptr::null_mut()
            }
        }
    })
}

// ---------------------------------------------------------------------------
// Transcript accessors. Returned pointers borrow the transcript object.
// ---------------------------------------------------------------------------

/// Destroy a transcript. Borrowed string pointers die with it.
///
/// # Safety
/// If non-null, the pointer must be a live object from a transcribe call
/// that has not been freed yet.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vasr_transcript_free(transcript: *mut VasrTranscript) {
    guard((), || {
        if transcript.is_null() {
            return;
        }
        // SAFETY: ownership contract, same as `vasr_engine_free`.
        unsafe {
            drop(Box::from_raw(transcript));
        }
    });
}

fn transcript_of<'a>(ptr: *mut VasrTranscript) -> Result<&'a VasrTranscript, (i32, String)> {
    if ptr.is_null() {
        return Err((VASR_NULL_ARG, "null transcript".to_string()));
    }
    // SAFETY: non-null checked; caller guarantees a live object.
    Ok(unsafe { &*ptr })
}

#[unsafe(no_mangle)]
pub extern "C" fn vasr_transcript_text(transcript: *mut VasrTranscript) -> *const c_char {
    guard(ptr::null(), || match transcript_of(transcript) {
        Ok(t) => t.text.as_ptr(),
        Err((code, message)) => {
            set_error(code, message);
            ptr::null()
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn vasr_transcript_provider(transcript: *mut VasrTranscript) -> *const c_char {
    guard(ptr::null(), || match transcript_of(transcript) {
        Ok(t) => t.provider.as_ptr(),
        Err((code, message)) => {
            set_error(code, message);
            ptr::null()
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn vasr_transcript_model(transcript: *mut VasrTranscript) -> *const c_char {
    guard(ptr::null(), || match transcript_of(transcript) {
        Ok(t) => t.model.as_ptr(),
        Err((code, message)) => {
            set_error(code, message);
            ptr::null()
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn vasr_transcript_request_id(transcript: *mut VasrTranscript) -> *const c_char {
    guard(ptr::null(), || match transcript_of(transcript) {
        Ok(t) => t.request_id.as_ref().map_or(ptr::null(), |id| id.as_ptr()),
        Err((code, message)) => {
            set_error(code, message);
            ptr::null()
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn vasr_transcript_segment_count(transcript: *mut VasrTranscript) -> i32 {
    guard(0, || match transcript_of(transcript) {
        Ok(t) => t.inner.segments.len().min(i32::MAX as usize) as i32,
        Err((code, message)) => {
            set_error(code, message);
            0
        }
    })
}

fn segment_of<'a>(
    transcript: *mut VasrTranscript,
    index: i32,
) -> Result<(&'a crate::types::AsrSegment, &'a CString), (i32, String)> {
    let t = transcript_of(transcript)?;
    let i = usize::try_from(index).map_err(|_| (VASR_RANGE, "segment index".to_string()))?;
    let segment = t
        .inner
        .segments
        .get(i)
        .ok_or_else(|| (VASR_RANGE, "segment index".to_string()))?;
    let text = t
        .seg_texts
        .get(i)
        .ok_or_else(|| (VASR_INTERNAL, "segment cache".to_string()))?;
    Ok((segment, text))
}

#[unsafe(no_mangle)]
pub extern "C" fn vasr_transcript_segment_text(
    transcript: *mut VasrTranscript,
    index: i32,
) -> *const c_char {
    guard(ptr::null(), || match segment_of(transcript, index) {
        Ok((_, text)) => text.as_ptr(),
        Err((code, message)) => {
            set_error(code, message);
            ptr::null()
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn vasr_transcript_segment_start_ms(
    transcript: *mut VasrTranscript,
    index: i32,
) -> i64 {
    guard(-1, || match segment_of(transcript, index) {
        Ok((segment, _)) => (segment.start_sec * 1000.0) as i64,
        Err((code, message)) => {
            set_error(code, message);
            -1
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn vasr_transcript_segment_end_ms(
    transcript: *mut VasrTranscript,
    index: i32,
) -> i64 {
    guard(-1, || match segment_of(transcript, index) {
        Ok((segment, _)) => (segment.end_sec * 1000.0) as i64,
        Err((code, message)) => {
            set_error(code, message);
            -1
        }
    })
}

// ---------------------------------------------------------------------------
// Asynchronous transcription. Exactly-once callback on a worker thread.
// ---------------------------------------------------------------------------

#[unsafe(no_mangle)]
pub extern "C" fn vasr_transcribe_async(
    engine: *mut VasrEngine,
    config_json: *const c_char,
    audio_path: *const c_char,
    filename: *const c_char,
    mime: *const c_char,
    cancel: *mut VasrCancel,
    callback: VasrCallback,
    userdata: *mut c_void,
) -> *mut VasrJob {
    guard(ptr::null_mut(), || {
        // Parse synchronously: bad arguments fail here, on this thread.
        let parsed = (|| -> Result<(AsrConfig, TranscribeInput, CancelToken), (i32, String)> {
            if engine.is_null() {
                return Err((VASR_NULL_ARG, "null engine".to_string()));
            }
            if callback.is_none() {
                return Err((VASR_NULL_ARG, "null callback".to_string()));
            }
            let config_text = arg_str(config_json)?;
            let config: AsrConfig = serde_json::from_str(config_text)
                .map_err(|e| (VASR_JSON, format!("config: {e}")))?;
            let input = TranscribeInput {
                audio_path: arg_str(audio_path)?.into(),
                filename: arg_str(filename)?.to_string(),
                mime: opt_str(mime)?,
            };
            let token = if cancel.is_null() {
                CancelToken::new()
            } else {
                cancel_of(cancel)?.inner.clone()
            };
            Ok((config, input, token))
        })();
        let (config, input, token) = match parsed {
            Ok(parts) => parts,
            Err((code, message)) => {
                set_error(code, message);
                return ptr::null_mut();
            }
        };

        let job_cancel = token.clone();
        let done = Arc::new(AtomicBool::new(false));
        let done_worker = Arc::clone(&done);
        let error = Arc::new(Mutex::new((VASR_OK, CString::new("").unwrap_or_default())));
        let error_worker = Arc::clone(&error);
        let target = SendEngine(engine);
        let user = SendUser(userdata);
        // Validated non-null above; `expect` cannot fire, and the guard
        // would contain even that.
        let callback = callback.expect("callback validated non-null above");

        let handle = std::thread::spawn(move || {
            // Any panic here must still fire the callback exactly once.
            let outcome = std::panic::catch_unwind(AssertUnwindSafe(|| {
                // SAFETY: the header contract keeps `engine` alive until the
                // job is freed; the pointer was validated non-null at spawn.
                let handle = unsafe { &*target.get() };
                match handle.inner.transcribe(&config, &input, &token) {
                    Ok(transcript) => {
                        Some(Box::into_raw(Box::new(VasrTranscript::wrap(transcript))))
                    }
                    Err(error) => {
                        let (code, message) = error_of(error);
                        if let Ok(mut slot) = error_worker.lock() {
                            *slot = (code, CString::new(message).unwrap_or_default());
                        }
                        None
                    }
                }
            }));
            let ptr = match outcome {
                Ok(ptr) => ptr.unwrap_or(ptr::null_mut()),
                Err(_) => {
                    if let Ok(mut slot) = error_worker.lock() {
                        *slot = (
                            VASR_INTERNAL,
                            CString::new("internal panic").unwrap_or_default(),
                        );
                    }
                    ptr::null_mut()
                }
            };
            callback(ptr, user.get());
            done_worker.store(true, Ordering::SeqCst);
        });

        Box::into_raw(Box::new(VasrJob {
            cancel: job_cancel,
            done,
            error,
            handle: Some(handle),
        }))
    })
}

/// Destroy a job. Blocking: joins the worker, so the callback has fired
/// by return.
///
/// # Safety
/// If non-null, the pointer must be a live object from
/// `vasr_transcribe_async` that has not been freed yet.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vasr_job_free(job: *mut VasrJob) {
    guard((), || {
        if job.is_null() {
            return;
        }
        // SAFETY: ownership contract, same as `vasr_engine_free`.
        // Join is blocking by design: the callback has fired by return.
        let mut owned = unsafe { Box::from_raw(job) };
        if let Some(handle) = owned.handle.take() {
            let _ = handle.join();
        }
    });
}

/// Request cancellation of a running job.
///
/// # Safety
/// The pointer must be a non-null live job from `vasr_transcribe_async`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vasr_job_cancel(job: *mut VasrJob) {
    guard((), || {
        if job.is_null() {
            set_error(VASR_NULL_ARG, "null job".to_string());
            return;
        }
        // SAFETY: non-null checked; caller guarantees a live job.
        unsafe { &*job }.cancel.cancel();
    });
}

/// True once the worker finished (callback fired). Null reads as done.
///
/// # Safety
/// If non-null, the pointer must be a live job from `vasr_transcribe_async`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vasr_job_is_done(job: *mut VasrJob) -> bool {
    guard(true, || {
        if job.is_null() {
            return true;
        }
        // SAFETY: non-null checked; caller guarantees a live job.
        unsafe { &*job }.done.load(Ordering::SeqCst)
    })
}

/// Async failure code; read after `is_done` or inside the callback.
///
/// # Safety
/// If non-null, the pointer must be a live job from `vasr_transcribe_async`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vasr_job_error_code(job: *mut VasrJob) -> i32 {
    guard(VASR_INTERNAL, || {
        if job.is_null() {
            return VASR_NULL_ARG;
        }
        // SAFETY: non-null checked; caller guarantees a live job.
        unsafe { &*job }
            .error
            .lock()
            .map(|slot| slot.0)
            .unwrap_or(VASR_INTERNAL)
    })
}

/// Async failure message. Borrows the job: read after `is_done` or inside
/// the callback, use before `vasr_job_free`.
///
/// # Safety
/// If non-null, the pointer must be a live job from `vasr_transcribe_async`.
/// The returned pointer must not outlive the job, and must not be read
/// concurrently with the worker thread (in practice: after completion).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vasr_job_error_message(job: *mut VasrJob) -> *const c_char {
    guard(ptr::null(), || {
        if job.is_null() {
            return ptr::null();
        }
        // SAFETY: non-null checked; caller guarantees a live job. The pointer
        // borrows the job's retained error and dies with the job.
        // (A poisoned mutex means the worker panicked; message is secondary.)
        let guard = unsafe { &*job }.error.lock();
        match guard {
            Ok(slot) => slot.1.as_ptr(),
            Err(_) => ptr::null(),
        }
    })
}

// ---------------------------------------------------------------------------
// Sync last-error access (calling thread only).
// ---------------------------------------------------------------------------

#[unsafe(no_mangle)]
pub extern "C" fn vasr_last_error_code() -> i32 {
    LAST_ERROR.with(|slot| slot.borrow().0)
}

#[unsafe(no_mangle)]
pub extern "C" fn vasr_last_error_message() -> *const c_char {
    LAST_ERROR.with(|slot| slot.borrow().1.as_ptr())
}
