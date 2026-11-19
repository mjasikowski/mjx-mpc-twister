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

## Installation

1. Download the ZIP and its `.sha256` file from [Releases](../../releases).
2. Verify the download with `sha256sum -c <archive>.zip.sha256`
   (or `shasum -a 256 -c <archive>.zip.sha256` on macOS).
3. Save your project. Extract the ZIP, copy the extracted directory to the
   MPC, and run `sh install.sh --restart` from that directory over authorized
   root SSH. This restarts the MPC application.
4. Plug in the Twister and select Bank 1. The integration starts
   automatically with the MPC application while installed; no separate step
   is needed when the controller is connected or reconnected.

The installer verifies the exact MPC executable and rolls back if startup
checks fail. It does not edit firmware, projects, or saved preferences. A full
device reboot clears the runtime installation; run the installer again after
boot. To remove it without rebooting the device, save your project and run
`sh uninstall.sh --restart`.

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

The ZIP follows the manual release steps in the
[Open MPC workflow](https://open-mpc.github.io/workflow.html): test, package
with install/uninstall scripts and checksums, smoke-test that ZIP on device,
then attach the tested ZIP and its checksum to a release. This mod is not a
VST2 instrument or effect and is **not** eligible for the VST catalog,
`release.py`, `catalog_check.py`, or the Plugin Manager.

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
