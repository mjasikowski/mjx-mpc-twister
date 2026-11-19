# Hardware verification

## 1.0.1: MPC Live III (Gen2), MPC OS 3.10.1.12

Tested on November 19, 2026, using the exact Open MPC catalog ZIP.

| Check | Result |
| --- | --- |
| Open MPC `catalog_check.py --catalog` | Pass, no warnings |
| Install over the existing Twister installation | Standard installer reported `checked: MPC loaded twister-control.so`; native UI initialized |
| Uninstall | Standard uninstaller removed the library; stock MPC was active and the library was absent from the process |
| Install from a clean add-in slot | Standard installer loaded the library; native UI initialized |
| Full device reboot with Open MPC Add-In Restore 1.2.0 | Restore recreated the runtime drop-in, restarted the MPC application once, and loaded Twister without a computer |
| Connected hardware | Twister was detected and rotary LED feedback was sent after reboot; no MPC crash restarts |

The control library is byte-identical to the v1.0.0 library. This catalog
smoke test did not repeat the physical sweep of every encoder or verify every
push action. Do not assume compatibility with other Gen2 models or builds.
