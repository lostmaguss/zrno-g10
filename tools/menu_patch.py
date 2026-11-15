# SPDX-License-Identifier: GPL-2.0-or-later
"""CHDK edits for the Zrno menu's renderer (camera/film_menu.c).

Shared by tools/integrate.py (the camera build) and tools/menu_sim.py (the
Mac simulator), so both draw the menu through the same patched CHDK code.
`edit(name, old, new)` replaces exactly one occurrence in a tree file.
"""

def menu_edits(edit, palette=True):
    # gui_menu.c keeps navigation; drawing the Zrno menu and its Settings
    # submenu goes to film_menu_draw, whose row count becomes the scroll window.
    edit('core/gui_menu.c', '#include "gui_palette.h"', '#include "gui_palette.h"\n#include "film_menu.h"')
    edit('core/gui_menu.c', '''    if (gui_menu_redraw)
    {
        if (gui_menu_redraw==2)
            gui_menu_draw_initial();''', '''    if (gui_menu_redraw)
    {
        if (film_menu_ours(curr_menu))
        {
            count = gui_menu_rows();
            num_lines = film_menu_draw(curr_menu, &gui_menu_top_item, gui_menu_curr_item, gui_menu_redraw == 2);
            gui_menu_redraw = 0;
            return;
        }
        if (gui_menu_redraw==2)
            gui_menu_draw_initial();''')
    # The film menu's item list depends on the mode (Re-develop only in the
    # gallery): refresh it whenever it becomes the current menu.
    edit('core/gui_menu.c', '''static void gui_menu_set_curr_menu(CMenu *menu_ptr, int top_item, int curr_item) {
''', '''static void gui_menu_set_curr_menu(CMenu *menu_ptr, int top_item, int curr_item) {
    if (menu_ptr == &film_lut_menu)
        film_menu_prepare();
''')
    # Entering the Settings submenu or going back from it: the Zrno menus
    # paint the whole screen, so CHDK's erase would only flash the camera
    # view for a frame.
    edit('core/gui_menu.c', '''void gui_menu_erase_and_redraw()
{
    gui_menu_redraw = 2;
    gui_set_need_restore();
''', '''void gui_menu_erase_and_redraw()
{
    gui_menu_redraw = 2;
    if (!film_menu_ours(curr_menu))
        gui_set_need_restore();
''')
    # The film menu fills the screen: erase it when another menu replaces
    # it (its "CHDK menu" item, or MENU); Back and leaving the menu already do.
    edit('core/gui_menu.c', '''void gui_menu_init(CMenu *menu_ptr) {
''', '''void gui_menu_init(CMenu *menu_ptr) {

    if (film_menu_ours(curr_menu) && menu_ptr != curr_menu)
        gui_set_need_restore();
''')
    # SET on the LUT or Look row opens the look preview (camera/film_menu.c)
    # instead of stepping the value; right still steps it.
    edit('core/gui_menu.c', '''                    case MENUITEM_ENUM:
                    case MENUITEM_ENUM2:
                        update_enum_value(&curr_menu->menu[gui_menu_curr_item],1);
                        gui_menu_redraw=1;
                        break;''', '''                    case MENUITEM_ENUM:
                    case MENUITEM_ENUM2:
                        if (film_menu_ours(curr_menu) && film_preview_open(&curr_menu->menu[gui_menu_curr_item]))
                            break;
                        update_enum_value(&curr_menu->menu[gui_menu_curr_item],1);
                        gui_menu_redraw=1;
                        break;''')
    # A half-press of the shutter leaves the Zrno menus for the camera, as
    # the S button does (CHDK uses it there for the step size of number rows,
    # which the Zrno menus do not have). Elsewhere it keeps CHDK's meaning.
    edit('core/gui_menu.c', '''        case KEY_ERASE:
        case KEY_SHOOT_HALF:
            if (!increment_factor())''', '''        case KEY_SHOOT_HALF:
            if (film_menu_ours(curr_menu))
            {
                exit_alt();
                break;
            }
            /* fall through */
        case KEY_ERASE:
            if (!increment_factor())''')
    # A second RBF font, loaded once (ASCII only) and swapped in while the
    # Zrno menu draws; CHDK's own menu font is untouched.
    edit('lib/font/rbf_font.c', '''//-------------------------------------------------------------------
// Attempt to load font from file and set codepage''', '''//-------------------------------------------------------------------
// Zrno: the Zrno menu's font (the current font if missing)
int rbf_film_font(const char *file, int on)
{
    static font *film, *saved;
    static int tried;
    if (!on) {
        if (saved) rbf_font = saved;
        saved = 0;
        return 0;
    }
    if (!tried) {
        tried = 1;
        film = new_font();
        if (film && !rbf_font_load((char *)file, film, 126)) {
            free(film);
            film = 0;
        }
    }
    if (film && !saved) {
        saved = rbf_font;
        rbf_font = film;
    }
    return rbf_font->hdr.height;
}

//-------------------------------------------------------------------
// Attempt to load font from file and set codepage''')
    if not palette:
        return
    # While the menu is up, four CHDK slots hold its palette, and the dark-blue
    # slot its orange, which the shooting-screen indicator also borrows. The
    # checks on slots 1 and 7 reload whenever that changes.
    edit('platform/g12/lib.c', 'void load_chdk_palette()\n{', '#include "film_menu.h"\n#include "film_gbui.h"\nvoid load_chdk_palette()\n{')
    edit('platform/g12/lib.c', '''        if (pal && pal[CHDK_COLOR_BASE+0] != 0x33ADF62)''', '''        int film = film_menu_palette();
        if (pal && (pal[CHDK_COLOR_BASE+0] != 0x33ADF62 ||
                    pal[CHDK_COLOR_BASE+FILM_PAL_SLOT_BG] != (film == 2 ? FILM_PAL_BG : 0x326EA40) ||
                    pal[CHDK_COLOR_BASE+FILM_PAL_SLOT_ICON] != (film ? FILM_PAL_ACCENT : 0x31250F3) ||
                    pal[CHDK_COLOR_BASE+FILM_GBUI_SLOT0] != (int)film_gbui_palette_slot(FILM_GBUI_SLOT0, 0x373BFAE) ||
                    pal[CHDK_COLOR_BASE+FILM_GBUI_SLOT3] != (int)film_gbui_palette_slot(FILM_GBUI_SLOT3, 0x3DED115) ||
                    pal[CHDK_COLOR_BASE+FILM_GBUI_SLOT4] != (int)film_gbui_palette_slot(FILM_GBUI_SLOT4, 0x34766F0)))''')
    edit('platform/g12/lib.c', '''            pal[CHDK_COLOR_BASE+13] = 0x0090000;  // Transparent dark grey
''', '''            pal[CHDK_COLOR_BASE+13] = 0x0090000;  // Transparent dark grey
            if (film)
                pal[CHDK_COLOR_BASE+FILM_PAL_SLOT_ICON]    = FILM_PAL_ACCENT;
            if (film == 2)
            {
                pal[CHDK_COLOR_BASE+FILM_PAL_SLOT_BG]      = FILM_PAL_BG;
                pal[CHDK_COLOR_BASE+FILM_PAL_SLOT_SURFACE] = FILM_PAL_SURFACE;
                pal[CHDK_COLOR_BASE+FILM_PAL_SLOT_ACCENT]  = FILM_PAL_ACCENT;
                pal[CHDK_COLOR_BASE+FILM_PAL_SLOT_MUTED]   = FILM_PAL_MUTED;
            }
            /* ZrnoBoy's live view: its palette's four shades; Print's:
             * its ink colours (three more slots with three inks). */
            pal[CHDK_COLOR_BASE+FILM_GBUI_SLOT0] = film_gbui_palette_slot(FILM_GBUI_SLOT0, 0x373BFAE);
            pal[CHDK_COLOR_BASE+FILM_GBUI_SLOT1] = film_gbui_palette_slot(FILM_GBUI_SLOT1, 0x3A9A917);
            pal[CHDK_COLOR_BASE+FILM_GBUI_SLOT2] = film_gbui_palette_slot(FILM_GBUI_SLOT2, 0x3819137);
            pal[CHDK_COLOR_BASE+FILM_GBUI_SLOT3] = film_gbui_palette_slot(FILM_GBUI_SLOT3, 0x3DED115);
            pal[CHDK_COLOR_BASE+FILM_GBUI_SLOT4] = film_gbui_palette_slot(FILM_GBUI_SLOT4, 0x34766F0);
            pal[CHDK_COLOR_BASE+FILM_GBUI_SLOT5] = film_gbui_palette_slot(FILM_GBUI_SLOT5, 0x37F408F);
            pal[CHDK_COLOR_BASE+FILM_GBUI_SLOT6] = film_gbui_palette_slot(FILM_GBUI_SLOT6, 0x3512D5B);
''')
