#!/bin/sh
set -eu
test "${1:-}" = --restart || { echo 'Save your project, then run: sh uninstall.sh --restart' >&2; exit 2; }
test "$(id -u)" = 0
runtime=/run/mjx-mpc-twister
entry=/run/systemd/system/acvs.service.d/90-mjx-mpc-twister.conf
if test -e "$entry" || test -L "$entry"; then
    test -f "$entry" && test ! -L "$entry" && cmp -s "$entry" "$runtime/dropin" ||
        { echo 'Refusing to remove an override not owned by MJX MPC Twister.' >&2; exit 1; }
    mv "$entry" "$runtime/restart-pending"
fi
if test -f "$runtime/restart-pending"; then
    systemctl daemon-reload
    systemctl restart acvs.service
    rm "$runtime/restart-pending"
fi
systemctl stop mjx-mpc-twister-rollback.timer 2>/dev/null || true
if test -d "$runtime"; then
    test ! -L "$runtime"
    rm -f "$runtime/twister-control.so" "$runtime/uninstall.sh" "$runtime/dropin"
    rmdir "$runtime"
fi
echo 'MJX MPC Twister removed.'
