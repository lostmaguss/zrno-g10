# Magus: Zrno for the Canon PowerShot G10 (firmware 1.03B)

**Magus** is the G10 port of [Zrno](https://github.com/suzvuk/zrno) by
suzvuk (GPL-2.0-or-later): Zrno's film looks, halation, Bloom and grain on
the G10, with a **Magus** entry in CHDK's menu. Its files on the card are in
`CHDK/MAGUS/` (looks, halation tints, `MAGUS.CFG` settings); the build switch
is `MAGUS_G10`, the build `tools/integrate_g10.py TREE --magus`.

These notes record how the port was found and tested. Addresses come from a
ROM dump of a G10 1.03B (`rom/PRIMARY.BIN`, not in git) and are compared with
the G12 1.00G facts in [ARCHITECTURE.md](ARCHITECTURE.md).

## Camera

| What | G10 1.03B | G12 1.00G |
| --- | --- | --- |
| CHDK port | `platform/g10/sub/103b`, r6355, KEYSYS d4 | `platform/g12/sub/100g` |
| Property set | 2 | 4 |
| Sensor (raw) | 4480 × 3348 | 3744 × 2784 |
| JPEG L | 4416 × 3312 | 3648 × 2736 |
| ALT button | Print | S (zrno remaps) |
| CHDK in EXMEM | untested by CHDK | used by zrno |
| Boot for test builds | locked card (`DISKBOOT.BIN`); `PS.FI2` needs FI2 keys we don't have | same |

Properties (propset 2): shooting mode 49, drive 102, size 218, aspect 294,
My Colors 184.

## Develop task

- `DevelopModule` is created at `0xff8b21a4`: entry **`0xff8b1c8c`**, priority
  0x19, stack 0x1000 (G12: `0xff8e6f3c`).
- The loop receives a message, copies it into three blocks and dispatches on
  its type (0–6) through a table jump at `0xff8b1d18`:
  - `msg+0x04`, 0x1c bytes → `0x21070`
  - `msg+0x20`, 0x70 bytes → `0x2108c` (the job)
  - `msg+0x90`, 0x18 bytes → `0x210fc`
- Cases: 0 → `0xff8b126c`, 1 → `0xff8b220c`, 2 and 6 → `0xff8b0fe8`,
  3 → inline (`0xff8b1d44`), 4 → `0xff8b153c`, 5 → nothing. After each case
  `0xff8b1a2c` runs, then the loop repeats. The task ends at `0xff8b1f10`.
- Unlike the G12, the steps are not inline in the task. Three helpers
  (`0xff8b0fe8`…, `0xff8b126c`, `0xff8b153c`) each loop over steps by calling
  `0xffa012e0` (DevelopPath). `0xff8b153c`'s loop (`0xff8b160c`–`0xff8b1644`)
  is the closest match to the G12's encode-step loop.
- The develop request is `0xff8b1654(job, desc, result, flag)`, wrapped by
  `0xff8b1f44` (flag 0) and `0xff8b1f58` (flag 1, used by DevelopPath at
  `0xff9fd868`).
- The ROM's own develop test (`0xff851644`) shows the job layout: **op at
  +0x20** (1 = RAW → YC, 3 = NR & red eye, 2 = YC → JPEG), **image size at
  +0x28** (property 218). These match the G12 (op at `0x82350`, size at
  `0x82358`, property 222), so the G12 job copy most likely starts at
  `0x82330`, and the G10's at `0x2108c`: op `0x210ac`, size `0x210b4`.
  To be confirmed on the camera.

## Colour table

- The G10 hardware table is **729 words** (9 × 9 × 9), not 1,000. The packing
  looks the same: Y in bits 20–29, U and V in bits 10–19 and 0–9. Custom Color
  (`PFMyColor.c`, `0xff8d2940`) builds it from 729 base entries plus slider
  deltas (properties 0xaf–0xb2) into **`0x40428`** (0xb64 bytes).
- The base tables are in RAM, behind the pointer at `0x75c8` (= `0x1cd160` on
  the probed camera; also at `0x6e08`, in a list of (address, size) blocks
  that looks like loaded adjustment data). No neutral table is in the ROM.
- **Scale: BT.601 × 2.5** (G12: × 2). Grey node *k* is Y = 80·*k*, U = V = 0.
  Table 0 (My Colors Off) is the BT.601 identity on that scale, nodes at 32·*k*
  (node 8 = 256), within 3 steps.
- **Preset selector `0xff8d2c38(mode, &id)`**: picks the table for My Colors
  mode 0–13, copies 0xb64 bytes to **`0x40428`** and returns `0x40428`.
  Mode 0 (Off) and unknown modes copy table 0, i.e. `*(0x75c8) + 0`. Mode 11
  (Custom) builds it (`0xff8d2940`). Other offsets from `*(0x75c8)`:
  1 → 0x9abc, 2 → 0xa620, 3 → 0xbce8, 4 → 0xb184, 5 → 0x4458, 6 → 0x2d90,
  7 → 0x38f4, 8 → 0x222c, 9 → 0x16c8, 10 → 0xb64.
  Probe 1 (My Colors Off, playback): `0x40428` equals table 0 byte for byte.
- **Loading into the hardware**:
  - `0xff8fa698(table)`: copies the table to `0x4278c` (saved copy) and to the
    DMA buffer `*(0x8284 + 8)` (`0x405c3220`), no load.
  - `0xff8fa6cc(table)`: copies to the DMA buffer and loads it into the colour
    block (`0xff908a50`, register `0xc0f22020`), waits up to 1 s.
  - `0xff8fa730()`: loads the saved copy.
  - `0xff83da04(mode)` (semaphore-guarded): selector → `0xff8fa698` →
    `0xff8fa730`. This is the G10's equivalent of the G12 colour reload.
    Called from `0xff8f81bc`.
  - `0xff83d964()`: selector(0) → `0xff8fa6cc`. Called from `0xff8528e8` and
    `0xffa670fc` (develop side, to confirm).
- **Plan for looks**: as on the G12, change what Canon reads, not its
  registers: overwrite table 0 at `*(0x75c8)` with the look (keeping a copy of
  Canon's to restore), then call `0xff83da04(0)` so the live view reloads it.
  Every later selector call copies the look.
- **Proven on the camera (2026-11-16)**: `COLORTEST.LUA` (folder above the
  repo) set U and V of table 0 to zero and called `0xff83da04(0)` from Lua.
  The live view and the next photo came out black and white; mode 1
  restored the table.
- **Film looks work (2026-11-16)**: `tools/g10_table.py` converts the 16
  `.HWT` looks to 729-word `.G10` tables (`card-g10/`); the converted
  identity is within 4 steps of Canon's table 0. `FILMLOOK.LUA` (folder above
  the repo) saves Canon's table to `CHDK/MAGUS/CANON.BIN`, pokes the
  chosen look into table 0 and reloads. Photos IMG_1018–1027 came out in
  clearly different looks.
- zrno's `.HWT` files hold 10 × 10 × 10 with the last layer repeated on the
  × 2 scale, so the G10 version is the first 9 of each axis, × 1.25.

## Developed frame

- **Found on the camera (probe 2, `CAPTPROBE.LUA`)**: the developed L frame
  is at **`0x42902100`** (uncached; cached `0x02902100`), UYVY 4:2:2 with
  signed U/V, 4416 × 3312, **8,832 bytes a row** (0x1BE8000 bytes, ends at
  `0x444EA100`). A coarse sample of it matches the saved JPEG (IMG_1030):
  luma correlation 0.9998, slope 0.998; U/V vs Cb/Cr 0.99.
  G12: `0x4238a730`, 7,296 bytes a row.
- The address appears in the develop state at `0x6ee8`/`0x6eec` and in the
  job blocks at `0x210d8`. After the shot `0x210ac` held 0x2d0, not an op
  code, so the job's offset inside the message copy is still to be checked
  with a hook.
- Other buffers seen: `0x42a37800` and `0x42b59800` (inside the same frame),
  `0x41660080` (an older image), `0x455c8600`, `0x4474d800` (not image data).

## Observe build (camera test 2026-11-16)

`tools/integrate_g10.py` + `camera/g10_observe.c`: the relocated develop task
(hash `0x1df5e28d`, checked by `tests/g10_reloc_host.c` and
`tools/g10_reloc_check.py`: 230 instructions, 0 problems) ran on the camera
(`reloc_status 1`); three photos saved normally (IMG_1031–1033). Boot:
locked card, two partitions (FAT16 boot + FAT32 data).

Each L shot is three messages to the develop task:

| Message | Work | Time | Job words after it |
| --- | --- | --- | --- |
| type 1 | RAW → YC | ~180 ms | `0x210b4` = raw buffer (`0x43164d80`), `0x210d8` = frame `0x42902100` |
| type 2 | YC → JPEG | ~110 ms | `0x210b4` = frame `0x42902100`, `0x210c0` = JPEG output |
| type 4 | small step loop (`0xff8b153c`, one step) | ~10 ms | `0x210b4` = `0x405c4c00` |

The frame samples are final at phase 2 after type 1 and don't change after
that. So **phase 2 after a type 1 message is the effects hook**: the frame is
developed and the JPEG encode hasn't started (G12: phase 1).

## First pixels (camera test 2026-11-16)

The `--square` build inverts the luma of a 256 × 256 square at phase 2 after
type 1. IMG_1034, 1035 and 1037 show it at exactly pixels/rows 200–455, in
the JPEG. IMG_1036 has none: that shot used the **other raw buffer
`0x413245a0`, developed into a second L frame at `0x41516720`**, which the
check (`0x42902100` only) refused, as it should. The two pairs are:

| Raw buffer (`0x210b4`) | L frame (`0x210d8`) |
| --- | --- |
| `0x43164d80` | `0x42902100` |
| `0x413245a0` | `0x41516720` |

`g10_observe.c` now accepts both pairs.

## Grain (camera test 2026-11-16)

`integrate_g10.py --grain`: zrno's `film_uyvy_grain` unchanged (only its
width cap is 4416 under `MAGUS_G10`), level High, on either L frame through
the cached alias with the D-cache cleaned and invalidated before and after
(as the G12's FRAMECACHE), 10 ms sleeps every 200 ms.
`tests/test_grain_g10.c`: luma only, guards intact, strength equal to the
G12 width (sd 8.28 on grey at High).

On the camera: IMG_1038–1040, all three shots grained (rc 0). Fine noise on
flat areas 2.7 → 7.3 (high-pass sd, median over flat tiles). **5.7 s per
shot**: slower than it should be; to be profiled.

## Zrno build: looks, halation, Bloom, grain, menu

`tools/integrate_g10.py TREE --magus` builds `camera/g10_magus.c` with zrno's
kernels (`film_colour/retro/toy/print/bloom/grain.c`, ARM at -O2), their
width cap raised to 4416 under `MAGUS_G10` (`camera/film_width.h`).

- **Menu**: CHDK menu → **Zrno**: Processing, Film look (Canon + 16),
  Halation (Off–High), Halation colour (Red/Amber), Bloom, Grain, and
  "Last shot" (time, or why a shot was skipped). Saved to
  `CHDK/MAGUS/MAGUS.CFG`.
- **Looks** are installed from C (SpyTask, shooting mode, My Colors Off):
  Canon's table 0 is kept in RAM, the look's `.G10` copied over it,
  `0xff83da04(0)` reloads. Halation takes the look's tint and highlight
  weights from its `.HWT` trailer and Amber tint from `.HWA`
  (`tools/g10_table.py` copies both beside the `.G10`).
- **Stage** (phase 2 after message type 1, either L frame): effects only if
  Canon's copy at `0x40428` is the installed look (else "My Colors not Off");
  halation (`film_uyvy_graded_retro`, mono-aware), then Bloom and grain in
  one fused row pass when both are on (as the G12's FUSEFX), through the
  cached alias with the D-cache cleaned and invalidated before and after.
- **Memory**: workspaces 269 KB (halation) + 74 KB (Bloom) + one row, and
  the optional 64 KB fade table, from Canon's heap (CHDK isn't in EXMEM on
  the G10); refused unless 512 KB stays free ("no memory (N KB)").
- **Grain speed**: one 32-bit load and store per pixel pair instead of four
  byte accesses, same bytes (checked against the original); instruction
  count unchanged at ~33 per pixel in the ARM emulator. A branch-free
  rounding was tried and was slower (36), so it was dropped.
- **Tests**: `tests/test_g10_effects.c` (full G10 frame: every level and
  tint returns 0, guard bytes intact, fused = Bloom then grain, glow near
  highlights only, mono looks keep U/V), `tests/test_grain_g10.c`.
- Phase-6 log records hold, in `w[0..10]`: cache-clean ms, halation ms,
  Bloom+grain ms, sleep ms, sleeps, free memory at start, settings,
  frame, workspace addresses.

## Speed (camera test 2026-11-16, IMG_1041)

All High, Kodachrome, a bright window in frame: **18.3 s** per photo:
halation 9.1 s, Bloom + grain 9.2 s, sleeps 0.6 s, cache work 0, 1,054 KB
free. The kernels are compute-bound (ARM emulator on the same photo;
11.8 ns per instruction, calibrated on the camera's grain run):

| Effect | Low | Mid | High |
| --- | --- | --- | --- |
| Halation | ~8.4 s | ~10.1 s | ~10.7 s (camera: 9.1 s) |
| Bloom | ~3.1 s | ~3.9 s | ~4.9 s |
| Grain (after the change below) | ~4.5 s | ~4.5 s | ~4.5 s |

- Halation's cost follows how much of the frame is near a highlight, not
  its level: 14 instructions/pixel far from highlights, ~122 near them
  (`halo_push`, the half-resolution mask with three box stages per axis,
  57; `halo_glow_pair`, the colour blend, 44). Making it faster would mean
  a coarser mask, a visible change: not done without photo comparisons.
- **Grain on the G10** now uses a Q16 weight, a multiply and shift instead
  of the rounding division, and a clamp table: 33 → 27 instructions/pixel.
  Against the original at G12 width: at most one level apart on 0.07–0.14 %
  of pixels, same strength and mean. The G12 build is byte-identical.
- The scratch row's size does not change halation's cost in this build
  (checked at 8.8 KB and 24 KB).

## Coarse halation (G10)

`film_halo_coarse(1)` (`camera/film_colour.c`, on in `g10_magus.c`): the core
glow mask in 4x4-pixel cells instead of 2x2, each cell the highlight mean of
its two middle rows' four pixels; the box halved (windows 13/19/25 → 7/9/13
cells, so the three-box blur's width stays within about ±7 %); per image row
a quarter step between mask rows (`halo_lerp4`), and per row the cells
expanded to one sample per pixel pair, 3/4 of the cell and 1/4 of its
neighbour (`halo_expand`). The tail and the colour blend run at pair
resolution as before. Off (the G12): byte-identical to the original at every
level (checked against the committed file on the real photo).

On IMG_1041 (bright window), coarse vs original: luma of glowing pixels
differs by 0.2 on average, 3 at the 99.9th percentile, 7 at most; U/V at
most 5. Side by side, no visible difference. ARM emulator:

| Halation | Original | Coarse |
| --- | --- | --- |
| Low | 48.9 instr/px, ~8.4 s | 36.0, ~6.2 s (74 %) |
| Mid | 58.9, ~10.1 s | 38.6, ~6.6 s (65 %) |
| High | 62.3, ~10.7 s | 42.4, ~7.3 s (68 %) |

What remains is drawing the glow: the row loop with tail blend (15.4), the
colour blend per glowing pixel (14.9) and the mask (11.8 instr/px).

Camera (2026-11-16, IMG_1046/1047, all High, Kodachrome): **19.9 and
20.8 s**: halation 9.6–10.5 s, Bloom + grain 10.3 s, sleeps 0.6 s. The scene
is about 8x brighter than IMG_1041 (13.4 % of pixels above 200 against
1.7 %). On IMG_1046 the emulator gives the original mask ~15.6 s and Bloom
~6.8 s (scaled by the first test's measured/predicted ratio), so the old
code would have taken about 28 s there. IMG_1045 (halation High, Bloom and
grain Low, Teal & Orange): 8.4 s.

## More speed work (2026-11-16)

Profile on IMG_1046 (bright), instructions per pixel: coarse halation 74.6
(`halo_glow_pair` 34.6, row loop 23.4, `halo_push` 16.2), Bloom High 39.4
(`diffuse_rows` 21.8, `filtered_row` 15.6), grain 26.1 (row 21.5, noise 4.6).

- **Fast Bloom on** (`film_bloom_fast(1)`, zrno's FASTBLOOM): 24.6/33.1/39.4
  → 21.1/29.1/34.6 at Low/Mid/High (−12 to −14 %); on IMG_1046 at most 2
  luma levels from Bloom, 0.04 on average.
- **Tried and dropped**: per-pass tables of the halation glow amount per
  blur difference (no division or multiplies per glowing pixel). Same bytes,
  but 74.6 → 79.9 instructions/pixel: the arithmetic was already cheap, the
  extra table addresses cost more.
- Stack: the develop task's stack is 4 KB (`0xff8b21a4`). Deepest path:
  halation, ~2.2 KB (`map_halo_cached` 1,392 bytes). The fused pass's grain
  state (540 bytes) is now static instead of on that stack.
- The three kernels are near what their algorithms need on this CPU:
  grain ~43 instructions per pixel pair, halation's blend works in linear
  RGB per glowing pixel. Bigger cuts would change the look.

## Next

1. Done: `RAMPROBE.LUA` (folder above the repo) copied low RAM and the
   colour heap (`rom/probe1/`, not in git). In `RAMLOW.BIN`, file offset =
   address − 4.
2. Done: tables, selector and hardware load (above).
3. Find the developed frame (4416 × 3312 UYVY, 8,832 bytes a row) and the
   shrunk frame used for M1–M3 and S.
4. First camera build: observe only. Hook `0xff8b153c`'s step loop and log
   the job (`0x2108c`…) for each step to a file. No pixels changed.
