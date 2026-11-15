# Licences and third-party notices

## Zrno

Copyright (C) 2026 suzvuk.

The code in this repository is free software. You can redistribute and modify
it under the terms of the GNU General Public License, version 2 or (at your
option) any later version (`GPL-2.0-or-later`; see [COPYING](COPYING)). That
covers `camera/`, `installer/`, `tools/`, `tests/` and `web/`. It also covers the Warm 200
recipe (`tools/bake_warm.py`) and the LUTs and tables generated from it or by
the repository's tools, except for the LUT data noted below.

It is distributed WITHOUT ANY WARRANTY. Zrno runs unofficial firmware on
your camera at your own risk. See the warning in the [README](README.md).

## CHDK

Zrno is a modified build of [CHDK](https://chdk.fandom.com/) release-1_6,
SVN r6355, which is licensed under the GPL (see [COPYING](COPYING)). CHDK's
source is **not** included here. `tools/fetch_chdk.py` downloads it from
CHDK's own server:
https://subversion.assembla.com/svn/chdk/!svn/bc/6355/branches/release-1_6/

`tools/integrate.py` applies Zrno's changes to it. A firmware binary built
from this repository is a combined work under GPLv2. Anyone who redistributes
one must also offer its complete corresponding source, including the CHDK
tree. `tools/package_release.py` makes that source zip for each release.

## Canon firmware

This repository and Zrno's releases contain no Canon code or data of
Zrno's own. Zrno runs one task of the G12's firmware from a copy that has
hooks added. The camera makes that copy itself, in RAM, from its own ROM,
each time it starts (`camera/film_reloc.c`). Zrno's source and binaries
carry only the rules for making the copy, the addresses, and a hash that
recognises the right firmware. The repository names firmware addresses and
describes how the firmware behaves, as CHDK's own ports do. The CHDK G12
port that Zrno builds on contains firmware-derived startup code, as every
CHDK port does. It is distributed as CHDK distributes it.

## Film simulation LUTs (CC BY-SA 4.0)

The fifteen film and creative presets listed in
`luts/rawtherapee/selection.json` come from the **RawTherapee Film
Simulation Collection** (2015-09-20) by Pat David, Pavlov Dmitry, Michael Ezra
et al. They are licensed under
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/):

- Source: https://rawtherapee.com/shared/HaldCLUT.zip
- Pinned mirror: https://github.com/cedeber/hald-clut/tree/3b3180f82d4dcea1e6e8c5648473539a910d7f49/HaldCLUT/Film%20Simulation
- Changes: the sRGB Hald data is resampled tetrahedrally to a 17³ uint16 RGB
  lattice in Zrno's checked G12 LUT container, and monochrome output
  channels are replicated. Each look's DIGIC colour table (`.HWT`, `.HWA`) is
  fitted to that LUT.

The adapted LUTs and their fitted tables stay under CC BY-SA 4.0, not the GPL.
The original collection notice, hashes and exact source URLs are in
`luts/rawtherapee/`. The camera's copy of the notice is
`card/CHDK/FILM/CREDITS.TXT`.

## Menu font

`card/CHDK/FILM/MENU.RBF` is `CHDK/FONTS/arialround16.rbf` from CHDK's
release-1_6 font pack (a bitmap rendering of Arial Rounded MT Bold 16), with
one change: the `y` glyph moved two columns left and narrowed from 13 to 11
columns. It is distributed as CHDK distributes it. The date-stamp font
(`camera/film_font.h`) is drawn independently by `tools/stamp_font.py`.

## Names

Canon and PowerShot are trademarks of Canon Inc. The film and product names
used for the looks and effects (among them Kodak, Portra, Ektar, Tri-X,
Kodachrome, Fujifilm, Provia, Velvia, Astia, Superia, Acros, Ilford, HP5,
Polaroid and CineStill) are trademarks of their respective owners.

Zrno is an independent project. It is not affiliated with or endorsed by
any of them. Film-stock names only say which stock a look approximates. The
looks are artistic approximations, not official or calibrated film profiles.
