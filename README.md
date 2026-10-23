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
2. In the repository: **Settings > Pages > Source: GitHub Actions**.
3. Make a release: push a tag, e.g. `git tag v1.0 && git push --tags`, or **Actions > release > Run workflow**.
   The version is `X.Y`, one digit each; the minor digit becomes the package identity `FM-1_96Y`.
4. The workflow runs the host test, downloads JieLi's Linux compiler and the AC79 SDK, builds `formation-X.Y.fwsc`,
   attaches it to the release and publishes the installer and the manual to your Pages site.

If the build step fails, open the failed run's log and send it to whoever helps you: the host test runs first and tells
you whether the problem is in the code or in the toolchain.

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
| `web/`, `tools/make_site.py` | the web installer |
| `tests/` | the host mode and its scripts |

Formation is built on [Dinghy](https://github.com/zvenson/dinghy), the platform of
[Felucca](https://github.com/hugelton/Felucca) and [SLOOP](https://github.com/isod89/sloop-fm1); see
[LICENSING.md](LICENSING.md).
