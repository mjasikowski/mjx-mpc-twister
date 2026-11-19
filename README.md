# MJX MPC Twister

Use your MIDI Fighter Twister as a hands-on control surface for the
**MPC Live III**. Reach all 16 Q-Links without switching banks on the MPC,
and jump between tracks without touching the screen.

## What you can do

- **Turn a knob to change a sound or setting.** Each knob controls one Q-Link.
  What it adjusts follows the current MPC screen and Q-Link assignments.
- **See where you are.** The light ring around each knob follows its Q-Link
  value. The light under the selected track's knob follows the track color.
- **Press a knob to select a track or open a shortcut.** The first 12 knobs
  select tracks in project order. The bottom row gives you four shortcuts:

| Push | Action | Switch LED |
| --- | --- | --- |
| 13 | Open Add Track | Green |
| 14 | Open Track Edit | Yellow |
| 15 | Ask before deleting the selected track | Red |
| 16 | Open Clip Matrix | Purple |

After setup, plug in or reconnect the Twister at any time. No MIDI input menu
or application restart is needed when you plug it in. With Add-In Restore
installed, it also comes back automatically when you turn on the MPC.

**Compatibility:** MPC Live III running **MPC OS 3.10.1.12** only.

## Installation with automatic startup

Use the `*-mpc-aarch64.zip` asset from [Releases](../../releases).
Extract it, verify `SHA256SUMS` from inside the extracted directory, and copy
the directory to your MPC. Save your project before installing.

1. From the extracted directory on the MPC, run `sh install.sh` over
   authorized root SSH. This is the standard Open MPC add-in installer.
2. Install the pinned [Open MPC Add-In Restore 1.2.0 script][restore]
   with `sh addin-restore-patch.sh install`. Its SHA-256 is
   `7cc1a2901f57b634874c28e5d2cb8912223ca5e489075031c5df5656218e2d20`.
3. Save your project and reboot the device. On startup, Add-In Restore
   restarts the MPC application once to load the add-in. No computer or
   action on the MPC is required on subsequent boots. Plug in the Twister
   and select Bank 1; the add-in also detects reconnects.

This setup was verified after a full reboot on an MPC Live III running
3.10.1.12. It does not edit the system image, projects, or preferences.
Check firmware compatibility before updating the device.

To remove Twister, save your project and run
`sh /data/mpc-addins/mjx-mpc-twister/uninstall.sh`. This leaves Add-In Restore
and other add-ins installed.

[restore]: https://raw.githubusercontent.com/open-mpc/mpc-vst-plugins/6fb514d0e553f8cd1872c91cf7d13caae061193f/tools/mpc_patch/addin_restore/addin-restore-patch.sh

### Twister MIDI profile

Use Bank 1. Knob turns use absolute CC 0–15 on MIDI channel 1. Knob presses
use **CC Toggle**, CC 0–15 on MIDI channel 2.

### Temporary installation

The older direct `*-mod.zip` uses separate scripts. For that ZIP only,
`sh install.sh --restart` installs for the current session and
`sh uninstall.sh --restart` removes it. A full device reboot also clears a
direct session installation.

## Build from source
With Python 3, a host C compiler, Zig 0.15.2, and public ALSA development
headers (for example, Debian's arm64 `libasound2-dev`), run from the repository root:

```sh
export ALSA_INCLUDE=/absolute/path/to/usr/include
bash build.sh
```

If using Python's `ziglang` package, set `ZIG_PYTHON` to the virtual
environment's Python. Build outputs, including the ZIP and its SHA-256,
appear in `dist/`. No Akai firmware files are packaged.

The ZIP from `build.sh` is for direct installation. Twister is a controller
add-in, not a VST2 instrument or effect.

### Open MPC catalog packaging

For a catalog release, use the [Open MPC add-in workflow][addin-workflow].
`build-catalog.sh` uses the upstream packager and standard installer and
validates the resulting Gen2 ZIP. It requires an approved root `LICENSE`;
use the same SPDX identifier in the build command and registry entry.

```sh
git -C /path/to/mpc-vst-plugins checkout --detach 864411da8fb0d88ad150d316ac6fea5f279113ea
bash build-catalog.sh /path/to/mpc-vst-plugins VERSION SPDX-LICENSE
```

Smoke-test the generated ZIP on a Live III with MPC OS 3.10.1.12 before
publishing it. Submit `catalog/plugins/mjx-mpc-twister.json` to Open MPC
with `kind: "addin"` and `asset_pattern: "*-mpc-aarch64.zip"`. The catalog
installer and the separate Add-In Restore patch serve different purposes:
the installer registers Twister for this session; the patch restores add-ins
on subsequent boots.

[addin-workflow]: https://github.com/open-mpc/mpc-vst-plugins/blob/main/docs/ADDINS.md#releasing

## Hardware checks

Connect the Twister in Bank 1. In a project with parameters assigned to
Q-Links, turn all 16 encoders, change the MPC's active Q-Link bank, and
repeat after unplugging and reconnecting the Twister. Verify that another MIDI
device and the hardware Q-Links still behave normally. Save and reload a test
project, then follow the uninstall instructions above and verify stock behavior.
A test project with 16 assigned parameters is needed to distinguish routed
turns from actual changes for every encoder. The MPC journal reports both
events per encoder and reports invoked push actions. Check the visible LED
colors and native dialogs separately; cancel Delete Track in smoke tests.
A successful install alone does not prove physical controller response.
