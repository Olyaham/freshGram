#!/usr/bin/env python3
"""Build and install an independent ASR component without building the client."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--flavor", choices=("cloud", "local", "cuda", "vulkan", "metal", "hipblas", "coreml", "sycl"), default="cloud")
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--target", action="append", default=[])
    parser.add_argument("--cargo", default="cargo")
    parser.add_argument("--profile", choices=("dev", "release"), default="dev")
    args = parser.parse_args()
    subprocess.run([sys.executable, str(Path(__file__).with_name("sync_aiapi.py")), "--check"], check=True)
    crate = Path(__file__).resolve().parents[1] / "Telegram/ThirdParty/voice-asr"
    feature = "local" if args.flavor == "local" else f"local-{args.flavor}"
    targets = list(dict.fromkeys(args.target)) or [None]
    if len(targets) > 1 and not all("apple-darwin" in target for target in targets):
        parser.error("multiple targets require a macOS universal component")
    target_os = "windows" if any(target and "windows" in target for target in targets) else "macos" if any(target and "apple-darwin" in target for target in targets) else sys.platform
    name = "voice_asr.dll" if target_os in ("windows", "win32") else "libvoice_asr.dylib" if target_os in ("macos", "darwin") else "libvoice_asr.so"
    artifacts = []
    for target in targets:
        command = [args.cargo, "build", "--lib", "--locked", "--no-default-features", "--profile", args.profile, "--manifest-path", str(crate / "Cargo.toml"), "--target-dir", str(args.build_dir)]
        if target:
            command += ["--target", target]
        if args.flavor != "cloud":
            command += ["--features", feature]
        subprocess.run(command, check=True)
        artifacts.append(args.build_dir / (target or "") / ("debug" if args.profile == "dev" else "release") / name)
    args.destination.mkdir(parents=True, exist_ok=True)
    output = args.destination / name
    descriptor, temporary = tempfile.mkstemp(prefix=f".{name}-", dir=args.destination)
    os.close(descriptor)
    try:
        if len(artifacts) == 1:
            shutil.copy2(artifacts[0], temporary)
        else:
            subprocess.run(["lipo", "-create", *map(str, artifacts), "-output", temporary], check=True)
        os.replace(temporary, output)
    finally:
        Path(temporary).unlink(missing_ok=True)
    for root, prefix in ((crate, ""), (crate.parent / "aiapi", "AiApi-")):
        for notice in ("LICENSE-MIT", "ATTRIBUTION.md"):
            shutil.copy2(root / notice, args.destination / (prefix + notice))
    print(output)


if __name__ == "__main__":
    main()
