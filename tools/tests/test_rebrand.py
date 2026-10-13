import importlib.util
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / 'rebrand.py'
CONFIG = json.loads((SCRIPT.parent.parent / 'branding.json').read_text())
SPEC = importlib.util.spec_from_file_location('rebrand', SCRIPT)
REBRAND = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = REBRAND
SPEC.loader.exec_module(REBRAND)


class RebrandTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        (self.root / 'branding.json').write_text(json.dumps(CONFIG))

    def put(self, relative, content):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return path

    def changes(self):
        return REBRAND.collect_changes(self.root, CONFIG, 'freshGram', CONFIG['aliases'])[0]

    def test_source_and_binary_names_with_attribution_and_storage_preserved(self):
        source = self.put('Telegram/SourceFiles/example.cpp', (
            '// This is the source code of AyuGram for Desktop.\n'
            'void AddAyuGramActions();\n'
            'LOG("AyuGram");\n'
            'ayugram::lib_icu; AYUGRAM_ENABLED; ayuGramIcon;\n'
            'const auto context = "ayu-snapshot-v1";\n'
        ).encode())
        watermark = self.put('Telegram/Resources/langs/lang.strings',
            b'"ayu_SettingsWatermark" = "based on AyuGram";\n')
        legacy = self.put('Telegram/SourceFiles/core/branding.h',
            b'u"AyuGram Desktop"_q,\nu"AyuGram"_q,\n')
        secret = self.put('out/bin/tdata/ayu_settings.json', b'{"brand":"AyuGram"}\n')
        vendor = self.put('Telegram/ThirdParty/example.cpp', b'AyuGram\n')
        logo = self.put('Telegram/Resources/logo-AyuGram.png', b'\0AyuGram\xff')
        preserved = {path: path.read_bytes() for path in (watermark, legacy, secret, vendor)}
        for change in self.changes():
            REBRAND.apply_change(change)
        self.assertIn(b'void AddFreshGramActions();', source.read_bytes())
        self.assertIn(b'LOG("freshGram");', source.read_bytes())
        self.assertIn(b'freshgram::lib_icu; FRESHGRAM_ENABLED; freshGramIcon;', source.read_bytes())
        self.assertIn(b'"ayu-snapshot-v1"', source.read_bytes())
        self.assertIn(b'source code of AyuGram', source.read_bytes())
        self.assertFalse(logo.exists())
        self.assertEqual((logo.parent / 'logo-freshGram.png').read_bytes(), b'\0AyuGram\xff')
        for path, content in preserved.items():
            self.assertEqual(path.read_bytes(), content)
        self.assertEqual(self.changes(), [])

    def test_preserves_line_endings_and_executable_mode(self):
        source = self.put('Telegram/build/AyuGram.sh', b'#!/bin/sh\r\necho AyuGram\r\n')
        source.chmod(0o755)
        for change in self.changes():
            REBRAND.apply_change(change)
        result = source.parent / 'freshGram.sh'
        self.assertEqual(result.read_bytes(), b'#!/bin/sh\r\necho freshGram\r\n')
        self.assertEqual(stat.S_IMODE(result.stat().st_mode), 0o755)

    def test_collision_is_rejected_before_writes(self):
        source = self.put('Telegram/build/AyuGram.sh', b'echo AyuGram\n')
        destination = self.put('Telegram/build/freshGram.sh', b'user work\n')
        with self.assertRaisesRegex(ValueError, 'Destination already exists'):
            self.changes()
        self.assertEqual(source.read_bytes(), b'echo AyuGram\n')
        self.assertEqual(destination.read_bytes(), b'user work\n')

    def test_changed_source_is_not_overwritten(self):
        source = self.put('Telegram/SourceFiles/example.cpp', b'AyuGram\n')
        change = self.changes()[0]
        source.write_bytes(b'new user work\n')
        with self.assertRaisesRegex(ValueError, 'Source changed since preview'):
            REBRAND.apply_change(change)
        self.assertEqual(source.read_bytes(), b'new user work\n')

    @unittest.skipUnless(hasattr(os, 'symlink'), 'symlinks are unavailable')
    def test_symlink_targets_are_not_modified(self):
        source = self.put('outside.cpp', b'AyuGram\n')
        link = self.root / 'Telegram/SourceFiles/example.cpp'
        link.parent.mkdir(parents=True)
        link.symlink_to(source)
        self.assertEqual(self.changes(), [])
        self.assertEqual(source.read_bytes(), b'AyuGram\n')

    def test_cli_preview_apply_and_check(self):
        source = self.put('Telegram/SourceFiles/example.cpp', b'AyuGram\n')
        command = [sys.executable, str(SCRIPT), '--root', str(self.root)]
        def run(action):
            return subprocess.run(command + [action], capture_output=True, text=True)
        self.assertEqual(run('--check').returncode, 1)
        self.assertEqual(run('--diff').returncode, 0)
        self.assertEqual(source.read_bytes(), b'AyuGram\n')
        self.assertEqual(run('--apply').returncode, 0)
        self.assertEqual(source.read_bytes(), b'freshGram\n')
        self.assertEqual(run('--check').returncode, 0)


if __name__ == '__main__':
    unittest.main()
