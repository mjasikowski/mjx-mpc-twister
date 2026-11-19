# MJX MPC Twister

Standalone MIDI Fighter Twister integration for the **MPC Live III**, on the
exact **MPC OS 3.10.1.12** AArch64 build. The first bank's channel 1 CC 0–15 (absolute)
target 16 fixed logical Q-Links independently of the MPC's active Q-Link
bank. The mod subscribes directly to the Twister at startup and on hotplug;
no MPC MIDI input preference or MPC restart is needed after connecting it.
Ring feedback follows MPC's Q-Link positions after a short idle window. The
first CC establishes a baseline if no feedback has yet been sent.

Bank 1 pushes must use **CC Toggle**, MIDI channel 2, CC 0–15:

| Push | Action | Switch LED |
| --- | --- | --- |
| 1–12 | Select the corresponding existing track | Selected track's approximate hue |
| 13 | Open Add Track | Green |
| 14 | Open Track Edit | Yellow |
| 15 | Open MPC's native Delete Track confirmation | Red |
| 16 | Open Clip Matrix | Purple |

The installer refuses existing MPC service overrides or an existing preload
rather than trying to combine unrelated mods.

## Installation with automatic startup

1. Download the ZIP and its `.sha256` file from [Releases](../../releases).
2. Verify the download with `sha256sum -c <archive>.zip.sha256`
   (or `shasum -a 256 -c <archive>.zip.sha256` on macOS).
3. Download the pinned [Open MPC Add-In Restore 1.2.0 script][restore].
   Extract the ZIP and copy the extracted files and the script to the MPC.
4. From the extracted directory, run over authorized root SSH:
   `sh autostart.sh install /path/to/addin-restore-patch.sh`.
   The installer verifies the script checksum and exact firmware build.
5. Save your project and reboot the device. On startup, Add-In Restore
   restarts the MPC application once to load the add-in. No computer or
   action on the MPC is required on subsequent boots. Plug in the Twister
   and select Bank 1; the add-in also detects reconnects.

This setup was verified after a full reboot on an MPC Live III running
3.10.1.12. It does not edit the system image, projects, or preferences.
Check firmware compatibility before updating the device.

To remove Twister autostart, use the same release and run
`sh autostart.sh uninstall`, then save your project and reboot.
This leaves Add-In Restore and other add-ins installed.

[restore]: https://raw.githubusercontent.com/open-mpc/mpc-vst-plugins/6fb514d0e553f8cd1872c91cf7d13caae061193f/tools/mpc_patch/addin_restore/addin-restore-patch.sh

### Temporary installation

For a session-only trial, save your project and run `sh install.sh --restart`
instead. `sh uninstall.sh --restart` removes that temporary installation.
A full device reboot also clears it.

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

This ZIP is distributed directly. Twister is a controller add-in, not a VST2
instrument or effect, and this package is not listed in the Plugin Manager.

## Hardware checks

Connect the Twister in Bank 1. In a project with parameters assigned to
Q-Links, turn all 16 encoders, change the MPC's active Q-Link bank, and
repeat after unplugging and reconnecting the Twister. Verify that another MIDI
device and the hardware Q-Links still behave normally. Save and reload a test
project, then run `sh uninstall.sh --restart` and verify stock behavior.
A test project with 16 assigned parameters is needed to distinguish routed
turns from actual changes for every encoder. The MPC journal reports both
events per encoder and reports invoked push actions. Check the visible LED
colors and native dialogs separately; cancel Delete Track in smoke tests.
A successful install alone does not prove physical controller response.
