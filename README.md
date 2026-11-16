# Magus (Zrno for the Canon PowerShot G10)

Film looks, halation, Bloom and grain for the **Canon PowerShot G10**,
processed in the camera when you take the picture. Magus is a port of
[Zrno](https://github.com/suzvuk/zrno) by suzvuk, which was made for the
G12, to the G10 with firmware **1.03B**. It runs on
[CHDK](https://chdk.fandom.com/) and saves normal `IMG_nnnn.JPG` files.

> [!WARNING]
> This is unofficial firmware, not made, supported or endorsed by Canon or
> by Zrno's author. It changes how the camera develops photos. It can crash
> the camera or lose photos, and in the worst case damage it. Use it at your
> own risk; there is no warranty (see [COPYING](COPYING)). Only use it on a
> **G10 with firmware 1.03B**.

## What it does

Open CHDK's menu (press the **Print** button for ALT mode, then **MENU**)
and choose **Magus**:

| Setting | What it does |
| --- | --- |
| Processing | Turns Magus on or off. Off: normal Canon photos. |
| Film look | Canon, or one of 16 film looks: Warm 200, Portra 400, Velvia 50, Kodachrome 64, Ilford HP5, Ektar 100, Portra 800, Fuji 400H, Provia 100F, Astia 100F, Superia 200, Polaroid 669, Tri-X 400, Acros 100, Bleach Bypass, Teal & Orange. The live view shows the look. |
| Halation | A red glow around bright lights, as on cinema film: Off, Low, Mid, High. |
| Halation colour | Red or Amber. |
| Bloom | A soft glow around bright areas: Off, Low, Mid, High. |
| Grain | Film grain, different on every shot: Off, Low, Mid, High. |
| Last shot | How long the last photo took, or why it was skipped. |

Settings are kept on the card (`CHDK/MAGUS/MAGUS.CFG`).

The film looks run in the camera's own colour hardware (the table behind
Canon's My Colors), so they cost no time. Halation, Bloom and grain are
added by the camera's processor to the developed photo before Canon saves
the JPEG, so a photo with effects takes longer to save:

| On a bright scene (window, sky) | Time per photo |
| --- | --- |
| Look only | as fast as a normal photo |
| Grain | about 4.5 s |
| Bloom Mid + grain Mid | about 8–10 s |
| Halation, Bloom and grain all High | about 19 s |

Darker scenes and lower levels are much faster: halation and Bloom cost
time only near bright areas. **Last shot** shows the real time.

### When it works

- Shooting mode P, Tv, Av or M, **image size L**, **My Colors Off**.
  The look also needs My Colors Off; with any other My Colors setting the
  camera takes its normal photo and Last shot says why.
- Effects are added to L photos only.

### Not ported yet

From Zrno: soft focus, sharpening, vignette, light leaks, toy lens, dust
and scratches, date stamp, the ZrnoBoy, Print and Grade engines, saved
looks, re-develop, the shooting-screen indicator, and effects on M1–M3/S
sizes.

## Getting it onto a camera

Download `magus-VERSION-g10-103b.zip` from
[Releases](https://github.com/lostmaguss/zrno-g10/releases) and follow its
`INSTALL.TXT`, or build it from source (below). The G10 starts CHDK from a
card only from a small **FAT16** partition marked bootable, so the card
needs two partitions. This erases the card: back it up first.

1. Check the firmware: put an empty file `ver.req` on a card, switch the
   camera on in playback and press **FUNC/SET** + **DISP**. It must say
   `GM1.03B`.
2. Partition the card (Windows: `diskpart`, or a tool like CHDK's
   *STICK*/*SDMinste*):
   1. `ZRNO_BOOT`: 32 MB, FAT16, first partition;
   2. `ZRNO`: the rest, FAT32.
3. Write `BOOTDISK` at byte 0x40 of the FAT16 partition's boot sector
   (what CHDK's tools call "make card bootable"). On Windows,
   `tools/mark_boot_g10.py` (also in the release zip) does it, as
   administrator.
4. Copy the built `DISKBOOT.BIN` to **both** partitions, and to the FAT32
   one CHDK's files plus `card-g10/CHDK/MAGUS/` as `CHDK/MAGUS/`.
5. **Lock** the card, put it in the camera and switch it on. CHDK starts
   (the blue Print light flashes). Unlocked, the camera starts as a normal
   Canon.

The release's `INSTALL.TXT` gives the exact Windows commands for steps 2–4.
Nothing is installed in the camera itself. Firmware Update (`PS.FI2`)
cannot start Magus: it only starts the official CHDK.

## Building

```bash
ZRNO_EXTRA_PORTS=g10 python3 tools/fetch_chdk.py
cp -r build/chdk-r6355 build/g10-magus
python3 tools/integrate_g10.py build/g10-magus --magus
cd build/g10-magus
make PLATFORM=g10 PLATFORMSUB=103b CROSS_COMPILE=arm-none-eabi- DEF_SVN_REF=6355 fir
```

The firmware is `bin/DISKBOOT.BIN`, the matching modules `CHDK/MODULES/`.
`python3 tools/package_g10.py VERSION` builds a release (card zip, GPL
source zip, checksums) from a committed tree.
Tested with xPack GNU Arm Embedded GCC 10.3.1-2.3, on Windows with MSYS2's
`make` (see [docs/BUILDING.md](docs/BUILDING.md) for the toolchain).
`python3 tools/g10_table.py` rebuilds the G10 looks.

Host tests (C compiler, `-DMAGUS_G10`): `tests/test_g10_effects.c`,
`tests/test_grain_g10.c`. With your own ROM dump in `rom/PRIMARY.BIN`
(Canon's firmware: keep it to yourself), `tests/g10_reloc_host.c` and
`tools/g10_reloc_check.py` check the develop-task copy instruction by
instruction.

## How it works

The port hooks into the same places as Zrno, with the G10's addresses: a
copy of Canon's develop task with hooks around its steps, the developed
frame (4416 × 3312, two buffers), and the 729-word DIGIC colour table. The
effects are Zrno's code, with the frame width raised to 4416 and three
G10 speed-ups (a coarser halation mask, Zrno's fast Bloom, a fixed-point
grain step); on the G12 the code stays as it was. Everything that was
found and measured, with every address, is in
[docs/PORT-G10.md](docs/PORT-G10.md).

## Credits and licence

- **[Zrno](https://github.com/suzvuk/zrno)** by suzvuk: the film looks,
  effects and everything they are built on. Its original README is in
  [docs/ZRNO-README.md](docs/ZRNO-README.md), and its documentation in
  [docs/](docs/).
- **Magus**, the G10 port: lostmaguss, developed with Claude Code
  (Anthropic) as an assistant.
- Built on [CHDK](https://chdk.fandom.com/) (GPL).

Magus, like Zrno, is free software under the
[GNU GPL, version 2 or later](COPYING). The RawTherapee-derived looks are
CC BY-SA 4.0; see [THIRD_PARTY.md](THIRD_PARTY.md). Film and product names
identify the stocks the looks approximate; Magus is not affiliated with or
endorsed by their owners, by Canon, or by Zrno's author.
