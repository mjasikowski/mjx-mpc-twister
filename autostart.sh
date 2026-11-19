#!/bin/sh
set -eu

usage() {
    echo "Usage: sh autostart.sh install /path/to/addin-restore-patch.sh" >&2
    echo "       sh autostart.sh uninstall" >&2
    exit 2
}

action=${1:-}
case "$action:$#" in install:2|uninstall:1) ;; *) usage ;; esac
test "$(id -u)" = 0
test "$(uname -m)" = aarch64

slot=/data/mpc-addins/mjx-mpc-twister
library=twister-control.so
package=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if test "$action" = uninstall; then
    test -d "$slot" && test ! -L "$slot"
    test -f "$slot/$library" && test ! -L "$slot/$library"
    cmp -s "$package/$library" "$slot/$library" ||
        { echo 'Installed library differs. Use the matching release to uninstall.' >&2; exit 1; }
    rm "$slot/$library"
    rmdir "$slot" 2>/dev/null || :
    echo 'Twister autostart removed. Save your project and reboot. Other add-ins and Add-In Restore are unchanged.'
    exit 0
fi

patch=$2
patch_hash=7cc1a2901f57b634874c28e5d2cb8912223ca5e489075031c5df5656218e2d20
actual=$(sha256sum < "$patch")
test "${actual%% *}" = "$patch_hash" ||
    { echo 'Add-In Restore checksum mismatch. Download the exact version linked in INSTALL.md.' >&2; exit 1; }
test "$(cat /usr/share/Akai/SME0/VERSION)" = 3.10.1.12
actual=$(sha256sum < /usr/bin/MPC)
test "${actual%% *}" = bd55df49a40bc030da13b67578d5bf88a33abd0b4b9fbdc1da7bc3db00bc61aa ||
    { echo 'Unsupported MPC executable.' >&2; exit 1; }
(cd "$package" && sha256sum -c SHA256SUMS)

mkdir -p /data/mpc-addins
if test -d "$slot" && test ! -L "$slot" &&
    test -f "$slot/$library" && test ! -L "$slot/$library" &&
    cmp -s "$package/$library" "$slot/$library"; then
    echo 'Matching Twister add-in already installed.'
    sh "$patch" install --confirmed
else
    mkdir "$slot"
    trap 'rm -f "$slot/$library"; rmdir "$slot"' EXIT
    cp "$package/$library" "$slot/$library"
    chmod 644 "$slot/$library"
    sh "$patch" install --confirmed
    trap - EXIT
fi
echo 'Twister autostart installed. Save your project and reboot the device to start it.'
