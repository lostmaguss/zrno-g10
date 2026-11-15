# Features

Everything in the Zrno menu. The **S button** opens it; MENU from there
opens CHDK's own menu, and Back, S or a half-press of the shutter returns to
the camera.

## Processing

**Processing** (Off by default) switches Zrno on. While it is Off, every
shot is exactly what Canon makes. While it is On, a shot is processed when
the camera is in P, Tv, Av, M or Auto (any dial position, C1/C2 included),
single-shot drive, 4:3, size L, M1, M2 or S, and My Colors Off. Otherwise
the shooting screen says why it is paused, for example `paused: not 4:3`.
Scene, creative-filter and movie modes, and shots taken from a script, are
never processed.

A processed shot takes about 3 s with a look alone and up to about 15 s at
L with the slowest effects on; smaller sizes are faster. Keep the camera on
until "saved" appears. The photo is a normal `IMG_nnnn.JPG` in DCIM, and a
sensor backup is kept in `A/FGRAW` for Re-develop.

**Engine** picks how a shot is made: **Film** (looks and effects, below),
**ZrnoBoy** ([ZRNOBOY.md](ZRNOBOY.md)), **Print** ([PRINT.md](PRINT.md)) or
**Grade** ([GRADE.md](GRADE.md)).

## Film: looks

**LUT** picks one of sixteen looks. Warm 200 is an original recipe; the
other fifteen are adapted from the RawTherapee Film Simulation Collection
([THIRD_PARTY.md](../THIRD_PARTY.md)): Portra 400 and 800, Ektar 100,
Velvia 50, Provia 100F, Astia 100F, Superia 200, Fuji 400H, Kodachrome 64,
Polaroid 669, Ilford HP5, Tri-X 400, Acros 100, Bleach Bypass and Teal &
Orange. Canon's own develop applies the look, so it costs no time, and the
live view shows its colours.

**Look preview.** In shooting mode, SET on the LUT row (or the Look row)
shows the live view with a small card. Left/right or the back dial step
through the looks; SET or MENU returns to the menu. On the Look row a saved
look of another engine shows that engine's screen (ZrnoBoy's or Print's). The effects are added
after the shot, so they don't show live.

## Film: effects

Each is Off, Low, Mid or High unless noted. Times are per shot at L.

| Effect | What it does | Time |
| --- | --- | --- |
| Halation | The glow around bright lights on film without an anti-halation layer (CineStill): a core close to the light and a wide, faint tail. **Halation colour** picks Red or Amber (a sodium-lamp glow); either follows the look. | 3–11 s at High, by how much of the frame is bright |
| Bloom | A soft, neutral glow around bright areas. | |
| Soft focus | A diffusion filter (Pro-Mist): light mists into the shadows around it, and local contrast drops a little. | 4.6–5.4 s |
| Sharpening | Original, Soft, Off or Crisp: Canon's own edge enhancement. | none |
| Grain | Luminance grain, different on every shot. | |
| Vignette | Darker corners, like a simple lens (−22%, −40% or −58%). | 1–3 s with leaks |
| Light leaks | One or two warm glows from a random edge, different on every shot. | |
| Toy lens | A sharp centre, soft edges and red-cyan fringes, like a plastic lens. | about 13 s |
| Dust & scratches | Specks, hairs and faint scratches, different on every shot. | about 0.4 s |
| Date stamp | An orange LED-style date or text in a corner (Off or the corner). **Stamp shows** picks `'YY MM DD`, `DD MM 'YY` or Text; **Stamp text** takes up to 24 characters. | about 0.7 s |

## Saved looks

**Look** recalls a saved set of settings in one step, in every engine. A
look keeps its engine and that engine's settings:

| Engine | What a look keeps |
| --- | --- |
| Film | The LUT, every effect and the date stamp |
| ZrnoBoy | Palette, Frame, Resolution, Contrast, Brightness, Edges and Dither |
| Print | Style and every Print row (Palette or Inks, Separation, Screen, Noise, Register, Print flaws, Paper) |
| Grade | Which grade (Grade 1 … 4), all its values, and Sharpening |

Recalling a look switches to its engine, so Look 1 can be a Film look and
Look 2 a Print look; the other engines keep their settings. A Grade look
puts its values back into its grade (Grade 2, say), replacing what that
grade holds. The Look row (second, under Processing) and the four rows that
manage looks sit in every engine's menu: **Save as new look** (up to 16, across all engines),
**Update**, **Rename** (up to 20 characters) and **Delete** (SET twice). A
`*` after the name means a setting has changed since it was recalled; to
adjust a saved look, recall it, change it and choose **Update**. The
shooting-screen indicator shows the look's name in every engine.

