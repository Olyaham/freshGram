"""Test the production decoder and temporary-file lifecycle outside the client.

Qt file I/O and FFmpeg decoding are real. The audio-only FFmpeg factory adapter
replaces application logging/hardware setup; link wrappers inject terminal errors
after successful frames. No Telegram profile or network is used.
"""

import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest
import wave


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "Telegram/SourceFiles"
TESTS = SOURCES / "test/recognition"


class NativeAudioTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(dir="/tmp/opencode" if Path("/tmp/opencode").is_dir() else None)
        cls.directory = Path(cls.temporary.name)
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-fPIC",
                 "-I" + str(TESTS / "stubs"), "-I" + str(SOURCES),
                 "-include", str(TESTS / "stubs/prelude.h")]
        def build(name, files, packages, wrappers=()):
            libraries = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", *packages], text=True))
            subprocess.run(compiler + flags + list(map(str, files)) + libraries
                           + ["-Wl,--wrap=" + value for value in wrappers]
                           + ["-o", str(cls.directory / name)], check=True)
        build("decode", [TESTS / "audio_fault_probe.cpp", SOURCES / "ayu/features/recognition/recognition_audio.cpp"],
              ["Qt6Core", "libavformat", "libavcodec", "libavutil", "libswresample"],
              ["avcodec_send_packet", "av_read_frame", "avcodec_receive_frame", "swr_convert", "swr_get_out_samples"])
        build("temporary", [TESTS / "temporary_audio_probe.cpp", SOURCES / "ayu/features/recognition/transcription_config.cpp"], ["Qt6Core"])
        cls.audio = cls.directory / "valid.wav"
        with wave.open(str(cls.audio), "wb") as file:
            file.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
            file.writeframes(struct.pack("<h", 12000) * 80000)
        for suffix, codec in [("ogg", "libopus"), ("m4a", "aac")]:
            subprocess.run(["ffmpeg", "-v", "error", "-i", str(cls.audio), "-c:a", codec,
                            str(cls.directory / ("valid." + suffix))], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def decode(self, path, fault="none"):
        return tuple(map(int, subprocess.check_output([str(self.directory / "decode"), str(path), fault], text=True).split()))

    def test_valid_audio_and_terminal_errors_after_valid_frames(self):
        self.assertEqual(self.decode(self.audio), (1, 80000))
        for fault in ["send", "read", "receive", "resample", "capacity", "budget", "flush", "cancel", "cancel-midway"]:
            with self.subTest(fault=fault):
                self.assertEqual(self.decode(self.audio, fault), (0, 0))

    def test_corrupted_audio_does_not_return_its_valid_prefix(self):
        corrupted = self.directory / "corrupted.wav"
        corrupted.write_bytes(self.audio.read_bytes()[:100057])
        self.assertEqual(self.decode(corrupted), (0, 0))

    def test_voice_and_round_video_audio_codecs_flush_successfully(self):
        for suffix in ["ogg", "m4a"]:
            with self.subTest(suffix=suffix):
                success, count = self.decode(self.directory / ("valid." + suffix))
                self.assertEqual(success, 1)
                self.assertTrue(75000 <= count <= 82000, count)

    def test_crash_cleanup_preserves_another_live_process(self):
        workdir = self.directory / "profile"
        workdir.mkdir(exist_ok=True)
        command = [str(self.directory / "temporary"), str(workdir)]
        child = subprocess.Popen(command + ["write"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            session = Path(child.stdout.readline().strip())
            wav = session / "audio.wav"
            with wave.open(str(wav), "rb") as audio:
                self.assertEqual(audio.getnframes(), 100000)
                self.assertEqual(audio.readframes(1), b"\x00\x00")
            subprocess.run(command + ["initialize"], check=True, capture_output=True)
            self.assertTrue(wav.exists(), "cleanup removed a live worker's audio")
            child.kill()
            child.wait(timeout=5)
            subprocess.run(command + ["initialize"], check=True, capture_output=True)
            self.assertFalse(session.exists(), "crashed worker's plaintext survived startup cleanup")
        finally:
            if child.poll() is None:
                child.kill()
                child.wait(timeout=5)
            child.stdin.close()
            child.stdout.close()

    def test_cleanup_preserves_unmanaged_data_and_symlink_targets(self):
        workdir = self.directory / "unmanaged-profile"
        root = workdir / "tdata/asr-tmp"
        personal = root / "session-personal"
        personal.mkdir(parents=True)
        notes = personal / "notes.txt"
        notes.write_text("user work")
        target = self.directory / "symlink-target"
        target.mkdir()
        (target / "managed").write_bytes(b"FGASR1\n")
        (target / "audio.wav").write_bytes(b"preserve target")
        (root / "session-link").symlink_to(target, target_is_directory=True)
        subprocess.run([str(self.directory / "temporary"), str(workdir), "initialize"], check=True, capture_output=True)
        self.assertEqual(notes.read_text(), "user work")
        self.assertTrue((target / "audio.wav").exists())


if __name__ == "__main__":
    unittest.main()
