#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
version=0.1.0
output="dist/mjx-mpc-twister-$version"
mkdir -p "$output"
"${HOST_CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -Isrc tests/encoder_test.c -o "$output/test"
"$output/test"
rm "$output/test"
sh -n install.sh
sh -n uninstall.sh
if [[ -n ${ZIG_PYTHON:-} ]]; then
    compiler=("$ZIG_PYTHON" -m ziglang cc -target aarch64-linux-gnu.2.34)
else
    compiler=(zig cc -target aarch64-linux-gnu.2.34)
fi
"${compiler[@]}" -I"${ALSA_INCLUDE:?Set ALSA_INCLUDE to the directory containing alsa/asoundlib.h}" \
    -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -mbranch-protection=standard \
    -shared -Wl,--no-undefined -pthread src/twister.c -ldl -o "$output/twister-control.so"
cp install.sh uninstall.sh "$output/"
cp README.md "$output/INSTALL.md"
python3 - "$output" <<'PY'
import hashlib
import sys
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

folder = Path(sys.argv[1])
files = ["INSTALL.md", "install.sh", "uninstall.sh", "twister-control.so"]
checksums = "".join(f"{hashlib.sha256((folder / f).read_bytes()).hexdigest()}  {f}\n" for f in files)
(folder / "SHA256SUMS").write_text(checksums, encoding="ascii")
archive = folder.with_name(folder.name + "-mpc-live3-3.10.1.12-mod.zip")
with ZipFile(archive, "w", compression=ZIP_DEFLATED) as z:
    for name in files + ["SHA256SUMS"]:
        z.write(folder / name, name)
archive.with_name(archive.name + ".sha256").write_text(
    f"{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n", encoding="ascii")
print(archive)
PY
