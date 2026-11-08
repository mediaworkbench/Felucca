# Building Felucca for the M-VAVE FM-1

This repository builds firmware for the FM-1 only. Host-side tests are development tools, not supported application platforms.

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

## Get the source

Two parts are git submodules: six engines (`firmware/engines/`, [Felucca-Engines](https://github.com/hugelton/Felucca-Engines),
MIT) and the hardware layer (`firmware/bsp/`, [felucca-bsp](https://github.com/hugelton/felucca-bsp), MPL-2.0), so clone
with `--recurse-submodules`:

```
git clone --recurse-submodules https://github.com/hugelton/Felucca.git
```

In a clone made without it, run `git submodule update --init` once. GitHub's "Download ZIP" and the source archives
it adds to a release leave `firmware/engines/` and `firmware/bsp/` empty; each release also has
`felucca-X.Y.Z-source.tar.gz`, the whole source with the submodules' files in it. The build stops with
"firmware/engines is empty" or "firmware/bsp is empty" when they are missing.

## Prerequisites (macOS)

- Python 3 with Pillow and fontTools: `pip3 install Pillow fonttools` (the UI font and icons are
  rasterised at build time)
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 1.8.1` makes a release build: the package identity becomes `FM-1_918`
and the version string `v1.8.1`; the package is `build/felucca-1.8.1.fwsc`, and
`build/release-1.8.1/` holds what a release ships: the package, the app
(`felucca-1.8.1-app.bin`), `SHA256SUMS`, the sample attribution, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it).

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`, `core.h` and `icons.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_CDC_DEFAULT` | 1 | `0`: the console is built in but left out of USB from boot (as MENU > USB SERIAL OFF) |
| `FELUCCA_UAC` | 1 | USB audio input (the master output, stereo, 44.1 or 48 kHz) |
| `FELUCCA_UAC_48K` | 1 | `0`: the USB audio input at 44.1 kHz only, as up to 1.0.5.2 (`firmware/bsp/chip/usb.c`) |
| `FELUCCA_UAC_AS_FIRST` | 1 | the audio control header lists the audio streaming interface before the MIDI one, so the USB audio driver of older macOS (up to 15) finds it; `0`: 1.0 to 1.5's order, byte for byte (`firmware/bsp/chip/usb.c`) |
| `FELUCCA_CPU2` | 1 | the second core is built in (MENU > SYSTEM > 2ND CORE, OFF by default); `0`: none of it, 1.6's one-core code and RAM (`firmware/bsp/chip/fm1_cpu1.h`, `firmware/src/fx.c`) |
| `FELUCCA_UART` | 1 | TRS MIDI IN |
| `FELUCCA_SLICE` | 0 | the SLICE engine and its BREAK (`0`: engine 13 reserved, so DCO stays 14; this fork's default, upstream's is 1) |
| `FELUCCA_SAMPLE` | 0 | the SAMPLE engine (4) and the built-in sample sets PIANO, FLUTE, SAX (`tools/gen_samples.py`); `0`: engine 4 reserved, GRAIN plays the user slots USR1..3 only (this fork's default) |
| `FELUCCA_LOFI` | 0 | the LOFI engine (3, Felucca-Engines); `0`: reserved (this fork's default) |
| `FELUCCA_VOICE` | 0 | the VOICE engine (5, `firmware/src/eng_formant.c`); `0`: reserved (this fork's default) |
| `FELUCCA_ICONS` | 1 | parameter icons on the knob cards |
| `FELUCCA_303` | 1 | the 303 engine (X0X's Open303, `firmware/src/eng_303.c`, `x0x/`) and TB-3PO, its ARP (`0`: engine 15 reserved, no float code) |
| `FELUCCA_AICD` | 1 | TB-3PO's MODE AICD: Aicd's acid line model (`assets/aicd/lm.js`, ~24 KB of flash as `build/gen/felucca_aicd.h` by `tools/gen_aicd.py`); `0`: no MODEL page, TB-3PO's own lines only |
| `FELUCCA_DBRAIN` | 1 | a DRUM track's BRAIN pages: DrumBrain's pattern model (`assets/drumbrain/model.js`, ~19 KB of flash as `build/gen/felucca_dbrain.h` by `tools/gen_dbrain.py`); `0`: no such pages |
| `FELUCCA_FM4` | 0 | the retired DIGITAL engine (4-operator FM) instead of its FM6 conversion (a test build: the DCO and 303 engines are left out, the EDIT layer has room for 15) |
| `FELUCCA_ADPCM` | 0 | upstream's ADPCM engine (`firmware/src/eng_adpcm.c`, upstream's engine 14): kept in the tree, not available in this fork (14 is DCO, 15 the 303); `1` stops the build |

`FELUCCA_USB_LAYOUT` (`0` to `3`, default `0`) picks other USB descriptor layouts for testing; see
`firmware/bsp/chip/usb.c`.

The app is compiled for the single-precision FPU (`-mcpu=r3 -mfprev1 -ffp-contract=off`, `tools/build.py` `FPU`): the 303
engine is float (X0X's port of Open303). The flags change no instruction of the integer code. Only the 303 may use float:
the build fails on `float` or `double` in any other source (`float_check`) and on an FPU instruction outside the 303's
functions in the image. The host tests build it with `-ffp-contract=off` too (and `eng_303.c` sets the pragma), so a host
render is the device's, sample for sample.

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works, with no
instrument sets in the SAMPLE engine and no PIANO in SLICE (the PIANO set's middle C): they play a plain sine
at the note's pitch instead, and the screen says NO SAMPLE.

## Tests

```
tests/run_tests.sh
```

Runs the firmware's host-side unit and regression tests and, with Node.js, the installer/editor tests. Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build). The suites cover flash storage,
user presets, projects of every format, backup, the keys and knobs, MIDI (USB, TRS, clock,
control), USB audio, the update entry and loader, the command-line installer, the UI (the real
drawing code against stubs: every screen in every palette is rendered and checked for clipped or
overlapping text; PNGs land in `build/ui_new/`), every engine (DRUM, NOISE, PHYS, FM6, SLICE, DCO, 303 against X0X's own code, the
DIGITAL conversion), TB-3PO against X0X's float generator (`tests/ref/`), the chord keys, the modulation matrix, the FX layer, the reverbs and the BBD chorus, the SLICER
and swing. With `DAISYSP` pointing at a DaisySP checkout, the PHYS models are also compared with
their floating-point originals; without it that test is skipped. Likewise with `SNES_SPC` pointing at a
snes_spc 0.9.0 checkout, the ADPCM engine's voice is compared sample for sample with its `SPC_DSP.cpp`
(`tests/adpcm_ref.cpp`, needs `c++`); without it the rest of the ADPCM test runs. Felucca-Engines' own test
(`firmware/engines/tests/run.sh`) runs too, and its standalone host is built once more with Felucca's tables
against the same golden renders.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

Use the web installer in Chrome or Edge:
<https://hugelton.github.io/Felucca/webapp/installer/>. It installs the released package.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/felucca.fwsc dev /tmp/felucca-site
cd /tmp/felucca-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).
