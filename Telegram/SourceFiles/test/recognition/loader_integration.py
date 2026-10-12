"""Exercise the real Qt loader, Rust ABI and HTTP transport together."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import subprocess
import sys
import threading
import time


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        body = self.rfile.read(int(self.headers["Content-Length"]))
        self.server.payloads.append(body)
        valid = (
            self.headers.get("Authorization") == "Bearer mock-key"
            and b'filename="audio.wav"' in body
            and b"audio/wav" in body
            and b"RIFF" in body
            and b"whisper-large-v3-turbo" in body
        )
        self.server.valid.append(valid)
        if self.path.startswith("/slow"):
            self.server.cancel_seen.set()
            self.server.stop_waiting.wait(5)
        failed = self.path.startswith("/failure")
        self.send_response(401 if failed else 200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        try:
            self.wfile.write(json.dumps({"error": "PRIVATE provider diagnostic"} if failed else {"text": "mock transcript"}).encode())
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, *args):
        pass


def main():
    binary, library = sys.argv[1:]
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.payloads = []
    server.valid = []
    server.cancel_seen = threading.Event()
    server.stop_waiting = threading.Event()
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        base = f"http://127.0.0.1:{server.server_port}"
        for mode, endpoint in (("success", "/ok"), ("failure", "/failure"), ("cancel", "/slow")):
            started = time.monotonic()
            subprocess.run([binary, library, base + endpoint, mode], check=True, timeout=10)
            if mode == "cancel":
                assert time.monotonic() - started < 3, "cancellation waited for remote response"
                assert server.cancel_seen.is_set(), "test must cancel an in-flight request"
        assert len(server.valid) == 3 and all(server.valid), "multipart, auth and WAV contract"
    finally:
        server.stop_waiting.set()
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == "__main__":
    main()
