# How Zrno works

For anyone changing Zrno or porting it to another camera. Using it is in
[FEATURES.md](FEATURES.md); building it in [BUILDING.md](BUILDING.md).

Zrno is CHDK release-1_6 r6355 plus the C files in `camera/`, which
`tools/integrate.py` copies into a CHDK tree. All addresses below are for the
G12 on firmware **1.00G**.

## One processed shot

1. **Route** (`film_queue.c`).
   With Processing On, a shot is taken in P, Tv, Av, M or Auto, single shot,
   4:3, size L, M1, M2 or S and My Colors Off (ZrnoBoy takes any My Colors).
   Otherwise the indicator shows why it is paused. Scripted shots never take
   the route. Grade never does either: it is only the colour table (step 4).
2. **Sensor backup** (`film_raw.c`). At CHDK's RAW hook the shot is latched,
   and a task of its own (FilmBackup, priority 0x18) writes the raw buffer to
   `A/FGRAW/Rnnnnnnn.RAW` (15,634,944 bytes) and Canon's develop settings
   record, which holds the white balance, to a `.SET` beside it. Canon reuses
   the sensor buffer soon after the JPEG is saved, so the develop task waits
   for the backup before writing the file. `NATIVE.TXT` logs each backup and
   the JPEG made from it.
3. **The develop task copy** (`film_reloc.c`). CHDK's task-creation hook in
   `boot.c` replaces the entry of Canon's develop task (`0xff8e6f3c`) with a
   copy that Zrno builds in RAM from the camera's own ROM: the task
   (`0xff8e6f3c`–`0xff8e73cc`) and its encode-step loop
   (`0xff8e5ec4`–`0xff8e60d4`). Branches inside the copied code go to the
   copy, others go through veneers back to the ROM, and PC-relative loads go
   to the copy's own literal pool. Four hooks are added:

   | ROM address | Hook |
   | --- | --- |
   | `0xff8e6f6c` | `film_develop_trace(msg)` after each message (research builds only) |
   | `0xff8e7324` | `film_stage_observe(1)`, before the encode-step loop: the L frame is graded here |
   | `0xff8e6050` | `film_stage_observe(3)`, before each encode step: M1, M2 and S are graded here, on Canon's shrunk frame |
   | `0xff8e7328` | `film_stage_observe(2)`, after the loop: waits for the backup |

   A hash of every ROM word read must equal `FILM_RELOC_HASH`; on any other
   firmware the camera runs Canon's own task and Zrno shows "paused: unknown
   firmware". No Canon code is in the source or the binary.
4. **Colour** (`film_hwcolour.c`, `film_lut.c`). Canon applies the look in
   hardware: the look's `.HWT` (a 4,000-byte DIGIC colour table, fitted to
   its 3D LUT by `tools/hw_table.py`) replaces My Colors Off's table. Canon
   applies that table when it encodes the JPEG (step 5), not before the
   effects. Grade writes its own table the same way. Sharpening sets Canon's
   edge-enhancement override. The live view takes the table through Canon's
   colour reload, so it shows the look.
5. **Effects** (`film_stage.c`). At the hook, the frame is checked (job,
   operation, size property, buffer addresses, format), then the CPU adds the
   effects in place, in this order: toy lens, halation, vignette, light
   leaks, dust and date stamp (one pass over the rows), then soft focus,
   Bloom and grain. ZrnoBoy (`film_gboy.c`) and Print (`film_riso.c`)
   replace the whole frame instead. The frame still has the scene's
   colours, so the effects work on it before the look. Canon then applies
   the look's table and encodes the JPEG and its thumbnail as usual. Each shot leaves a line in
   `STAGELOG.TXT`.

**Re-develop** (`film_redev.c`, the `redev.flt` module) uses the same path:
it takes a real shot, swaps the sensor data for a saved backup before
Canon develops it, as CHDK's own RAW develop does, and restores the shot's
white balance from its `.SET`.

## Source map

| File | Role |
| --- | --- |
| `film_queue.c` | Which shots are processed, and why not |
| `film_raw.c` | Sensor backup, its task, Re-develop's loading and white balance |
| `film_reloc.c` | The develop task copy and its hooks |
| `film_stage.c` | The hook: frame checks, effect passes, progress, logs |
| `film_colour.c` | Halation and the colour pass; the CPU LUT (host reference only) |
| `film_retro.c`, `film_toy.c`, `film_print.c`, `film_bloom.c`, `film_grain.c` | The effects |
| `film_hwcolour.c` | Installing colour tables and the sharpening override |
| `film_lut.c` | Settings, looks, files, the menu's values |
| `film_menu.c`, `film_ui.c` | The Zrno menu, the indicator, the developing panel |
| `film_gboy.c`, `film_gbui.c` | ZrnoBoy, the engine switch, the ZrnoLive task |
| `film_riso.c`, `film_rsui.c` | Print |
| `film_grade.c`, `film_grui.c` | Grade |
| `film_redev.c` | Re-develop (a CHDK module) |

