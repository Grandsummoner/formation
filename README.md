# Formation for the M-VAVE FM-1

A port of [Navy Arp 2](https://github.com/Grandsummoner/navy-arp2), the dual-scene probabilistic arpeggiator and step
sequencer, to the FM-1: custom firmware that plays the FM-1's own voices and sends MIDI over USB. Free software
(GPL-3.0). **Install:** use the web installer of your GitHub Pages site (after the first release, see below), in Chrome
or Edge, with the FM-1 connected by USB.

[Manual](docs/MANUAL.md) · [What the platform gives you](docs/API.md) · [Rules of the chip](docs/RULES.md)

Not made or supported by M-VAVE. Use it at your own risk; hold **OCT-** while switching the FM-1 on for the rescue
updater, or go back to the official firmware through the [FM-1 Firmware Hub](https://thefirmwarehub.com).

## Get it built and published (GitHub does everything)

1. Make a GitHub repository and push this tree to it.
2. In the repository: **Settings > Pages > Source: GitHub Actions** (once).
3. That is all. **Every push to `main` builds and publishes**, in about two minutes:
   * the web installer, the browser emulator and the manual on your Pages site,
   * `formation-X.Y.fwsc` on the release called **latest**
     (`https://github.com/<you>/<repo>/releases/download/latest/formation-X.Y.fwsc`).
   The version is the first line of the file `VERSION` (`X.Y`, one digit each; the minor digit becomes the package
   identity `FM-1_96Y`). Push a tag such as `v1.1` for a numbered release. A build that fails publishes nothing.

If a build fails, open the failed run in the Actions tab and send the log of the failed step. The host test runs first and
tells you whether the problem is in the code or in the toolchain.

## Try it in a browser (emulator)

`emulator.html` on your Pages site runs the firmware's own code, compiled to WebAssembly, with a faceplate: click the
keys and buttons, turn the knobs with the mouse wheel, switch the sound on, watch the MIDI it sends (and send it to a
real MIDI port). Your presets are kept in the browser. The release workflow builds it with
`python3 tools/build_emu.py` (needs `pip install ziglang`) and tests it with `node tests/emu_test.mjs`; the page is
`tools/make_site.py` putting `build/emu.wasm` into `web/emulator.html`.

## Build and try it on your own computer

* **No hardware:** `sh tests/run_host.sh tests/full.txt` runs the firmware on this computer (needs a C compiler and Pillow):
  the MIDI it sends is printed, the sound goes to `build/host/formation.wav`, and the screens come out as PNG.
  `tests/host.c` documents the script format (buttons, knobs, keys, MIDI in).
* **The firmware:** needs Linux x86-64 (macOS through Docker). `sh tools/get_toolchain.sh`, then clone the SDK
  (`git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK`),
  then `sh build.sh --release 1.0`. The package is `build/formation-1.0.fwsc`.

## Layout

| | |
| --- | --- |
| `firmware/src/formation.c` | the app: engine, voices, controls, screen |
| `firmware/src/platform.c` and the rest of `firmware/` | the platform (Dinghy, Felucca, SLOOP): USB, updates, screen, flash |
| `web/`, `tools/make_site.py`, `tools/build_emu.py` | the web installer and the browser emulator |
| `tests/` | the host mode (`host.c`, scripts) and the browser mode (`web.c`, `emu_test.mjs`) |

Formation is built on [Dinghy](https://github.com/zvenson/dinghy), the platform of
[Felucca](https://github.com/hugelton/Felucca) and [SLOOP](https://github.com/isod89/sloop-fm1); see
[LICENSING.md](LICENSING.md).
