#!/bin/sh
set -eu
cd "$(dirname "$0")"
test "${1:-}" = --restart || { echo 'Save your project, then run: sh install.sh --restart' >&2; exit 2; }
test "$(id -u)" = 0
sha256sum -c SHA256SUMS
test "$(uname -m)" = aarch64
test "$(cat /usr/share/Akai/SME0/VERSION)" = 3.10.1.12

runtime=/run/mjx-mpc-twister
dropdir=/run/systemd/system/acvs.service.d
entry=$dropdir/90-mjx-mpc-twister.conf
rollback=mjx-mpc-twister-rollback
expected=bd55df49a40bc030da13b67578d5bf88a33abd0b4b9fbdc1da7bc3db00bc61aa
pid=$(systemctl show acvs.service -p MainPID --value)
case "$pid" in ''|*[!0-9]*|0|1) exit 1;; esac
test "$(readlink "/proc/$pid/exe")" = /usr/bin/MPC
test "$(sha256sum "/proc/$pid/exe" | cut -d ' ' -f1)" = "$expected"
test "$(sha256sum /usr/bin/MPC | cut -d ' ' -f1)" = "$expected"
test "$(systemctl show acvs.service -p DropInPaths --value)" = "" ||
    { echo 'Existing MPC override; cannot safely combine independent mods.' >&2; exit 1; }
test ! -e "$entry" && test ! -L "$entry"
if tr '\000' '\n' < "/proc/$pid/environ" | grep -q '^LD_PRELOAD='; then
    echo 'Existing MPC preload; not replacing it.' >&2
    exit 1
fi

umask 077
mkdir "$runtime"
timer=0
failure() {
    result=$?
    trap - EXIT
    if test "$result" != 0; then
        if test "$timer" = 1; then
            systemctl start "$rollback.service" || true
        else
            rm -f "$runtime/twister-control.so" "$runtime/uninstall.sh" "$runtime/dropin"
            rmdir "$runtime"
        fi
    fi
    exit "$result"
}
trap failure EXIT
cp twister-control.so uninstall.sh "$runtime/"
cat > "$runtime/dropin" <<EOF
[Service]
Environment="LD_PRELOAD=$runtime/twister-control.so"
EOF
mkdir -p "$dropdir"
systemd-run --unit="$rollback" --on-active=150s /bin/sh "$runtime/uninstall.sh" --restart
timer=1
(set -C; cat "$runtime/dropin" > "$entry")
systemctl daemon-reload
systemctl restart acvs.service
i=0
while test "$i" -lt 50; do
    new=$(systemctl show acvs.service -p MainPID --value)
    restarts=$(systemctl show acvs.service -p NRestarts --value)
    test "$restarts" = 0 || exit 1
    if test "$new" != "$pid" && systemctl is-active --quiet acvs.service &&
        grep -Fq "$runtime/twister-control.so" "/proc/$new/maps" 2>/dev/null; then
        log=$(journalctl -b -u acvs.service "_PID=$new" --no-pager -o cat)
        if printf '%s\n' "$log" | grep -Fxq 'twister-control: hotplug listener ready' &&
            printf '%s\n' "$log" | grep -Fxq 'twister-control: 16-slot adapter captured' &&
            printf '%s\n' "$log" | grep -Fxq 'twister-control: native UI captured'; then
            systemctl stop "$rollback.timer"
            echo 'MJX MPC Twister is active and automatically detects the controller. A full reboot removes this installation.'
            exit 0
        fi
    fi
    i=$((i+1))
    sleep 1
done
echo 'Native audio/UI adapter or hotplug listener was not ready; rolling back.' >&2
exit 1
