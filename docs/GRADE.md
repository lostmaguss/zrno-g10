# Grade — the in-camera grading engine

Grade is the Zrno menu's fourth engine. It grades the photo's colours in the
camera: tone, white balance, saturation, HSL per colour, split toning and
black and white through a coloured filter. Each grade is folded into the
colour table Canon's develop already applies, so:

- a graded shot takes no longer than Canon's own, with no sensor backup and
  no developing panel;
- it works in any drive mode (continuous too), aspect and image size;
- the live view shows the grade about a quarter of a second after a change.

It needs Processing On, a photo mode (P, Tv, Av, M, Auto) and My Colors Off.
The film effects don't apply; Sharpening does.

## Using it

Zrno menu → **Engine** → Grade. The rows edit the grade picked in the
**Grade** row. SET on any value row hides the menu over the live view with a
card for that row; left/right or the back dial step it, and the live view
follows. SET or MENU returns to the menu.

| Row | Values | What it does |
| --- | --- | --- |
| Grade | Grade 1 … Grade 4 | Which of the four stored grades is used and edited. |
| Contrast | −5 … +5 | An S-curve around mid grey: deeper shadows and brighter highlights (or flatter). Black and white stay put. |
| Midtones | −5 … +5 | Lifts or lowers the middle tones (up to about 38 levels), ends fixed. |
| Shadows | −5 … +5 | Lifts or darkens the shadows (peak at a third of the range). |
| Highlights | −5 … +5 | Brightens or holds back the highlights (peak at two thirds). |
| Blacks | −5 … +5 | Moves the black end of the curve, over the darkest 64 levels. −: the black point rises 6 levels a step (30 at −5), so the deepest shadows go black and the toe gets steeper, as on slide film. +: black lifts 6 levels a step and the lift fades out by 64 levels, as on faded instant film; the rest of the range is untouched. |
| Whites | −5 … +5 | Moves the white end, over the brightest 64 levels. −: white comes down 6 levels a step, to a soft paper white. +: the top clips earlier, 6 levels a step, for brighter, harder highlights. It acts last, after saturation and HSL, so no colour goes past that white. |
| Fade | Off, 1 … 5 | Lifts black, 8 levels a step, and scales the whole range to fit: a matte, faded print. It comes after Blacks, so a crushed toe can still be faded. |
| Temperature | −5 … +5 | Cooler (blue) or warmer (amber), in proportion to brightness, as a white balance. Black and white stay neutral. |
| Tint | −5 … +5 | Green (−) or magenta (+). |
| Saturation | −5 … +5 | −5 is black and white; +5 is 1.5 times. |
| Vibrance | −5 … +5 | Like saturation, but mostly on muted colours; already vivid ones barely move. |
| Black & white | Off, No filter, Yellow, Orange, Red, Green, Blue filter | Makes the grade black and white, through the coloured filter you'd put on a lens for black-and-white film. No filter is the plain brightness. Yellow darkens a blue sky a little, Orange more and Red to near black (Red also lightens skin, brick and red paint). Green lightens foliage and darkens reds. Blue lightens the sky and darkens skin. The HSL lightness rows then lighten or darken each colour in the grey, and split toning tints it (sepia, cyanotype). Saturation and Vibrance do nothing here. |
| HSL colour | Red, Orange, Yellow, Green, Aqua, Blue, Purple, Magenta | Which colour the next three rows edit (their labels name it). |
| *Colour* hue | −5 … +5 | Turns that colour's hue, 6° a step (±30°). Red +5 goes toward orange. |
| *Colour* saturation | −5 … +5 | −5 greys that colour out; +5 is 1.6 times. |
| *Colour* lightness | −5 … +5 | Darker (toward black) or lighter (toward white), 8% of the brightness a step, scaled by how saturated the colour is, so greys never move. |
| Shadow tone | Off or a colour | Split toning: a colour added to the shadows. |
| Shadow amount | 1 … 5 (3) | How strong, 4 levels of chroma a step at black. |
| Highlight tone | Off or a colour | A colour added to the highlights. |
| Highlight amount | 1 … 5 (3) | How strong, at white. |
| Sharpening | Original, Soft, Off, Crisp | Canon's edge enhancement, shared with the Film engine. |
| Reset grade | — | SET twice puts the current grade back to neutral (the row reads "SET again to reset" in between; leaving the row cancels). |

The shooting-screen indicator names the grade in use (Grade 1 … 4).

## The order of the grade

Colour balance, then the tone curve (contrast, midtones, shadows,
highlights, blacks, then fade), saturation and vibrance, HSL, whites, and
split toning last. In black and white, the grey mix through the filter comes
after HSL, and the tone curve is applied to that grey.

## Limits

Canon's table has 9 nodes per axis, so colours are only isolated as finely
as that grid allows: pushing two neighbouring colours hard in opposite
directions tints the near-neutral colours between them. Under Temperature
and Tint the highlights stay neutral; use Highlight tone to colour white.

The four grades are stored in `CHDK/FILM/GRADE.CFG`. A saved look made in
Grade keeps which grade it is, its values and Sharpening, and recalling it
puts them back into that grade ([FEATURES.md](FEATURES.md#saved-looks)).
`tools/grade_render.py IMAGE` renders a photo through sample grades with
the camera's own code.
