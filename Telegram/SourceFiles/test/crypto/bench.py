import argparse
import pathlib
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("binary", type=pathlib.Path)
parser.add_argument("--quick", action="store_true")
args = parser.parse_args()
binary = args.binary.resolve(strict=True)
print("mode,operation,bytes,iterations,median_us,p95_us,mib_per_s,rss_before_kib,peak_rss_kib", flush=True)
sizes = {
    "inline": [32, 1024, 64 * 1024, 1024 * 1024],
    "text": [32, 1024, 64 * 1024, 1024 * 1024],
    "stream": [1024 * 1024, 16 * 1024 * 1024, 128 * 1024 * 1024],
}
for mode, values in sizes.items():
    for size in values[:2] if args.quick else values:
        iterations = 3 if args.quick or mode == "stream" else 101
        for operation in ["seal", "open"]:
            subprocess.run(
                [str(binary), "--mode", mode, "--operation", operation,
                 "--bytes", str(size), "--iterations", str(iterations)],
                check=True,
                stdout=sys.stdout,
            )
