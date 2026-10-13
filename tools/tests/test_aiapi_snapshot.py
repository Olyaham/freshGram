import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class SnapshotCheckoutTest(unittest.TestCase):
    def test_windows_checkout_preserves_hashes_and_rejects_edits(self):
        with tempfile.TemporaryDirectory(dir="/tmp/opencode" if Path("/tmp/opencode").is_dir() else None) as directory:
            root = Path(directory)
            repository = root / "repository"
            checkout = root / "checkout"
            repository.mkdir()
            checkout.mkdir()
            baseline = root / "baseline"
            baseline.mkdir()
            snapshot = "Telegram/ThirdParty/aiapi"
            shutil.copytree(ROOT / snapshot, repository / snapshot,
                            ignore=shutil.ignore_patterns("target", ".git", "Cargo.lock"))
            (repository / ".gitattributes").write_bytes((ROOT / ".gitattributes").read_bytes())
            subprocess.run(["git", "init", "--quiet", str(repository)], check=True)
            def git(*args):
                subprocess.run(["git", "-C", str(repository), *args], check=True,
                               capture_output=True)
            git("config", "core.autocrlf", "true")
            git("add", ".gitattributes", snapshot)
            git("checkout-index", "--all", "--prefix=" + checkout.as_posix() + "/")
            (checkout / "tools").mkdir()
            shutil.copyfile(ROOT / "tools/sync_aiapi.py", checkout / "tools/sync_aiapi.py")
            command = [sys.executable, str(checkout / "tools/sync_aiapi.py"), "--check"]
            checked = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(checked.returncode, 0, checked.stderr)
            manifest = json.loads((checkout / snapshot / "SOURCE.json").read_text())
            for name in manifest["files"]:
                self.assertNotIn(b"\r\n", (checkout / snapshot / name).read_bytes(), name)
            changed = checkout / snapshot / "crates/aiapi-http/src/lib.rs"
            changed.write_bytes(changed.read_bytes() + b"\n// unexpected edit\n")
            rejected = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("crates/aiapi-http/src/lib.rs", rejected.stderr)
            attributes = repository / ".gitattributes"
            attributes.write_bytes(attributes.read_bytes().replace(b"Telegram/ThirdParty/aiapi/** text eol=lf\n", b""))
            git("add", ".gitattributes")
            git("checkout-index", "--all", "--prefix=" + baseline.as_posix() + "/")
            (baseline / "tools").mkdir()
            shutil.copyfile(ROOT / "tools/sync_aiapi.py", baseline / "tools/sync_aiapi.py")
            reproduced = subprocess.run([sys.executable, str(baseline / "tools/sync_aiapi.py"), "--check"], capture_output=True, text=True)
            self.assertNotEqual(reproduced.returncode, 0)
            for name in manifest["files"]:
                self.assertIn(name, reproduced.stderr)


if __name__ == "__main__":
    unittest.main()
