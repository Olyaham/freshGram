"""Run production secret-chat state, vault and key rendering in fresh processes.

The database and DH host adapters are isolated. The production startup migration,
PFS handlers, TL serialization, AES-GCM vault, packet crypto and UI formatters run
unchanged; this does not launch an authenticated Telegram client.
"""

import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "Telegram/SourceFiles"


def method(path, signature):
    source = path.read_text()
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


class SecretVisualHashTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(
            dir="/tmp/opencode" if Path("/tmp/opencode").is_dir() else None)
        cls.directory = Path(cls.temporary.name)
        manager = SOURCES / "ayu/secret/secret_manager.cpp"
        vault = SOURCES / "ayu/secret/secret_vault.cpp"
        box = SOURCES / "ayu/ui/boxes/secret_key_box.cpp"
        source = (SOURCES / "test/secret/key_visual_hash_probe.cpp").read_text()
        replacements = {
            "CHAT": manager.read_text().split("struct Chat {", 1)[1].split("\tbool loaded", 1)[0],
            "CHAT_INFO": re.search(r"struct ChatInfo \{.*?\n\};", (SOURCES / "ayu/secret/secret_manager.h").read_text(), re.S).group(),
            "ROW_STATE": re.search(r"enum class RowState \{.*?\n\};", manager.read_text(), re.S).group(),
            "VAULT_CONSTANTS": vault.read_text().split("constexpr uint8_t kMagic[4]", 1)[1].split("std::atomic<bool>", 1)[0],
            "LOAD_CHATS": manager.read_text().split("\t\tauto resave =", 1)[1].split("\t\tQDir(tempDir())", 1)[0],
        }
        replacements["CHAT"] = "struct Chat {" + replacements["CHAT"]
        replacements["VAULT_CONSTANTS"] = "constexpr uint8_t kMagic[4]" + replacements["VAULT_CONSTANTS"]
        replacements["LOAD_CHATS"] = "\t\tauto resave =" + replacements["LOAD_CHATS"]
        for name, signature in {
            "TO_CHARS": "[[nodiscard]] std::vector<char> ToChars(",
            "FROM_CHARS": "[[nodiscard]] Bytes FromChars(",
            "CHAT_CONTEXT": "[[nodiscard]] std::string ChatContext(",
            "SEAL_ROW": "[[nodiscard]] bool SealChatRow(",
            "OPEN_ROW": "[[nodiscard]] RowState OpenChatRow(",
            "ENCODE_EXTRAS": "[[nodiscard]] std::vector<char> EncodeExtras(",
            "DECODE_EXTRAS": "[[nodiscard]] bool DecodeExtras(",
            "SAVE_CHAT": "\tvoid saveChat(Chat &chat) {",
            "BECOME_READY": "void Manager::Impl::becomeReady(",
            "RESET_KEY_USAGE": "void Manager::Impl::resetKeyUsage(",
            "CLEAR_OUR_EXCHANGE": "void Manager::Impl::clearOurExchange(",
            "COMMIT_KEY": "void Manager::Impl::handleCommitKey(",
            "ACCEPT_KEY": "void Manager::Impl::handleAcceptKey(",
            "MAKE_INFO": "ChatInfo Manager::Impl::makeInfo(",
        }.items():
            if name == "SAVE_CHAT":
                text = manager.read_text()
                start = text.index(signature)
                end = text.index("\n\t}\n", start) + 4
                replacements[name] = text[start:end].replace("\tvoid saveChat", "void Manager::Impl::saveChat", 1)
            else:
                replacements[name] = method(manager, signature)
        for name, signature in {
            "VAULT_IS_SEALED": "bool IsSealed(",
            "VAULT_IS_BOUND": "bool IsBound(",
            "VAULT_SEAL": "Bytes Seal(",
            "VAULT_OPEN": "bool Open(",
        }.items():
            replacements[name] = method(vault, signature)
        for name, signature in {
            "IDENTICON": "[[nodiscard]] QImage MakeIdenticon(",
            "HEX_TEXT": "QString SecretKeyHexText(",
            "EMOJI_TEXT": "QString SecretKeyEmojiText(",
        }.items():
            replacements[name] = method(box, signature)
        replacements["IDENTICON"] = "constexpr auto kGrid = 12;\n" + replacements["IDENTICON"]
        for name, text in replacements.items():
            source = source.replace("@" + name + "@", text)
        assert not re.search(r"@[A-Z_]+@", source)
        probe = cls.directory / "probe.cpp"
        probe.write_text(source)
        cls.executable = cls.directory / "probe"
        libraries = shlex.split(subprocess.check_output(
            ["pkg-config", "--cflags", "--libs", "Qt6Core", "Qt6Gui", "openssl"], text=True))
        command = shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++20", "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations",
            "-fPIC", "-I" + str(SOURCES), str(probe), *libraries, "-o", str(cls.executable)]
        compiled = subprocess.run(command, capture_output=True, text=True)
        if compiled.returncode:
            raise RuntimeError(compiled.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def probe(self, mode, path):
        result = subprocess.run([str(self.executable), mode, str(path)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout) if result.stdout.strip() else None

    def expected(self, snapshot, rotation=0):
        key = bytes((i + rotation * 17) % 256 for i in range(256))
        self.assertEqual(snapshot["hash"], (hashlib.sha1(key).digest()[:16]
                         + hashlib.sha256(key).digest()[:20]).hex())
        self.assertEqual(len(snapshot["emoji"].split()), 5)

    def test_establish_rekey_restart_preserves_both_sides_and_rendering(self):
        for role in ("initiator", "responder"):
            with self.subTest(role=role):
                path = self.directory / (role + ".sealed")
                initial = self.probe("create", path)
                self.expected(initial)
                self.assertEqual(self.probe("rotate-" + role, path), initial)
                self.assertEqual(self.probe("restart", path), initial)

    def test_legacy_rows_freeze_once_and_resave_before_rekey(self):
        for format in ("raw", "extras", "accounting"):
            with self.subTest(format=format):
                path = self.directory / (format + ".sealed")
                initial = self.probe("legacy-" + format, path)
                self.expected(initial, rotation=1)
                self.assertEqual(self.probe("restart-legacy", path), initial)

    def test_layer_46_keeps_original_sha1_and_captures_transition_sha256(self):
        self.probe("layers", self.directory / "unused")

    def test_truncated_hash_is_not_mistaken_for_a_missing_legacy_field(self):
        path = self.directory / "corrupt.sealed"
        self.probe("create", path)
        self.probe("corrupt", path)


if __name__ == "__main__":
    unittest.main()
