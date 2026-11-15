# ZrnoBoy — the pocket-console camera engine

ZrnoBoy is the Zrno menu's second engine. It does what the 1998 pocket-console
camera cartridge did: a coarse grid of 4 shades with ordered dithering, edge
outlines from the sensor, a 4-colour palette and a frame. It shows live on
the shooting screen. All frame art, palettes beyond the console's own and
names are original. No console art, logo or ROM data is copied: the dither
anchors are numbers published by people who reverse-engineered the cartridge.

## Using it

Zrno menu → **Engine** → ZrnoBoy. The menu then shows the engine's rows:

| Row | Values | What it does |
| --- | --- | --- |
| Palette | Pea green, Pocket, Printer, Backlight, Zrno, Sunset, Ocean, Candy, Red visor, Sepia | The four colours, lightest to darkest. |
| Frame | Off, Classic, Film strip, Cicmany, Viewfinder, Stamp, Tatras, Wheat, Window, TV set, Light leak | Each goes round a 128 × 96 picture. Off fills the photo with a 160 × 120 picture. |
| Resolution | 128x96 (default), 256x192 | 256x192 has twice the detail each way, in a 320 × 240 canvas; the frames keep their pixel art, drawn twice the size. |
| Contrast | 1–16 (8) | The cartridge's 16 contrast steps. 16 is pure black and white. |
| Brightness | −3 to +3 (0) | Shifts the picture against the dither thresholds. |
| Edges | Off, Soft, Camera, Strong | The sensor's edge enhancement at 0, 25, 50 (the cartridge's) and 100%. |
| Dither | On, Off | Off writes the same three thresholds to every cell, as the cartridge's own "flat" option did. |

With Processing On and the shooting screen showing, the live view becomes the
ZrnoBoy screen, at up to 10 frames a second (about 7 at 256x192). In shooting mode, SET on any
ZrnoBoy row except Engine opens the preview: the live screen with a card to
step that row. Left/right or the back dial step it; SET or MENU returns.
**Live preview** (Settings) Off keeps Canon's own live view, with the
indicator card, while ZrnoBoy or Print is the engine. The preview of a
row still shows the engine's screen.

A ZrnoBoy shot needs what a film shot needs (a photo mode, single shot,
4:3, size L, M1, M2 or S) but takes any My Colors setting, because it uses
only the brightness. While it is the engine, Canon's own colour table stays
in place, so the palette comes out exactly as written. The settings are
stored in `CHDK/FILM/ZRNOBOY.CFG`, and a saved look made in ZrnoBoy keeps
them ([FEATURES.md](FEATURES.md#saved-looks)).

**Frames.** Each frame is drawn in code (`camera/film_gboy.c`):

| Frame | What it shows |
| --- | --- |
| Classic | A light mat with a dark keyline and the Zrno bean. |
| Film strip | Sprocket holes and a frame mark in the rebate. |
| Cicmany | White geometric motifs on dark logs, after the painted houses of Čičmany. |
| Viewfinder | A rangefinder's bright-line corners round the picture, with focus scales and the real exposure: shutter speed, aperture and ISO as the camera names them (1/250, 2"5, F4.0, ISO200, AUTO), and a five-mark meter with the exposure compensation lit. The live view shows the camera's current values (they settle on half-press); the photo shows its own. |
| Stamp | A perforated postage stamp, a postmark tucked under its corner. |
| Tatras | A dithered sky with a sun, and two ridges rising behind the picture. |
| Wheat | Ears of wheat up the sides ("zrno" is a grain). |
| Window | A 90s desktop window: title bar, tools, scroll bar and status bar. |
| TV set | A wooden set with knobs and a grille. |
| Light leak | A black rebate fogged from one or two edges up to the picture, different on every shot. |

## How it reproduces the console camera

- **Picture.** The cartridge's 128-pixel width, made 4:3 (128 × 96) so it
  shows the G12's whole view, in a 160 × 120 canvas with the frame round it.
- **Tones.** Each pixel is compared with three thresholds from its cell in
  the cartridge's 4 × 4 matrix, which sets it to one of four shades. The
  thresholds come from the cartridge's 16 contrast steps.
- **Signal and edges.** The picture is stretched onto the sensor's range as
  the cartridge's auto-exposure did, and edges are enhanced as its sensor
  did in normal light, which gives the 1-pixel outlines.
- **Live view.** The camera's live view is sampled to the picture and drawn
  over the whole screen, so it shows the frame as the photo will, also with
  the screen turned to face the lens.

Sources:
[AntonioND, gbcam-rev-engineer](https://github.com/AntonioND/gbcam-rev-engineer),
[Pan Docs](https://gbdev.io/pandocs/Gameboy_Camera.html),
[Raphaël Boichot's cartridge disassembly](https://github.com/Raphael-Boichot/Game-Boy-Camera-disassembly),
[dither-pattern-gen](https://github.com/HerrZatacke/dither-pattern-gen) (the anchor tables),
[Mitsubishi M64282FP dashcam](https://github.com/Raphael-Boichot/Mitsubishi-M64282FP-dashcam) (sensor range),
[Camera Faker](https://github.com/Raphael-Boichot/Game-Boy-Camera-Faker) (processing order),
[gb-palettes](https://github.com/HerrZatacke/gb-palettes),
[gb-camera-frames](https://github.com/cristofercruz/gb-camera-frames) (frame geometry).

`tools/gboy_render.py PHOTO --out DIR [--sheet] [--live]` renders a photo or
CR2 through the camera's own code on a computer.
