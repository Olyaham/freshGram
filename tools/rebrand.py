#!/usr/bin/env python3

import argparse
from dataclasses import dataclass
import difflib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile


@dataclass
class Change:
    source: Path
    destination: Path
    before: bytes
    after: bytes


def replacement_rules(aliases, name):
    rules = {}
    for alias in aliases:
        rules[alias] = name
        title = alias[0].upper() + alias[1:]
        if title != alias:
            rules[title] = name[0].upper() + name[1:]
        rules[alias[0].lower() + alias[1:]] = name[0].lower() + name[1:]
        rules[alias.lower()] = name.lower()
        rules[alias.upper()] = name.upper()
    pattern = re.compile('|'.join(re.escape(key) for key in sorted(
        rules, key=len, reverse=True)))

    def replace(text):
        def substitute(match):
            value = rules[match.group()]
            if (match.group()[0].isupper() and not match.group().isupper()
                    and match.start() and (text[match.start() - 1].isalnum()
                        or text[match.start() - 1] == '_')):
                value = value[0].upper() + value[1:]
            return value
        return pattern.sub(substitute, text)

    return replace


def collect_changes(root, config, name, aliases):
    replace = replacement_rules(aliases, name)
    listed = subprocess.check_output([
        'git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard',
    ], cwd=root).decode().split('\0')
    changes = []
    destinations = set()
    protected = 0
    for relative in sorted(set(listed) - {''}):
        if not any(relative == path or relative.startswith(path + '/')
                for path in config['paths']):
            continue
        source = root / relative
        if source.is_symlink() or not source.is_file():
            continue
        if not source.resolve().is_relative_to(root.resolve()):
            raise ValueError(f'Path escapes the repository: {relative}')
        before = source.read_bytes()
        after = before
        if b'\0' not in before:
            try:
                text = before.decode('utf-8')
            except UnicodeDecodeError:
                text = None
            if text is not None:
                preserved = config['preserve_lines'] + config.get(
                    'preserve_files', {}).get(relative, [])
                lines = []
                for line in text.splitlines(keepends=True):
                    updated = replace(line)
                    if any(token in line for token in preserved):
                        protected += int(updated != line)
                        updated = line
                    lines.append(updated)
                after = ''.join(lines).encode('utf-8')
        destination = root / replace(relative)
        if destination == source and after == before:
            continue
        if destination in destinations:
            raise ValueError(f'Duplicate destination: {destination}')
        if destination != source and (destination.exists() or destination.is_symlink()):
            raise ValueError(f'Destination already exists: {destination}')
        destinations.add(destination)
        changes.append(Change(source, destination, before, after))
    return changes, protected


def apply_change(change):
    if change.source.is_symlink() or change.source.read_bytes() != change.before:
        raise ValueError(f'Source changed since preview: {change.source}')
    if (change.destination != change.source
            and (change.destination.exists() or change.destination.is_symlink())):
        raise ValueError(f'Destination already exists: {change.destination}')
    mode = stat.S_IMODE(change.source.stat().st_mode)
    change.destination.parent.mkdir(parents=True, exist_ok=True)
    if change.before != change.after:
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=change.source.parent,
                    prefix='.rebrand-', delete=False) as output:
                temporary = Path(output.name)
                output.write(change.after)
                output.flush()
                os.fsync(output.fileno())
            os.chmod(temporary, mode)
            os.replace(temporary, change.source)
        finally:
            if temporary is not None and temporary.exists():
                temporary.unlink()
    if change.destination != change.source:
        change.source.rename(change.destination)


def main():
    parser = argparse.ArgumentParser(description='Normalize first-party product branding.')
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--config', type=Path, default=Path('branding.json'))
    parser.add_argument('--name', help='Target name, defaults to branding.json.')
    parser.add_argument('--from-name', action='append', default=[],
        help='Additional source name, including an already applied brand.')
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--check', action='store_true', help='Fail if changes are needed (default).')
    action.add_argument('--diff', action='store_true', help='Preview text changes and file renames.')
    action.add_argument('--apply', action='store_true', help='Apply the previewed changes.')
    args = parser.parse_args()
    root = args.root.resolve()
    config_path = args.config if args.config.is_absolute() else root / args.config
    config = json.loads(config_path.read_text(encoding='utf-8'))
    name = args.name or config['name']
    if not re.fullmatch('[A-Za-z][A-Za-z0-9]*', name):
        parser.error('The target name must be a C++ identifier containing letters and digits.')
    aliases = list(dict.fromkeys(config['aliases'] + args.from_name))
    if not aliases or any(not re.fullmatch('[A-Za-z][A-Za-z0-9]*', alias) for alias in aliases):
        parser.error('Source names must be non-empty identifiers containing letters and digits.')
    changes, protected = collect_changes(root, config, name, aliases)
    for change in changes:
        source = change.source.relative_to(root).as_posix()
        destination = change.destination.relative_to(root).as_posix()
        if args.diff:
            if source != destination:
                print(f'rename {source} -> {destination}')
            if change.before != change.after:
                print(''.join(difflib.unified_diff(
                    change.before.decode('utf-8').splitlines(keepends=True),
                    change.after.decode('utf-8').splitlines(keepends=True),
                    fromfile='a/' + source, tofile='b/' + destination)), end='')
        else:
            print(f'{source}' + (f' -> {destination}' if source != destination else ''))
        if args.apply:
            apply_change(change)
    print(f'{len(changes)} files need changes; {protected} attribution/legacy lines preserved.')
    return 1 if changes and not (args.apply or args.diff) else 0


if __name__ == '__main__':
    raise SystemExit(main())
