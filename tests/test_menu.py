#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Zrno menu in the Mac simulator (CHDK's own menu, drawing and font code
with tools/menu_patch.py applied): every row reachable with the cursor on
screen and one row tall, only known palette colours, wrap-around, the
Settings submenu drawn in the same theme without an erase in between,
"CHDK menu" there and MENU reaching CHDK's main menu on an erased screen with
CHDK's own colours back, Back leaving to <ALT> mode and S returning, and the
shooting screen (CHDK's own OSD code) showing only the Zrno card.
Skipped without the CHDK r6355 tree."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import menu_sim

try:
    menu_sim.chdk_base()
except SystemExit:
    print('Zrno menu simulator: skipped (no CHDK r6355 tree)')
    sys.exit(0)
menu_sim.build()
# The card's menu font keeps its descenders: CHDK's Verdana 16 cut the bowl
# of g and the hook of j off at the bottom of the glyph cell. A complete
# bowl is one run of pixels on the glyph's lowest row.
import struct
rbf = (Path(__file__).resolve().parents[1] / 'card/CHDK/FILM/MENU.RBF').read_bytes()
cs, pts, fh, mw, cf, cl, unk, wm, cm = struct.unpack('<9i', rbf[72:108])
fw = 8 * cs // fh
for ch in 'gj':
    i = ord(ch) - cf
    d, w = rbf[cm + i * cs:cm + (i + 1) * cs], rbf[wm + i]
    rows = [''.join('#' if d[r * fw // 8 + x // 8] & (1 << (x % 8)) else '.' for x in range(w)) for r in range(fh)]
    low = [r for r in rows if '#' in r][-1].strip('.')
    assert '.' not in low and len(low) >= 2, f'menu font {ch!r} clipped: {low}'
# No gap before y ("ZrnoBoy"): it starts one column in, as the other letters.
for ch in 'oy':
    i = ord(ch) - cf
    d, w = rbf[cm + i * cs:cm + (i + 1) * cs], rbf[wm + i]
    cols = [any(d[r * fw // 8 + x // 8] & (1 << (x % 8)) for r in range(fh)) for x in range(w)]
    assert cols.index(True) <= 1, f'menu font {ch!r} starts {cols.index(True)} columns in'

BASE = 0xB3
ACCENT, BG = BASE + 4, BASE + 1                      # film palette slots while the menu is up
ROWS = 23                                            # items in camera/film_menu.c outside the gallery
GBOY_ROWS = 16                                       # the ZrnoBoy engine's items outside the gallery
RISO_ROWS = 17                                       # the Print engine's items outside the gallery (Riso style)
GRADE_ROWS = 32                                      # the Grade engine's items outside the gallery

def cursor(sim):
    # The strip at the row's right end, between the value card and the
    # row's rounded end: only the cursor colours it (icons sit at x 18-30).
    ys = [y for y in range(menu_sim.H) if ACCENT in sim.idx[y * menu_sim.W + 340:y * menu_sim.W + 342]]
    return (min(ys), max(ys)) if ys else None

def known(sim):
    pal = sim.pal
    used = set(sim.idx)
    bad = [i for i in used if tuple(pal[4 * i:4 * i + 3]) == (255, 0, 255) and pal[4 * i + 3] == 255]
    assert not bad, f'unknown palette indexes {bad}'

sim = menu_sim.Sim()
assert sim.state == 'OK Zrno menu', sim.state
first = cursor(sim)
film_bg = sim.pal[4 * BG:4 * BG + 4]
assert first, 'no cursor'
height = first[1] - first[0] + 1
assert height == fh + 7, height                                     # font height + padding
# The Zrno logo, an orange bean with a navy crease, left of the title.
logo = [sim.idx[y * menu_sim.W + x] for y in range(0, 26) for x in range(18, 31)]
assert logo.count(ACCENT) > 60 and len(set(logo)) >= 3, 'no logo'          # orange body, navy crease, background
# No Status row: the title bar's right side stays empty while Ready.
title_right = [sim.idx[y * menu_sim.W + x] for y in range(0, 24) for x in range(160, 345)]
assert set(title_right) == {BG}, 'status shown while Ready'
seen = [first]
for i in range(ROWS - 1):
    sim.send('down')
    assert sim.state == 'OK Zrno menu', sim.state
    c = cursor(sim)
    assert c and c[1] - c[0] + 1 == height and c[1] < menu_sim.H, (i, c)
    known(sim)
    seen.append(c)
assert seen[-1][0] > seen[0][0]                                     # scrolled to the bottom row (Back)
sim.send('down')
assert cursor(sim) == first                                         # wraps to the top
# Settings: up from the top wraps to Back, then Settings. It opens in the
# menu's own theme, straight over the menu (no erased frame), with the
# camera rows, CHDK menu and Back; Back returns to the Settings row.
sim.send('up'); sim.send('up')
settings_row = cursor(sim)
sim.send('set')
assert sim.state == 'OK Zrno settings', sim.state
assert sim.pal[4 * BG:4 * BG + 4] == film_bg and sim.idx.count(0) <= 1, 'Settings not drawn in the Zrno theme'   # pixel 0 is CHDK's guard pixel
known(sim)
sim.send('up')                                                      # Back
sim.send('set')
assert sim.state == 'OK Zrno menu' and cursor(sim) == settings_row, sim.state
# "CHDK menu", the fifth Settings row (Indicator, Busy LEDs, S button opens, Live preview).
sim.send('set')
for k in ['up', 'up']:                                              # Back, then CHDK menu
    sim.send(k)
sim.send('set')
assert sim.state == 'OK CHDK menu', sim.state
assert sim.pal[4 * BG:4 * BG + 4] != film_bg, 'film palette left on'
assert 0 in sim.idx, 'screen not erased around CHDK menu'
known(sim)
# MENU from the film menu also opens CHDK's main menu; Back leaves to <ALT>; S returns.
sim = menu_sim.Sim()
sim.send('menu')
assert sim.state == 'OK CHDK menu' and 0 in sim.idx
sim = menu_sim.Sim()
sim.send('up')
sim.send('set')
assert sim.state.startswith('OK <ALT>') and set(sim.idx) == {0}, sim.state
sim.send('s')
assert sim.state == 'OK Zrno menu' and cursor(sim)
# Developing overlay: themed panel with the accent progress bar, theme
# palette while shown, then SAVED, then FAILED with the red dot.
sim = menu_sim.Sim()
sim.send('develop'); sim.send('develop')
assert sim.state.startswith('OK Shooting screen: developing'), sim.state
assert sim.pal[4 * BG:4 * BG + 4] == film_bg and ACCENT in sim.idx and BG in sim.idx
known(sim)
for i in range(7):                                                  # one step per stage of the progress panel
    sim.send('develop')
assert sim.state == 'OK Shooting screen' and ACCENT in sim.idx       # saved card with the accent dot
sim.send('fail')
assert BASE + 0 in sim.idx and BG in sim.idx                         # red dot on the themed card
# Shooting-screen indicator: with processing on, the card shows with the
# orange in the dark-blue slot only; the battery/SD slots keep CHDK's colours.
sim = menu_sim.Sim()
for k in ['right', 'down', 'down', 'down', 'down', 'right', 'shoot', 'shoot']:   # Film processing On, Halation Low
    sim.send(k)
ICON = BASE + 7
assert sim.state == 'OK Shooting screen' and ICON in sim.idx and 0xFF in sim.idx, sim.state
r, g, b = sim.pal[4 * ICON:4 * ICON + 3]
assert r > 200 and g < 140 and b < 60, (r, g, b)                     # orange
assert sim.pal[4 * BG:4 * BG + 4] != film_bg                         # theme slots not borrowed here
known(sim)
sim.send('pause')
assert ICON not in sim.idx and 0x16 in sim.idx                       # paused: grey dot, no icons
# CHDK's own shooting-screen OSD (gui_osd.c with tools/osd_patch.py, as on
# the camera) draws nothing by default: no SD card space or bar, battery,
# temperature or clock, so the screen is Canon's. With CHDK r6355's stock
# defaults ("chdk-osd") the same code draws them, and the indicator card
# is the only thing drawn with Zrno's.
def drawn(sim):
    return [i for i, v in enumerate(sim.idx) if v]
def bbox(px):
    xs, ys = [i % menu_sim.W for i in px], [i // menu_sim.W for i in px]
    return min(xs), min(ys), max(xs), max(ys)
sim = menu_sim.Sim()
sim.send('shoot')
assert sim.state == 'OK Shooting screen' and not drawn(sim), 'CHDK OSD drawn with Processing Off'
sim.send('chdk-osd')
stock = bbox(drawn(sim))
assert stock[1] == 0 and stock[3] >= 233, f'CHDK stock OSD not drawn: {stock}'   # battery/clock at the top, space bar at the bottom
sim = menu_sim.Sim()
for k in ['right', 'shoot']:                                         # Processing On
    sim.send(k)
card = drawn(sim)
x0, y0, x1, y1 = bbox(card)
assert ICON in sim.idx and len(card) > 0.9 * (x1 - x0 + 1) * (y1 - y0 + 1), (x0, y0, x1, y1)   # one card, nothing else
indicator = sim.idx[:]
sim.send('chdk-osd')
assert bbox(drawn(sim)) != (x0, y0, x1, y1), 'CHDK stock OSD not drawn beside the card'
sim.send('chdk-osd')
assert sim.idx == indicator, 'CHDK OSD left over after Zrno defaults again'
# Halation colour, the row under Halation: Red by default, Amber stored as
# the HALOAMB.TXT marker, removed again for Red.
sim = menu_sim.Sim()
amber = sim.work / 'A/CHDK/FILM/HALOAMB.TXT'
for k in ['down', 'down', 'down', 'down', 'down', 'right']:          # Processing .. Halation colour: Amber
    sim.send(k)
for _ in range(2): sim.send('up'); sim.send('down')                  # the change is applied
assert amber.exists(), 'Amber not stored'
sim.send('left')
for _ in range(2): sim.send('up'); sim.send('down')
assert not amber.exists(), 'Red kept the marker'
# Re-develop photo shows only in the gallery, as the first row.
sim = menu_sim.Sim()
top = sim.idx[:]
sim.send('gallery')
assert sim.state == 'OK Zrno menu (gallery)', sim.state
assert cursor(sim) == first and sim.idx != top                       # a different first row
for i in range(ROWS - 1):
    sim.send('down')
assert cursor(sim) != first                                           # one row more than outside the gallery
sim.send('record')
assert sim.state == 'OK Zrno menu' and sim.idx == top
# Every row carries an icon before its label (the effect rows the
# indicator's): on the cursor row it is drawn dark, as the label, so the
# icon column of each row holds dark pixels when the cursor is on it.
def icon_column(sim):
    top, bottom = cursor(sim)
    return {sim.idx[y * menu_sim.W + x] for y in range(top + 4, bottom - 3) for x in range(18, 31)}
# A half-press of the shutter leaves the Zrno menu, its Settings and a row
# preview for the shooting screen, as S does.
sim = menu_sim.Sim()
sim.send('half')
assert sim.state == 'OK Shooting screen', sim.state
sim.send('s'); sim.send('up'); sim.send('up'); sim.send('set')      # Settings
assert sim.state == 'OK Zrno settings', sim.state
sim.send('half')
assert sim.state == 'OK Shooting screen', sim.state
sim.send('s')
for k in ['down', 'down', 'down', 'set']:                            # Look, Engine, LUT: its preview
    sim.send(k)
assert sim.state == 'OK Zrno preview', sim.state
sim.send('half')
assert sim.state == 'OK Shooting screen', sim.state
sim.send('s')
assert sim.state == 'OK Zrno menu', sim.state                        # and S opens the menu again
sim = menu_sim.Sim()
for i in range(ROWS):
    assert BG in icon_column(sim), f'no icon on row {i}'
    sim.send('down')
sim.send('up'); sim.send('up'); sim.send('set')                    # Settings
for i in range(6):
    assert BG in icon_column(sim), f'no icon on Settings row {i}'
    sim.send('down')
# Look preview: SET on the LUT row (shooting mode) hides the menu over the
# live view and leaves a themed card at the bottom; left/right and the back
# dial step the LUT; SET or MENU returns to the menu on the LUT row with the
# new value. In the gallery SET steps the value as before; with no looks
# saved the Look row does too.
sim = menu_sim.Sim()
sim.send('down'); sim.send('down'); sim.send('down')                 # Look, Engine, LUT
lut_row = cursor(sim)
lut_before = sim.idx[:]
sim.send('set')
assert sim.state == 'OK Zrno preview', sim.state
card = 175 * menu_sim.W
assert BG not in sim.idx[:card] and BG in sim.idx[card:], 'menu not hidden, or no card'
known(sim)
shown = sim.idx[:]
sim.send('right')
assert sim.state == 'OK Zrno preview' and sim.idx != shown, 'right did not step the LUT'
stepped = sim.idx[:]
sim.send('dial-right')
assert sim.idx != stepped, 'the dial did not step the LUT'
sim.send('dial-left'); sim.send('left')
assert sim.idx == shown, 'left and the dial back did not return to the first LUT'
sim.send('right')
sim.send('set')
assert sim.state == 'OK Zrno menu' and cursor(sim) == lut_row and sim.idx != lut_before, sim.state
sim.send('set'); sim.send('menu')
assert sim.state == 'OK Zrno menu' and cursor(sim) == lut_row, sim.state
sim.send('up'); sim.send('up')                                       # Engine, Look: nothing saved
sim.send('set')
assert sim.state == 'OK Zrno menu', sim.state
sim = menu_sim.Sim()
sim.send('gallery')
for k in ['down', 'down', 'down', 'down']:                           # Re-develop, Processing, Look, Engine, LUT
    sim.send(k)
sim.send('set')
assert sim.state == 'OK Zrno menu (gallery)', sim.state
# ZrnoBoy. Engine (the third row, after Look) switches the list to the engine's rows,
# the cursor staying on Engine; each is reachable and has an icon. With
# Processing On the shooting screen shows the ZrnoBoy screen in its
# palette's four shades (CHDK's green and yellow slots), and SET on a
# ZrnoBoy row previews it there with the card. The choice is stored.
GB = [BASE + 3, BASE + 10, BASE + 11, BASE + 12]
sim = menu_sim.Sim()
sim.send('down'); sim.send('down')                                   # Look, Engine
engine_row = cursor(sim)
sim.send('right')
assert sim.state == 'OK Zrno menu' and cursor(sim) == engine_row, sim.state
known(sim)
for i in range(GBOY_ROWS):
    assert BG in icon_column(sim), f'no icon on ZrnoBoy row {i}'
    sim.send('down')
assert cursor(sim) == engine_row                                     # wrapped after GBOY_ROWS rows
sim.send('up'); sim.send('up'); sim.send('right')                    # Look, Processing: On
sim.send('shoot')
assert sim.state == 'OK Shooting screen', sim.state
counts = [sim.idx.count(c) for c in GB]
assert sum(counts) == menu_sim.W * menu_sim.H - 1 and min(counts) > 0, counts   # the whole screen (but the guard pixel), all four shades
r, g, b = sim.pal[4 * GB[0]:4 * GB[0] + 3]
assert g > r > b and g > 150, (r, g, b)                              # Pea green's lightest shade
assert (sim.work / 'A/CHDK/FILM/ZRNOBOY.CFG').read_bytes()[:9] == b'G12GB01\0\x01'
# Swung round to face the lens: the whole panel is still the ZrnoBoy screen,
# the frame reads the same (the overlay is written pre-mirrored) and the
# picture is a mirror image, as Canon's own live view there.
import array
normal = sim.idx[:]
sim.send('flip')
flipped = sim.idx[:]
assert all(v in GB for v in flipped[1:]), 'flipped screen not all ZrnoBoy'
W = menu_sim.W
side = lambda idx: [idx[y * W + x] for y in range(240) for x in range(0, 30)]
assert side(normal) == side(flipped), 'frame not the right way round when flipped'
# The synthetic scene's sun is right of the middle; mirrored, it is left.
# (The dither pattern stays put, so compare where the light is.)
def light_x(idx):
    solid = lambda x, y: all(idx[(y + j) * W + x + i] == GB[0] for i in range(5) for j in range(4))   # the sun, not the dithered sky
    pts = [x for y in range(30, 130) for x in range(40, 316) if solid(x, y)]
    return sum(pts) / len(pts)
assert light_x(normal) > 200 and light_x(flipped) < 160, ('picture not mirrored', light_x(normal), light_x(flipped))
sim.send('flip')
sim.send('s')
for k in ['down', 'down', 'down', 'down']:                           # Look, Engine, Palette, Frame
    sim.send(k)
sim.send('set')
assert sim.state == 'OK Zrno preview', sim.state
card = 175 * menu_sim.W
assert BG not in sim.idx[:card] and BG in sim.idx[card:] and set(GB) <= set(sim.idx[:card]), 'no ZrnoBoy screen with the card'
shown = sim.idx[:]
sim.send('right')
assert sim.state == 'OK Zrno preview' and sim.idx != shown, 'right did not step the frame'
sim.send('set')
assert sim.state == 'OK Zrno menu', sim.state
# Leaving a preview another way (S leaves <ALT> mode, as a script's end
# does) must not leave it set: the live screen comes back on the shooting
# screen, with Dither Off too (it stayed off on the camera).
for k in ['down', 'down', 'down', 'down', 'down']:                   # Resolution, Contrast, Brightness, Edges, Dither
    sim.send(k)
sim.send('set'); sim.send('left')
assert sim.state == 'OK Zrno preview', sim.state
sim.send('s'); sim.send('shoot')
assert sim.state == 'OK Shooting screen' and all(v in GB for v in sim.idx[1:]), 'live screen not back after leaving the preview'
# The higher resolution: the shooting screen is still the whole ZrnoBoy
# screen, now one row per canvas row (rows differ from their neighbours).
sim.send('s')
for k in ['down', 'down', 'down', 'down', 'down', 'right']:          # Look, Engine, Palette, Frame, Resolution: 256x192
    sim.send(k)
sim.send('shoot')
assert sim.state == 'OK Shooting screen' and all(v in GB for v in sim.idx[1:]), 'no live screen at 256x192'
W = menu_sim.W
pairs = sum(sim.idx[y * W:(y + 1) * W] == sim.idx[(y + 1) * W:(y + 2) * W] for y in range(0, 238, 2))
assert pairs < 100, pairs                                            # not every row doubled any more
sim.send('s')
for k in ['down', 'down', 'down', 'down', 'down', 'left', 'up']:    # back to 128x96, on Frame
    sim.send(k)
for k in ['up', 'up', 'right']:                                      # Engine on to Print
    sim.send(k)
# Print: its rows, each with an icon; the shooting screen in the ink
# colours (two inks: ZrnoBoy's four slots, the dark ink blue); SET on a row
# previews it with the card; three inks use three more slots and black; the
# settings are stored.
assert sim.state == 'OK Zrno menu' and cursor(sim) == engine_row, sim.state
known(sim)
for i in range(RISO_ROWS):
    assert BG in icon_column(sim), f'no icon on Print row {i}'
    sim.send('down')
assert cursor(sim) == engine_row                                     # wrapped after RISO_ROWS rows
sim.send('shoot')
assert sim.state == 'OK Shooting screen', sim.state
assert all(v in GB for v in sim.idx[1:]), 'Print live screen not in the ink slots'
assert len(set(sim.idx[1:])) >= 3, set(sim.idx[1:])
r, g, b = sim.pal[4 * GB[1]:4 * GB[1] + 3]
assert b > r + 60 and b > g, (r, g, b)                               # mask 1: the dark ink, medium blue
r, g, b = sim.pal[4 * GB[0]:4 * GB[0] + 3]
assert min(r, g, b) > 210, (r, g, b)                                 # mask 0: the off-white paper
sim.send('flip')
assert all(v in GB for v in sim.idx[1:]), 'flipped Print screen not in the ink slots'
sim.send('flip')
sim.send('s')
sim.send('down'); sim.send('down'); sim.send('down'); sim.send('down')   # Look, Engine, Style, Inks
sim.send('set')
assert sim.state == 'OK Zrno preview', sim.state
assert BG not in sim.idx[:card] and BG in sim.idx[card:] and set(sim.idx[1:card]) <= set(GB), 'no Print screen with the card'
shown = sim.idx[:]
sim.send('left')                                                     # Yellow+Pink+Blue (wraps back)
assert sim.state == 'OK Zrno preview' and sim.idx != shown, 'left did not step the inks'
sim.send('set')
sim.send('shoot')
RS = GB + [BASE + 6, BASE + 8, BASE + 9, 0xFF]
assert all(v in RS for v in sim.idx[1:]), 'three-ink screen outside its slots'
assert set(sim.idx[1:]) - set(GB), 'three inks drew only four colours'
sim.send('s')
for k in ['down', 'down', 'down', 'down', 'right', 'up', 'up']:     # Inks back to Pink + Blue, on Engine
    sim.send(k)
assert (sim.work / 'A/CHDK/FILM/ZRNOBOY.CFG').read_bytes()[:9] == b'G12GB01\0\x02'   # stored a second after the change
# Style: each style switches the rows (Style stays the fifth row, the
# cursor on it), has an icon on every row and a live screen within its
# colour slots; text mode has only Style and Palette. Back to Riso after.
STYLE_ROWS = {1: 14, 2: 14, 3: 14, 4: 14, 5: 14, 6: 11}             # rows outside the gallery: Processing .. Back
sim.send('down')                                                     # Style
style_row = cursor(sim)
for st in range(1, 7):
    sim.send('right')
    assert sim.state == 'OK Zrno menu' and cursor(sim) == style_row, (st, sim.state)
    for i in range(STYLE_ROWS[st]):
        assert BG in icon_column(sim), f'no icon on style {st} row {i}'
        sim.send('down')
    assert cursor(sim) == style_row, f'style {st}: {STYLE_ROWS[st]} rows expected'
    sim.send('shoot')
    assert sim.state == 'OK Shooting screen', sim.state
    assert all(v in RS for v in sim.idx[1:]) and len(set(sim.idx[1:])) >= 2, (st, set(sim.idx[1:]))
    sim.send('s'); sim.send('down'); sim.send('down'); sim.send('down')   # back on Style
sim.send('right')                                                    # wraps to Riso
assert cursor(sim) == style_row
sim.send('up')                                                       # on Engine
for _ in range(4): sim.send('down'); sim.send('up')                  # a second passes: stored
cfg = (sim.work / 'A/CHDK/FILM/PRINT.CFG').read_bytes()
assert len(cfg) == 48 and cfg[:8] == b'G12ZP02\0' and cfg[15] == 0, cfg[:16]
# Live preview Off (Settings): the shooting screen is Canon's live view with
# the indicator card again, while SET on a Print row still previews the
# print; stored in ZRNOBOY.CFG (bit 2 of byte 14). On again brings it back.
def live_preview(sim):                                               # from the menu on Engine: toggle the Settings row
    for k in ['up', 'up', 'up', 'up', 'set']:                        # Look, Processing, wrap to Back, Settings
        sim.send(k)
    for k in ['up', 'up', 'up', 'right', 'down', 'down', 'set']:     # Back, CHDK menu, Live preview: toggle; Back
        sim.send(k)
    for k in ['down', 'down', 'down', 'down']:                       # Settings -> Back -> Processing -> Look -> Engine
        sim.send(k)
live_preview(sim)
assert sim.state == 'OK Zrno menu' and cursor(sim) == engine_row, sim.state
sim.send('shoot'); sim.send('shoot')                                 # (the indicator waits a second after a live frame)
assert sim.state == 'OK Shooting screen', sim.state
assert not set(sim.idx[1:]) & set(GB), 'Print screen still drawn with Live preview Off'
assert 0xFF in sim.idx, 'no indicator card with Live preview Off'
sim.send('s'); sim.send('down'); sim.send('down'); sim.send('down'); sim.send('set')   # Look, Engine, Style: preview
assert sim.state == 'OK Zrno preview' and set(GB) <= set(sim.idx[1:card]), 'row preview without the print'
sim.send('set'); sim.send('up')                                      # back on Engine
for _ in range(4): sim.send('down'); sim.send('up')                  # a second passes: stored
assert (sim.work / 'A/CHDK/FILM/ZRNOBOY.CFG').read_bytes()[14] & 4, 'Live preview Off not stored'
live_preview(sim)
sim.send('shoot')
assert all(v in GB for v in sim.idx[1:]), 'Print screen not back with Live preview On'
sim.send('s'); sim.send('down'); sim.send('down')                    # Look, Engine
# Grade: its own rows with an icon on each, the cursor kept on Engine;
# a value steps and is stored in GRADE.CFG; SET previews it over Canon's
# own live view (no picture-engine screen); the HSL rows follow the chosen
# colour; Reset grade needs SET twice. The shooting screen is Canon's.
sim.send('right')
assert sim.state == 'OK Zrno menu' and cursor(sim) == engine_row, sim.state
for i in range(GRADE_ROWS):
    assert BG in icon_column(sim), f'no icon on Grade row {i}'
    sim.send('down')
assert cursor(sim) == engine_row, f'{GRADE_ROWS} Grade rows expected'
def grade_cfg():
    for _ in range(4): sim.send('down'); sim.send('up')              # a second passes: stored
    cfg = (sim.work / 'A/CHDK/FILM/GRADE.CFG').read_bytes()
    assert len(cfg) == 171 and cfg[:8] == b'G12GR02\0', cfg[:10]
    return cfg
sim.send('down'); sim.send('down')                                   # Grade, Contrast
sim.send('right'); sim.send('right')
cfg = grade_cfg()
assert cfg[8] == 0 and cfg[10] == 2, cfg[8:14]                       # Grade 1, contrast +2
sim.send('set')
assert sim.state == 'OK Zrno preview', sim.state
assert not set(sim.idx[1:card]) & set(GB), 'a picture-engine screen behind the grade preview'
assert BG in sim.idx[card:], 'no preview card'
sim.send('left')                                                     # contrast +1, from the preview
sim.send('set')
assert sim.state == 'OK Zrno menu', sim.state
assert grade_cfg()[10] == 1
for _ in range(4): sim.send('down')                                  # Contrast .. Blacks
sim.send('left'); sim.send('left'); sim.send('down'); sim.send('right')   # Blacks -2, Whites +1
cfg = grade_cfg()
assert cfg[10 + 37] == 254 and cfg[10 + 38] == 1, list(cfg[10 + 37:10 + 40])
for _ in range(6): sim.send('down')                                  # Whites .. Black & white
sim.send('left')                                                     # wraps from Off to Blue filter
cfg = grade_cfg()
assert cfg[10 + 39] == 6, list(cfg[10 + 37:10 + 40])
for _ in range(5): sim.send('right')                                 # Off, No filter .. Red filter
cfg = grade_cfg()
assert cfg[10 + 39] == 4, list(cfg[10 + 37:10 + 40])
sim.send('down'); sim.send('right')                                  # HSL colour: Orange
sim.send('down'); sim.send('right'); sim.send('right'); sim.send('right')   # Orange hue +3
cfg = grade_cfg()
assert cfg[9] == 1 and cfg[10 + 13 + 1] == 3 and cfg[10 + 13] == 0, cfg[9:30]
for _ in range(8): sim.send('down')                                  # Hue .. Reset grade
sim.send('set')
assert grade_cfg()[10] == 1, 'one SET reset the grade'                # and leaving the row disarms it
sim.send('set'); sim.send('set')
cfg = grade_cfg()
assert list(cfg[10:50]) == [0] * 10 + [3, 0, 3] + [0] * 27, list(cfg[10:50])   # neutral (split amounts 3)
for _ in range(25): sim.send('up')                                   # up to Processing (the rows unscrolled)
sim.send('down'); sim.send('down')                                   # Look, Engine
assert cursor(sim) == engine_row
sim.send('shoot')
assert sim.state == 'OK Shooting screen' and not set(sim.idx[1:]) & set(GB), 'a picture-engine screen under Grade'
sim.send('s'); sim.send('down'); sim.send('down')                    # Look, Engine
sim.send('right')                                                    # Engine back to Film
for i in range(ROWS - 2):
    sim.send('down')
assert cursor(sim) == first                                           # the film rows again: ROWS of them from Engine back to the top
assert (sim.work / 'A/CHDK/FILM/PRINT.CFG').read_bytes()[:9] == b'G12ZP02\0\x00'   # Inks back to Pink + Blue
# Looks in every engine: the Look row and the look actions sit in each
# engine's rows (Look the second, above Engine). A look saved in ZrnoBoy
# keeps the engine: recalled from Film, it switches the rows back to
# ZrnoBoy's with the cursor still on Look, and LOOKS.TXT names the engine.
sim = menu_sim.Sim()
sim.send('down'); sim.send('down'); sim.send('right')                # Engine: ZrnoBoy
for _ in range(8): sim.send('down')                                  # Palette .. Dither, Save as new look
sim.send('set')
for _ in range(10): sim.send('up')                                   # up to Processing (the rows unscrolled)
sim.send('down'); sim.send('down')                                   # Look, Engine
for _ in range(4): sim.send('down'); sim.send('up')                  # a second passes: stored
looks = (sim.work / 'A/CHDK/FILM/LOOKS.TXT').read_text()
assert 'Look 1|ZrnoBoy|palette=0 frame=1 ' in looks, looks
sim.send('left')                                                     # Engine back to Film
assert sim.state == 'OK Zrno menu' and cursor(sim) == engine_row, sim.state
for _ in range(ROWS): sim.send('down')
assert cursor(sim) == engine_row                                      # the Film rows
sim.send('up')                                                       # Look, the rows unscrolled
look_row = cursor(sim)
sim.send('right')                                                    # Custom: the settings stay
for _ in range(ROWS): sim.send('down')
assert cursor(sim) == look_row, 'Custom changed the engine'
sim.send('right')                                                    # recall Look 1
assert sim.state == 'OK Zrno menu' and cursor(sim) == look_row, sim.state
for _ in range(GBOY_ROWS): sim.send('down')
assert cursor(sim) == look_row, 'not the ZrnoBoy rows after recalling a ZrnoBoy look'
sim.send('down'); sim.send('right')                                  # Engine: Print (Look shows Look 1*)
for _ in range(RISO_ROWS): sim.send('down')
assert cursor(sim) == engine_row                                      # the Print rows, with the look rows
# The Look preview follows the engine: stepping from a Film look to the
# ZrnoBoy look shows the ZrnoBoy screen with the card, and back to the Film
# look Canon's live view again; SET returns to the Film rows on Look.
sim.send('left'); sim.send('left')                                   # Engine: Film
for _ in range(8): sim.send('up')                                    # Look, Processing, Back, Settings, Delete .. Save as new look
sim.send('set')                                                      # Look 2: Film
for _ in range(6): sim.send('down')                                  # back on Processing
sim.send('right'); sim.send('down')                                  # Processing On; Look
sim.send('set')
assert sim.state == 'OK Zrno preview' and not set(sim.idx[1:card]) & set(GB), 'a ZrnoBoy screen behind a Film look'
sim.send('right')                                                    # Custom: still the Film look's settings
assert sim.state == 'OK Zrno preview' and not set(sim.idx[1:card]) & set(GB), 'a ZrnoBoy screen on Custom'
sim.send('right')                                                    # Look 1: ZrnoBoy
assert sim.state == 'OK Zrno preview' and set(GB) <= set(sim.idx[1:card]) and BG in sim.idx[card:], 'no ZrnoBoy screen for the ZrnoBoy look'
sim.send('right')                                                    # Look 2: Film again
assert sim.state == 'OK Zrno preview' and not set(sim.idx[1:card]) & set(GB) and BG in sim.idx[card:], 'ZrnoBoy screen left behind the Film look'
sim.send('set')
assert sim.state == 'OK Zrno menu' and cursor(sim) == look_row, sim.state
for _ in range(ROWS): sim.send('down')
assert cursor(sim) == look_row, 'not the Film rows after the preview'
# A card from before the rename keeps its Print settings: ZRNOPRNT.CFG is
# read while PRINT.CFG is missing, the next change is stored as PRINT.CFG,
# and the old file is left as it was (an older build still reads it).
old = bytearray(48)
old[:8] = b'G12ZP02\0'
old[8:16] = bytes([1, 0, 0, 2, 2, 3, 0, 3])                          # Teal + Orange, ..., style 3 (Woodcut)
old[23:44] = bytes([1, 1, 1] * 7)
old[47] = (0xa5 + sum(old[:47])) & 255
plain_card = menu_sim.card_dir
def old_card(font):
    work = plain_card(font)
    (work / 'A/CHDK/FILM/ZRNOPRNT.CFG').write_bytes(bytes(old))
    return work
menu_sim.card_dir = old_card
sim = menu_sim.Sim()
menu_sim.card_dir = plain_card
for k in ['down', 'down', 'right', 'right', 'down', 'right', 'left']:   # Engine to Print; Style forth and back
    sim.send(k)
for _ in range(4): sim.send('down'); sim.send('up')                  # a second passes: stored
new = (sim.work / 'A/CHDK/FILM/PRINT.CFG').read_bytes()
assert new[8] == 1 and new[15] == 3, list(new[8:16])                 # the old inks and style carried over
assert (sim.work / 'A/CHDK/FILM/ZRNOPRNT.CFG').read_bytes() == bytes(old), 'old settings file changed'
# A card from before blacks, whites and black and white keeps its grades:
# G12GR01 (37 values a grade) is read, the new rows start at 0, and the
# next change is stored as G12GR02.
old = bytearray(160)
old[:8] = b'G12GR01\0'
old[8], old[9] = 1, 1                                                 # Grade 2, HSL colour Orange
for i in range(4):
    g = 10 + 37 * i
    old[g + 10], old[g + 12] = 3, 3                                   # split amounts
    old[g] = i + 1                                                    # contrast 1..4
    old[g + 13 + 1] = 256 - 2                                         # orange hue -2
old[159] = (0x3c + sum(old[:159])) & 255
def old_grade_card(font):
    work = plain_card(font)
    (work / 'A/CHDK/FILM/GRADE.CFG').write_bytes(bytes(old))
    return work
menu_sim.card_dir = old_grade_card
sim = menu_sim.Sim()
menu_sim.card_dir = plain_card
for k in ['down', 'down', 'right', 'right', 'right', 'down', 'down', 'right']:   # Engine to Grade; Grade 2's contrast +1
    sim.send(k)
new = grade_cfg()
assert new[8] == 1 and new[9] == 1, list(new[8:10])
for i in range(4):
    g = 10 + 40 * i
    assert list(new[g:g + 14]) == [i + 1 + (i == 1)] + [0] * 9 + [3, 0, 3, 0] and new[g + 14] == 254, (i, list(new[g:g + 40]))
    assert list(new[g + 37:g + 40]) == [0, 0, 0], (i, list(new[g + 37:g + 40]))
print(f'Zrno menu simulator: menu font descenders complete, bean logo in the title, {ROWS} items reachable, cursor {height} px tall and on screen, known colours only, wrap-around, an icon on every row (dark on the cursor), CHDK menu and MENU reach CHDK on an erased screen, Back to <ALT>, S returns, a half-press leaving the menu, Settings and a preview, Settings submenu in the theme without an erase, look preview (SET on LUT: card over the live view, left/right and back dial step, SET/MENU back to the row; not in the gallery or with no looks), looks in every engine (a ZrnoBoy look stored with its engine and recalled from Film onto the ZrnoBoy rows, the cursor kept on Look, the second row; the Look preview swapping the ZrnoBoy screen and the Canon live view as the engine of the look changes), Re-develop only in the gallery, Print settings carried over from ZRNOPRNT.CFG, the Engine row switching to the ZrnoBoy rows with its live screen in four shades (mirrored with the frame readable when the screen faces the lens) and preview, then to the Print rows with its live screen in the ink colours (three inks in seven slots and black), preview and stored settings, the six other print styles with their own rows and live screens, the Grade rows (stored values, Blacks, Whites and Black & white, preview over the Canon live view, HSL colour, Reset on a second SET, grades carried over from G12GR01), Live preview Off (the Canon view and the indicator, row previews still shown, stored) and On again, themed developing panel, SAVED and FAILED cards, shooting-screen icon card with its own orange slot and paused state, the CHDK OSD (SD card, battery, temperature, clock) off with only the card drawn and on with the CHDK stock defaults: passed')
