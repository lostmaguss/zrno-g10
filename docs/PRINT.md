# Print — the stencil-print engine

Print is the Zrno menu's third engine. It prints the photo the way a
stencil duplicator (a risograph) prints one. The photo is split into two or
three spot inks, and each ink is screened with a random grain or with
halftone dots. The inks overprint like multiply on off-white paper. Each
drum sits out of register and slightly turned, and the print picks up the
small flaws real prints have. Every pixel comes out as one of 4 (or 8)
exact colours. Ink colours are the published approximations of real spot
inks; the engine's name, art and code are original.

## Using it

Zrno menu → **Engine** → Print. The menu then shows the engine's rows:

| Row | Values | What it does |
| --- | --- | --- |
| Style | Riso, Screenprint, Engraving, Woodcut, Photocopy, Comic, Text mode | The print process. Riso shows the rows below; the other styles show Palette, Noise, Register and Print flaws (text mode only Palette). |
| Inks | Pink + Blue, Teal + Orange, Blue + Orange, Blue + Yellow, Green + Pink, Black + Pink, Yellow+Pink+Blue | The ink set. The first ink of each is the darkest. |
| Separation | Colour, Duotone | Colour: each spot gets the mix of the inks that prints nearest its colour. Duotone: brightness only, the dark ink in the shadows, the light ink from the midtones up. |
| Screen | Grain, Dots | Grain: a random dither like the duplicator's default. Dots: halftone, the dark ink at 45° and 11.3 px, the others at 0° and 8 px. |
| Noise | 0 Clean … 4 Heavy (2) | Grain added to each ink's tone (0, 15, 30, 50, 75), fading out toward paper and solid ink, so highlights stay clean and the tones stay put. |
| Register | Off, 1 mm, 1.5 mm, 2 mm (1.5 mm) | How far the other drums sit from the first, in a new direction every shot (at L, 12.3 px per mm of an A4 print). |
| Print flaws | Off, Subtle, Strong, Heavy (Heavy) | Drum skew, lost dots, white specks and soft streaks along the feed (below). |
| Paper | Off-white, White, Cream | The paper colour under the inks. |

**Styles.** Each brings its own inks, screen and paper, and remembers its own
Noise, Register and Print flaws (defaults below):

| Style | Palettes | What it prints | Defaults (noise, register, flaws) |
| --- | --- | --- | --- |
| Screenprint | Pop, Sunset, Mint + Crimson | Four flat inks by brightness band, hard edges; the dark key screen out of register. | 0, 1.5 mm, Off |
| Engraving | Aubergine, Steel blue, Two plates | One ink in lines at about 61° that swell with the tone, cross-hatched in the shadows; Two plates adds a flat peach tint plate in the midtones. | 0, Off, Off |
| Woodcut | Black, Two blocks | Wavy gouges carry the midtones, solids with pale wood grain; Two blocks adds a red block under the black. | 1, 1.5 mm, Subtle |
| Photocopy | White paper, Pink paper | Hard toner black and white; the noise roughens edges, the flaws give streaks and specks. | 3, Off, Strong |
| Comic | Full key, Outlines only | Cyan, magenta and yellow in four flat Ben-Day tints and black keylines from edges; Full key also inks the darkest tones black, Outlines only lifts the tones. | 0, 1 mm, Off |
| Text mode | Green, Amber, Colour | Each 16 × 32 px cell (8 × 16 below 2400 wide) becomes the character of CHDK's 8 × 16 font whose ink matches its brightness, with scanlines; Colour picks each cell's colour from seven terminal colours. | none apply |

With Processing On and the shooting screen showing, the live view becomes a
320 × 240 sketch of the print in the ink colours: the same inks, separation
and register, with a coarser screen and no flaws. In shooting mode, SET on
any Print row except Engine opens the preview: the live screen with a
card to step that row. Left/right or the back dial step it; SET or MENU
returns. **Live preview** (Settings) Off keeps Canon's own live view, with
the indicator card; the preview of a row still shows the print.

A Print shot needs what a film shot needs (a photo mode, single shot, 4:3,
size L, M1, M2 or S, and My Colors Off). While Print is the engine, Canon's
own colour table stays in place, so the inks come out exactly as written.
The settings are stored in `CHDK/FILM/PRINT.CFG`, and a saved look made in
Print keeps them ([FEATURES.md](FEATURES.md#saved-looks)).

**Print flaws**, after Riso Kagaku's own patents:

| Level | Drum skew | Lost dots | White specks | Feed streaks |
| --- | --- | --- | --- | --- |
| Subtle | 0.12°, −0.08° | 1–4 px dashes now and then | rare | ±8 tone, 2–3 bands |
| Strong | 0.3°, −0.2° | twice as often | ~1.5% of 4 × 2 blocks | ±28, 3–5 wider bands |
| Heavy | 0.5°, −0.33° | four times as often | ~5%, running up to 16 px across or down | ±45, 4–7 bands |

The turns are about the frame's centre, with a random direction per shot.
Lost dots and specks only show where an ink would have printed: specks in
solids, nothing in the highlights.

## How it works

The tables the camera can't cheaply compute (the colour separation for each
ink set, the overprint colours for each paper, and the grain and dot
screens) are made by `tools/riso_pack.py` and shipped in
`CHDK/FILM/PRINT.BIN`. On the camera, `camera/film_riso.c` prints the frame
in place, two rows at a time: it reads each ink's tone where that ink's
drum put it, adds the noise and flaws, and screens every pixel pair to one
of the precomputed ink colours. A shot at the defaults takes about 4–6 s
at L.

`tools/riso_render.py PHOTO --out DIR [--sheet] [--live]` renders a photo or
CR2 through the camera's own code on a computer.
