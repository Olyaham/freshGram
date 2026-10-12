from pathlib import Path
import re
import sys


def extract(path, marker):
    source = path.read_text()
    start = source.index(marker)
    opening = source.index("{", start)
    masked = re.sub(
        r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        lambda match: " " * len(match.group()),
        source,
        flags=re.S,
    )
    depth = 0
    for position in range(opening, len(source)):
        if masked[position] == "{":
            depth += 1
        elif masked[position] == "}":
            depth -= 1
            if depth == 0:
                line = source.count("\n", 0, start) + 1
                return f'#line {line} "{path.as_posix()}"\n' + source[start:position + 1]
    raise ValueError(f"Unterminated function: {marker}")


root = Path(sys.argv[1])
output = Path(sys.argv[2])
pieces = [
    "namespace MTP {\n",
    extract(
        root / "mtproto/mtproto_auth_key.cpp",
        "void aesIgeDecryptRaw(",
    ),
    "\n",
    extract(
        root / "mtproto/mtproto_auth_key.cpp",
        "void AuthKey::prepareAES_oldmtp(",
    ),
    "\n",
    extract(
        root / "mtproto/mtproto_auth_key.h",
        "inline void aesDecryptLocal(",
    ),
    "\n} // namespace MTP\n",
    extract(root / "core/utils.cpp", "int32 *hashSha1("),
    "\nnamespace Storage::details {\n",
    extract(
        root / "storage/details/storage_file_utilities.cpp",
        "bool DecryptLocal(",
    ),
    "\n} // namespace Storage::details\n",
]
output.write_text("".join(pieces))
