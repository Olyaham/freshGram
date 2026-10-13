#!/usr/bin/env python3
"""Synchronize the standalone AiApi source snapshot used by the client."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess


def snapshot_digest(files):
    content = json.dumps(files, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(content).hexdigest()


def source_revision(source):
    if not (source / ".git").exists():
        return None
    try:
        revision = subprocess.run(["git", "-C", str(source), "rev-parse", "--verify", "HEAD"],
                                  capture_output=True, text=True, check=False)
        status = subprocess.run(["git", "-C", str(source), "status", "--porcelain"],
                                capture_output=True, text=True, check=False)
    except OSError:
        return None
    return revision.stdout.strip() if revision.returncode == 0 and status.returncode == 0 and not status.stdout else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    destination = Path(__file__).resolve().parents[1] / "Telegram/ThirdParty/aiapi"
    marker = destination / "SOURCE.json"
    previous = json.loads(marker.read_text()) if marker.exists() else {"files": {}}
    if args.check and previous.get("content_sha256") != snapshot_digest(previous["files"]):
        parser.error("snapshot content identity is missing or inconsistent")
    for name in previous["files"]:
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or "\\" in name:
            parser.error("invalid snapshot path: " + name)
    if args.source is None:
        if not args.check:
            parser.error("--source is required when synchronizing")
        if not previous["files"]:
            parser.error("snapshot manifest is missing or empty")
        changed = [name for name, digest in previous["files"].items()
                   if not (destination / name).is_file()
                   or hashlib.sha256((destination / name).read_bytes()).hexdigest() != digest]
        if changed:
            parser.exit(1, "AiApi snapshot checksum mismatch: " + ", ".join(sorted(changed)) + "\n")
        print(f"AiApi snapshot checksums verified: {len(previous['files'])} files")
        return
    source = args.source.resolve()
    paths = [Path(name) for name in ("Cargo.toml", "README.md", "ATTRIBUTION.md", "LICENSE-MIT", ".gitignore")]
    paths += sorted(path.relative_to(source) for path in (source / "crates").rglob("*")
                    if path.is_file() and (path.name == "Cargo.toml" or path.suffix == ".rs"))
    paths += sorted(path.relative_to(source) for path in (source / "profiles").glob("*.example"))
    files = {path.as_posix(): hashlib.sha256((source / path).read_bytes()).hexdigest() for path in paths}
    stale = set(previous["files"]) - files.keys()
    changed = [name for name, digest in files.items()
               if not (destination / name).is_file()
               or hashlib.sha256((destination / name).read_bytes()).hexdigest() != digest]
    if args.check:
        if changed or stale or previous["files"] != files:
            manifest_changes = {name for name, digest in files.items()
                                if previous["files"].get(name) != digest}
            parser.exit(1, "AiApi snapshot differs: " + ", ".join(sorted(set(changed) | stale | manifest_changes)) + "\n")
        print("AiApi source snapshot is current")
        return
    for name in stale:
        path = destination / name
        if path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() != previous["files"][name]:
            parser.error("locally edited snapshot file: " + name)
    for name in changed:
        path = destination / name
        if path.exists() and (name not in previous["files"]
                             or hashlib.sha256(path.read_bytes()).hexdigest() != previous["files"][name]):
            parser.error("locally edited snapshot file: " + name)
    for name in changed:
        path = destination / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, path)
    for name in stale:
        (destination / name).unlink(missing_ok=True)
    destination.mkdir(parents=True, exist_ok=True)
    marker.write_text(json.dumps({"project": "AiApi-rs", "source": "https://github.com/Olyaham/AiApi-rs",
                                  "source_revision": source_revision(source),
                                  "content_sha256": snapshot_digest(files), "files": files},
                                 indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    print(f"AiApi snapshot: {len(files)} files, {len(changed)} updated")


if __name__ == "__main__":
    main()
