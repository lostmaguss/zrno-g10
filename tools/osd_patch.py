# SPDX-License-Identifier: GPL-2.0-or-later
"""CHDK's shooting-screen OSD under Zrno, for tools/integrate.py and the
menu simulator (tools/menu_sim.py), which build the same code.

The Zrno indicator card (camera/film_ui.c) draws in CHDK's OSD pass, after
CHDK's own elements. CHDK's r6355 defaults also draw the SD card's free
space (text and a long bar along the bottom), the battery (icon and
percentage), the optics temperature and a clock with seconds. Zrno turns
these off by default, so the shooting screen is Canon's own with only the
Zrno card over it; each stays in CHDK's OSD menu to turn back on.

A card whose CHDK/OSD__4.CFG was saved before keeps the values stored
there: CHDK loads its defaults first, then the saved file over them."""

# CHDK's osd_conf_info entries (core/conf.c): (name, r6355 default, Zrno's).
# The battery's voltage and the SD card's icon and percentage are off in
# r6355 already; they are listed so that every element stays off.
OSD_OFF = [
    ('show_clock', 2, 0),          # clock with seconds
    ('show_temp', 1, 0),           # optics temperature
    ('batt_icon_show', 1, 0),
    ('batt_perc_show', 1, 0),
    ('batt_volts_show', 0, 0),
    ('space_icon_show', 0, 0),
    ('space_perc_show', 0, 0),
    ('space_mb_show', 1, 0),       # free space in MB
    ('space_bar_show', 1, 0),      # the long free-space bar (1 along the bottom, 2 along the side)
]


def conf_line(name, value):
    """The default's text in core/conf.c, as it is aligned there."""
    return f'conf.{name},'.ljust(45) + f'CONF_DEF_VALUE, i:{value})'


def osd_edits(edit):
    """Apply the OSD changes with edit(name, old, new) on a CHDK r6355 tree."""
    for name, old, new in OSD_OFF:
        if old != new:
            edit('core/conf.c', conf_line(name, old), conf_line(name, new))
    # The Zrno indicator: drawn in CHDK's OSD pass (not while the OSD
    # layout is edited), after CHDK's own elements.
    edit('core/gui_osd.c', '#include "platform.h"', '#include "platform.h"\n#include "film_ui.h"')
    edit('core/gui_osd.c', '            gui_osd_draw_ev(is_osd_edit);\n', '            gui_osd_draw_ev(is_osd_edit);\n            if(!is_osd_edit)film_ui_draw_osd();\n')
