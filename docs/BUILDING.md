# Building and installing

To use Zrno you don't need this: download a release and run its installer
(see the [README](../README.md#getting-it-onto-a-camera)). This guide builds
it from source, for the **Canon PowerShot G12 with firmware 1.00G**. No ROM
dump is needed: the camera copies the part of its firmware Zrno hooks into
from its own ROM ([ARCHITECTURE.md](ARCHITECTURE.md)).

## What you need

- Python 3, a C compiler and `pip install -r requirements.txt`.
- The cross compiler Zrno is tested with:
  [xPack GNU Arm Embedded GCC 10.3.1-2.3](https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/tag/v10.3.1-2.3/).
- macOS, for the card installers.

## Build

```bash
python3 tools/fetch_chdk.py
export CROSS_COMPILE=/path/to/xpack-arm-none-eabi-gcc-10.3.1-2.3/bin/arm-none-eabi-
SOURCE_DATE_EPOCH=$(git log -1 --format=%ct) python3 tools/build_firmware.py
```

`fetch_chdk.py` downloads CHDK release-1_6 r6355 (the G12 port and the shared
code) into `build/chdk-r6355` and checks every file against
`tools/chdk-r6355.sha256`. `build_firmware.py` copies that tree, applies Zrno
with `tools/integrate.py`, runs CHDK's `make`, and puts `DISKBOOT.BIN` and
CHDK's modules into `card/`. `SOURCE_DATE_EPOCH` fixes the build time, so
the same source gives byte-identical firmware.

Build flags, set in the environment: `FILM_HWMASK=1` (an experimental
hardware Bloom mask) and `FILM_DEVTRACE=1` (logs the develop task's
messages).

## Other firmware versions (untested)

CHDK r6355 also has G12 ports for firmware 1.00C, 1.00E and 1.00F. Zrno can be
built from each of them, but **none of these builds has been tested on a
camera**:

```bash
SOURCE_DATE_EPOCH=$(git log -1 --format=%ct) python3 tools/build_firmware.py --fw 100f   # or 100c, 100e, all
SOURCE_DATE_EPOCH=$(git log -1 --format=%ct) python3 tools/package_release.py --fw 100f  # or all
```

Each one uses CHDK's own addresses for that firmware, and CHDK's loader still
checks the firmware version, so a build starts only on its own firmware (on
any other, the camera's LED blinks and CHDK does not start). Its files go to
`build/firmware-g12-FW-untested/`, never to `card/`, and its release files say
so: `zrno-VERSION-g12-FW-untested.zip`,
`zrno-VERSION-g12-FW-untested-chdk-source.zip` and
`SHA256SUMS-g12-FW-untested`, named like the 1.00G files, with the firmware
and "UNTESTED" in `INSTALL.TXT` and in the installer.

What is known about them: CHDK's ports put Canon's code below 0xff980000 (1.00C)
and 0xffaf0000 (1.00E, 1.00F) at the same addresses as 1.00G, and Zrno's
patches to the port apply unchanged. Zrno's own addresses are 1.00G's, and
its checks of Canon's code decide on the camera:
- the develop task (`camera/film_reloc.c`): if its hash differs, the camera
  runs Canon's own task, Zrno leaves the photos alone and its indicator says
  "unknown firmware";
- the colour table, preview reload and sharpening (`camera/film_hwcolour.c`):
  each one is refused if Canon's code or table is not where 1.00G keeps it.

CHDK in EXMEM (`MEMISOSTART` 0x79089e0) was checked only on 1.00G
(`tools/exmem_boot_check.py`).

## Install

Package a release (below), unzip `build/release/zrno-VERSION-g12-100g.zip`
and double-click its `install.command`, as a user would.

While developing, `tools/install_card.py` updates a mounted card in place,
backs up every file it replaces into `card-install-*/`, and keeps the card's
settings. It is a dry run until `--apply`; `--restore DIR --apply` rolls back,
and `--luts-only` changes only the looks.

## Release

```bash
python3 tools/package_release.py
```

This writes, to `build/release/`:

- `zrno-VERSION-g12-100g.zip`: the card files, `CARD.SHA256`, `VERSION`,
  `install.command`, `INSTALL.TXT` (from [INSTALL.txt](INSTALL.txt)),
  `COPYING` and `THIRD_PARTY.md`;
- `zrno-VERSION-g12-100g-chdk-source.zip`: the CHDK tree with Zrno
  integrated, the source the GPL asks for, to attach next to the card zip.
  Unzipped, the `make` command in its `ZRNO-BUILD.TXT` builds the same
  `DISKBOOT.BIN`;
- `SHA256SUMS-g12-100g`.

On GitHub, [release.yml](../.github/workflows/release.yml) does all of this
for a version tag: set `version` in `card-manifest.json`, commit, then push
the tag (`git tag v0.8.3 && git push origin v0.8.3`). It builds, runs the
tests, and attaches the files to a draft release to check and publish: the
1.00G files and the untested 1.00C, 1.00E and 1.00F ones. The release text
comes from [RELEASE-NOTES.md](RELEASE-NOTES.md), with `{version}` filled in.

## Tests

```bash
python3 tools/test_native.py
bash tests/test_installer.sh   # macOS, after packaging
```

The first runs the camera's C on the host with AddressSanitizer and UBSan,
the menu and the shooting screen in a simulator of CHDK's own code (with
`build/chdk-r6355`), and the develop task relocator on synthetic ROMs. The second runs the installer on a
disk image.

With your own ROM dump in `rom/PRIMARY.BIN` (ignored by git; it is Canon's
firmware, so keep it to yourself), the tests also check the real develop
task and Canon's neutral colour table, and `tools/check_reloc.py` checks a
build's copy of the task instruction by instruction:

```bash
chdkptp -c -e"rmem 0xFF810000 0x7F0000 -f=rom/PRIMARY.BIN"
python3 tools/check_reloc.py rom/PRIMARY.BIN --emulate --elf build/firmware/chdk/platform/g12/sub/100g/main.elf
```

## Looks

`tools/build_lut_pack.py` rebuilds every look from the included sources, and
`tools/hw_table.py` fits each look's DIGIC colour table (`.HWT`, and `.HWA`
for Amber halation) to its `.LUT`. `tools/cube_pack.py input.cube
output.LUT --size 17` converts an sRGB `.cube`. `CHDK/FILM/LUTS.TXT` holds at
most 16 looks, and all 16 are taken: replace a line with `FILE.LUT|Menu name`
(an upper-case 8.3 file name, a menu name of up to 20 characters).
