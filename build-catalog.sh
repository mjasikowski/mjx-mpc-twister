#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

if [[ $# != 3 ]]; then
    echo 'Usage: bash build-catalog.sh /path/to/mpc-vst-plugins VERSION SPDX-LICENSE' >&2
    exit 2
fi
tools=$(cd "$1" && pwd)
version=$2
license=$3
ref=864411da8fb0d88ad150d316ac6fea5f279113ea
test "$(git -C "$tools" rev-parse HEAD)" = "$ref" ||
    { echo "Check out Open MPC tools at $ref." >&2; exit 1; }
test -f LICENSE ||
    { echo 'Add a LICENSE file approved by the copyright holder before packaging for the catalog.' >&2; exit 1; }

stage=$(mktemp -d)
trap 'rm -f "$stage/addin.manifest" "$stage/twister-control.so" "$stage/LICENSE"; rmdir "$stage"' EXIT
bash build.sh --library "$stage/twister-control.so"
cp addin.manifest LICENSE "$stage/"
python3 "$tools/tools/release_addin.py" --dir "$stage" --version "$version" \
    --repo mjasikowski/mjx-mpc-twister --license "$license" \
    --about 'MIDI Fighter Twister control for MPC Live III: 16 Q-Links, LED feedback and encoder push actions.' \
    --requires 'MPC Live III with the exact MPC OS 3.10.1.12 build. Automatic startup after a full reboot requires Open MPC Add-In Restore.' \
    -o dist
python3 "$tools/tools/catalog_check.py" "dist/MJX-MPC-Twister-$version-mpc-aarch64.zip" \
    --catalog --expect-id mjx-mpc-twister --expect-repo mjasikowski/mjx-mpc-twister
