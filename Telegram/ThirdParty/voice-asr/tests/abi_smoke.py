#!/usr/bin/env python3
"""Verify the loaded C ABI against a loopback provider, without real credentials."""

import argparse
import ctypes as C
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading


class Provider(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    failures = []

    def log_message(self, *args):
        pass

    def respond(self, value):
        data = json.dumps(value).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self.respond({"data": [{"id": "text-model", "context_window": 2048,
                                 "max_output_tokens": 64}]})

    def do_POST(self):
        try:
            body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
            assert "authorization" not in self.headers
            if self.path == "/audio/transcriptions":
                assert b"audio.ogg" in body
                assert b"FAKEAUDIO" in body
                self.respond({"text": "hello world", "segments": [
                    {"start": 0, "end": 1.25, "text": "hello world"}]})
            else:
                request = json.loads(body)
                assert request["model"] == "text-model"
                assert request["max_tokens"] == 64
                self.respond({"id": "edit-1", "object": "chat.completion", "created": 1,
                              "model": "text-model", "choices": [{"index": 0,
                              "message": {"role": "assistant", "content": json.dumps({
                                  "mode": "short", "text": "Hello, world!", "chapters": []})},
                              "finish_reason": "stop"}]})
        except Exception as error:
            self.failures.append(repr(error))
            self.send_error(500)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    args = parser.parse_args()
    lib = C.CDLL(str(args.library.resolve()))
    pointer = C.c_void_p
    callback_type = C.CFUNCTYPE(None, pointer, pointer)
    signatures = {
        "vasr_abi_version": (C.c_uint32, []),
        "vasr_capabilities": (C.c_uint32, []),
        "vasr_engine_new": (pointer, []),
        "vasr_engine_free": (None, [pointer]),
        "vasr_cancel_new": (pointer, []),
        "vasr_cancel_free": (None, [pointer]),
        "vasr_cancel": (None, [pointer]),
        "vasr_models": (pointer, [pointer, C.c_char_p, pointer]),
        "vasr_generate": (pointer, [pointer, C.c_char_p, pointer]),
        "vasr_string_free": (None, [pointer]),
        "vasr_transcribe": (pointer, [pointer, C.c_char_p, C.c_char_p,
                                      C.c_char_p, C.c_char_p, pointer]),
        "vasr_transcribe_async": (pointer, [pointer, C.c_char_p, C.c_char_p,
                                            C.c_char_p, C.c_char_p, pointer,
                                            callback_type, pointer]),
        "vasr_transcript_text": (C.c_char_p, [pointer]),
        "vasr_transcript_details": (C.c_char_p, [pointer]),
        "vasr_transcript_segment_count": (C.c_int32, [pointer]),
        "vasr_transcript_segment_end_ms": (C.c_int64, [pointer, C.c_int32]),
        "vasr_transcript_free": (None, [pointer]),
        "vasr_job_free": (None, [pointer]),
        "vasr_last_error_code": (C.c_int32, []),
        "vasr_last_error_message": (C.c_char_p, []),
    }
    for name, (result, arguments) in signatures.items():
        function = getattr(lib, name)
        function.restype = result
        function.argtypes = arguments
    assert lib.vasr_abi_version() == 1
    assert lib.vasr_capabilities() & (128 | 256 | 512) == 128 | 256 | 512
    assert not lib.vasr_models(None, b"{}", None)
    assert lib.vasr_last_error_code() == 1
    lib.vasr_string_free(None)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Provider)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    engine = lib.vasr_engine_new()
    assert engine
    token = lib.vasr_cancel_new()
    try:
        assert not lib.vasr_transcribe(engine, b'{"provider":"private-token-not-a-provider"}', b"unused", b"unused.ogg", None, None)
        assert lib.vasr_last_error_code() == 3
        assert b"private-token" not in lib.vasr_last_error_message()
        base = f"http://127.0.0.1:{server.server_port}"

        def owned(function, request, cancellation=None):
            value = function(engine, json.dumps(request).encode(), cancellation)
            assert value, lib.vasr_last_error_message()
            try:
                return json.loads(C.string_at(value))
            finally:
                lib.vasr_string_free(value)

        discovery = {"options": {"provider": "custom", "protocol": "openai_compatible",
                                  "base_url": base, "secret_name": None}, "force": True}
        assert owned(lib.vasr_models, discovery)["models"][0]["id"] == "text-model"
        discovery["force"] = False
        assert owned(lib.vasr_models, discovery)["cached"]
        config = json.dumps({"provider": "custom", "fallback": None,
                             "custom": {"api_base": base, "model": "asr-model"},
                             "cleanup": {"provider": "custom", "model": "text-model"}}).encode()
        with tempfile.TemporaryDirectory(prefix="voice-asr-abi-") as directory:
            path = Path(directory) / "audio.ogg"
            path.write_bytes(b"FAKEAUDIO")
            for _ in range(2):
                transcript = lib.vasr_transcribe(engine, config, str(path).encode(),
                                                 b"audio.ogg", b"audio/ogg", None)
                assert transcript, lib.vasr_last_error_message()
                try:
                    assert lib.vasr_transcript_text(transcript) == b"Hello, world!"
                    details = json.loads(lib.vasr_transcript_details(transcript))
                    assert details["raw"]["text"] == "hello world"
                    assert details["cleanup_provider"] == "custom"
                    assert lib.vasr_transcript_segment_count(transcript) == 1
                    assert lib.vasr_transcript_segment_end_ms(transcript, 0) == 1250
                finally:
                    lib.vasr_transcript_free(transcript)
            completed = threading.Event()
            callbacks = []

            @callback_type
            def complete(transcript, userdata):
                callbacks.append((transcript, userdata))
                completed.set()

            job = lib.vasr_transcribe_async(engine, config, str(path).encode(),
                                            b"audio.ogg", b"audio/ogg", None, complete, None)
            assert job
            try:
                assert completed.wait(5), "async callback must complete"
            finally:
                lib.vasr_job_free(job)
            assert len(callbacks) == 1
            assert callbacks[0][0]
            lib.vasr_transcript_free(callbacks[0][0])
        profiles = f'''
[providers.custom]
driver = "openai_compatible"
base_url = "{base}"
[models.text]
provider = "custom"
model = "text-model"
[models.text.capabilities]
supports_images = false
supports_tools = false
supports_system_prompt = true
structured_output = "prompt_only"
context_window_tokens = 2048
max_output_tokens = 64
request_timeout_sec = 5
thinking = "none"
[routes.text]
models = ["text"]
'''
        generated = owned(lib.vasr_generate, {"profiles_toml": profiles,
                                             "route": "text", "prompt": "hello world"})
        assert generated["model"] == "text-model"
        assert generated["attempts"][0]["outcome"] == "success"
        lib.vasr_cancel(token)
        assert not lib.vasr_models(engine, json.dumps(discovery).encode(), token)
        assert lib.vasr_last_error_code() == 8
        assert not Provider.failures, Provider.failures
    finally:
        lib.vasr_cancel_free(token)
        lib.vasr_engine_free(engine)
        server.shutdown()
        server.server_close()
        thread.join()
    print("ABI smoke passed: discovery, generation, cleanup, segments, async ownership and cancellation")


if __name__ == "__main__":
    main()
