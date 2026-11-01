# Licensing

Formation is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The app `firmware/src/formation.c`, the web installer page `web/index.html`, the emulator page `web/emulator.html`, `tools/make_site.py`, `tools/build_emu.py`, the host and browser modes (`tests/web.c`, `tests/emu_test.mjs`) and tests, the docs | Copyright (C) 2026 Grand (Grandsummoner), a port of the plugin Navy Arp 2 (<https://github.com/Grandsummoner/navy-arp2>) |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` (except `make_site.py`), `web/fm1pkg.js`, `web/fm1ota.js` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| The platform wiring `firmware/src/platform.c`, the host mode `tests/host.c`, `docs/API.md`, `docs/RULES.md` | zvenson ([Dinghy](https://github.com/zvenson/dinghy)), changed here for Formation |
| The Bluetooth LE MIDI stack: `firmware/src/ble/ble_*`, `rf_share.h`, `firmware/hal/fm1_ble.h`, `fm1_ble_rf.h`, taken unchanged from [fm1-dock](https://github.com/joakimeriksson/fm1-dock) commit `4248805` (they are [Optimist](https://github.com/w0ts/optimist)'s, commit `5b06939`, written from the Bluetooth Core and BLE-MIDI specifications) | Copyright (C) 2026 w0ts (Optimist), GPL-3.0-only |
| The glue around the stack, `firmware/src/ble/fm_ble.c` | adapted from fm1-dock's `hx.c`, Copyright (C) 2026 Joakim Eriksson, GPL-3.0-only; changed here for Formation |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| The BLE radio's start-up values, `firmware/hal/ble_rf_tables_v15.h` (through `firmware/gen/ble_rf_tables.h`): register writes and tables captured by Optimist from M-VAVE's stock FM-1 V15 firmware. Optimist's `LICENSING.md` traces them to JieLi's radio start-up and M-VAVE's V15 package, both published under Apache-2.0; they are not Optimist's work. Check that note against Optimist's before you ship a build | Apache-2.0 | `LICENSES/Apache-2.0.txt`, <https://github.com/w0ts/optimist> |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | `LICENSES/Apache-2.0.txt`, <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE" and "FM-1" belong to their owners.
Formation is not made or endorsed by them.