`tools/integrate.py` also patches CHDK: the Makefiles, the task hook in
`boot.c`, the menu (`tools/menu_patch.py` hands the Zrno menu to
`film_menu_draw` while CHDK keeps the navigation), the shooting screen
(`tools/osd_patch.py` turns CHDK's own OSD elements off by default and draws
the indicator in CHDK's OSD pass), and the core's place in memory. The core
runs from EXMEM (`OPT_CHDK_IN_EXMEM`, loaded at `0x079089e0`, about 3.9 MB
of heap); it outgrew CHDK's 200 KB ARAM block.

## G12 1.00G facts

| What | Where |
| --- | --- |
| Developed L frame | UYVY 4:2:2, signed U/V, 3648 × 2736, 7,296 bytes a row, at `0x4238a730` (uncached) |
| Shrunk frame (M1, M2, S) | `0x42154f30` |
| Colour presets | pointer table `0x5b4c`; entry 0 is My Colors Off, Canon's own at `0xffbcb7c0`; the loaded copy at `0xa9864` |
| Colour reload (live view) | `0xff855274` |
| Colour table format | 1,000 words, a 10 × 10 × 10 RGB grid (nodes 0–8 cover 0–255), tetrahedral; Y in bits 20–29, signed U and V in bits 10–19 and 0–9 |
| Sharpening override | `0x5b40`: −1 is Canon's own; FCreate (`0xff914710`) copies it into the develop parameters |
| Angel table | pointer at `0x674c`, 504 16-bit hardware entries; entry 183 is the edge-enhancement gain (64 at My Colors Off) |
| Register shadow | `0x400000 + (reg & 0x3FFFF)` holds the last value Canon wrote to each `0xC0F00000`–`0xC0F3FFFF` register |
| Properties | 49 shooting mode, 102 drive, 222 size (0 = L), 300 aspect (0 = 4:3), 188 My Colors (0 = Off) |
| CPU | ARM946E-S, ARMv5TE, 144 MHz: no FPU, no divide; DSP multiplies, `QADD`, `LDRD` |

## Constraints learned on the camera

- **Speed.** About a quarter of effect time is memory stalls. Kernels avoid
  divides and count instructions in an ARM emulator before they reach the
  camera (`tests/bench_*_arm.py`). SpyTask has an 8 KB stack and must stay
  responsive: heavy work runs in a low-priority task of its own.
- **Canon applies the colour table in its JPEG step**, after the hook has
  written the frame. The frame the effects see still has the scene's
  colours, also on a black-and-white look, and the table turns that colour
  into the look's colours (or grey) afterwards. So:
  - ZrnoBoy and Print install Canon's own table: a look would recolour their
    exact palette (a ZrnoBoy chart shot with Tri-X came out grey).
  - On a black-and-white look, an effect must keep a pixel's colour. If it
    sets the colour to zero, the table gives that pixel a different grey.
    Halation did this until it was fixed: the glow's edge showed as 16 px
    squares, 10 to 20 levels darker under warm light.
  - Colour added to a black-and-white look is turned into grey. That is why
    Toning (sepia, selenium, cyanotype) was removed.
  - Halation's tint and highlight weights (`tools/hw_table.py`) and the
    emulation in `tools/acceptance.py` still assume the table comes before
    the effects.
- **Tasks.** CHDK runs key handlers in the keyboard task and drawing in
  SpyTask. Code shared between them reads its state once: reading it twice
  crashed the camera (a jump to address 0).
- **Frame and screen geometry.** A UYVY pair, and the JPEG's 4:2:2 chroma,
  share one colour, so any edge Zrno draws starts on an even column. The
  overlay is 720 × 240 in byte pairs, and is mirrored when the screen faces
  the lens.
- **Change what Canon reads, not its registers.** Engine registers cannot be
  read back, and Canon reprograms them for every develop. Zrno changes the
  preset pointer and the override word instead.
- **BltDmac.** Its modes 1 and 2 never complete from RAM to RAM, wedge the
  block, and were followed by a Canon lens error (the lens MCU uses channel
  2). Don't use them.
- **Live view holds image engines**; playback mode frees them.

## Changing and testing

- `python3 tools/test_native.py`: the camera's C on the host, with
  AddressSanitizer and UBSan, the menu and the shooting screen in a
  simulator of CHDK's own code, the relocator and the installer.
- `python3 tools/menu_sim.py serve`: the Zrno menu in a browser.
- `tools/acceptance.py PHOTO.CR2`: renders a real G12 raw through the
  current kernels in the camera's order, with 100% crops of every corner
  and edge. Review it before any effect change goes to the camera: one
  build turned diagonal pipes into straight streaks at the frame edges.
- `tools/g12_usb.py` ([USB.md](USB.md)): logs, screenshots, Lua and test
  boots over USB, without moving the card.
- `tools/check_reloc.py`, with your own ROM dump: checks the develop task
  copy instruction by instruction (BUILDING.md).

## Porting to another camera

Start from a CHDK port of that camera and firmware. Then find, from a ROM
dump and on the camera: the develop task and the loop around the JPEG encode
(`film_reloc.c`'s ranges, hooks and hash), the developed frame's address,
size and format (`film_stage.c`), the colour preset table and its reload
(`film_hwcolour.c`), and the property values (`film_queue.c`). Prove each
step on the camera before writing pixels: observe first, then grade a small
rectangle, then the whole frame.

## Further reading

The probes in `tools/probes/` are the experiments behind the develop task
copy and Re-develop. Each one says in its header what it tested and what it
found.

Most of the public DIGIC 4 knowledge comes from Magic Lantern's work on the
DSLRs: [magiclantern_simplified](https://github.com/reticulatedpines/magiclantern_simplified)
(`src/edmac.c`, `src/lv-img-engio.c`) and
[qemu-eos](https://github.com/reticulatedpines/qemu-eos).