**Custom** sits on the Look row between the last look and the first. It
keeps the settings as they are and leaves every look, so changes no longer
show as an edit of one (`*`), the indicator shows no look's name, and
Update, Rename and Delete have no look to act on. Recalling or saving a
look leaves Custom. After a restart the row shows a look's name again if
the settings equal it.

Processing, the Settings rows (the indicator, Busy LEDs, the S button and
Live preview) are camera settings, not part of a look.

Looks are stored in `CHDK/FILM/LOOKS.TXT`, one per line, and can be edited
on a computer:

```
Beach day|EKTAR.LUT|halation=High bloom=Mid grain=Low stamp=BottomRight shows=Text|Summer '26
Pocket|ZrnoBoy|palette=2 frame=1 resolution=0 contrast=7 brightness=3 edges=2 dither=1
Woodcut|Print|style=3 palette=1 inks=0 separation=0 screen=0 noise=2 register=1 flaws=1 paper=0
Matte|Grade|grade=2 sharpening=Soft contrast=-1 fade=2 bluehue=+3
```

The second field is the LUT file for a Film look, or the engine's name.
ZrnoBoy and Print settings are each row's place in its list, counting
from 0; Grade values are the numbers the menu shows. The file's header
lists every key and value. A setting left out takes its first value (Off,
or Original for sharpening; for Grade, the neutral grade's).

## Shooting screen

The shooting screen is Canon's own, with only the Zrno indicator over it.
CHDK's own display there (the card's free space and its bar, the battery,
the temperature and the clock) is off by default; each can be turned on
again in CHDK's menu, under OSD Parameters. CHDK keeps these choices in
`CHDK/OSD__4.CFG`: a card that already has one keeps the choices saved in it,
so turn the elements off there, or delete the file to start from Zrno's
defaults (it holds CHDK's other display settings too, such as its colours).

## Settings

**Settings**, at the bottom of the menu:

- **Indicator**: the card on the shooting screen at the Bottom, Top or Off.
  It shows the look's name and an icon for each active effect, with its
  level as pips.
- **Busy LEDs**: the indicator lights blink while a shot processes.
- **S button opens**: the Zrno menu, or CHDK's <ALT> mode as in stock CHDK.
- **Live preview**: Off keeps Canon's live view while ZrnoBoy or Print is
  the engine.
- **CHDK menu**: opens CHDK's main menu.

Changes apply and save on their own once the camera is idle.

## Re-develop photo

In the gallery, with a photo taken with Processing On on screen (its number
showing), press S and choose **Re-develop photo**. Zrno develops the shot
again from its sensor backup with the current settings and the shot's
original white balance, and saves it as a new JPEG; the original is kept.
The flash stays off for it, and your flash setting is put back afterwards.
It is experimental.

## Files on the card

All in `CHDK/FILM/` unless noted. Without a settings file, its settings are
the defaults.

| File | Holds |
| --- | --- |
| `FILMON.TXT` | Processing On (present) or Off |
| `LUTSEL*.CFG`, `FILMSEL*.CFG`, `FXSEL*.CFG`, `RETRO.CFG`, `STAMP.CFG`, `HALOAMB.TXT` | The Film engine's settings |
| `LOOKS.TXT` | Saved looks |
| `LOOKSRLD.TXT` | Left by the browser tool after it changes `LOOKS.TXT`: Zrno reads its looks again and removes it |
| `ZRNOBOY.CFG`, `PRINT.CFG`, `GRADE.CFG` | The other engines' settings, and the engine choice |
| `FILMOSD.TXT`, `LEDOFF.TXT`, `SCHDK.TXT` | Settings submenu choices |
| `LUTS.TXT`, `LUTS/` | The look catalogue: each look's `.HWT` and `.HWA` colour tables, and its `.LUT` source |
| `RAWGOLD.TXT`, `STAGE.TXT`, `LUTPATCH.TXT` | Switch the processing path on; keep them |
| `NATIVE.TXT`, `STAGELOG.TXT`, `REDEV.TXT` | Logs of backups, shots and re-develops |
| `A/FGRAW/` | Sensor backups (about 15.6 MB each), with their `.SET` settings |
