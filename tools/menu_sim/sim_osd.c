/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Mac menu simulator: the camera for CHDK's own shooting-screen OSD
 * (gui_osd.c, gui_batt.c, gui_space.c, gui_usb.c) and its configuration
 * defaults (conf.c), built from the CHDK tree with tools/osd_patch.py
 * applied, as on the camera. A charged battery, a card with free space, a
 * warm lens and a fixed time, so each element CHDK is set to show draws;
 * the modules (grids, histogram, zebra) draw nothing. */
#include "camera_info.h"
#include "conf.h"
#include "gui.h"
#include "gui_draw.h"
#include "modules.h"
#include "module_def.h"
#include "histogram.h"
#include "gui_grid.h"
#include "zebra.h"
#include "dng.h"
#include "conf_migrate.h"
#include "time.h"

/* Canon's menu state: CHDK draws its OSD only while no Canon menu is up,
 * which it tells by the word before canon_menu_active (32-bit addresses
 * on the camera, so compare as it does). */
int canon_menu_active;
char canon_shoot_menu_active;
int recreview_hold;
int zoom_status;
void sim_osd_init(void) { canon_menu_active = (int)(long)&canon_menu_active - 4; }

/* Battery, card, temperatures, time. */
long stat_get_vbatt() { return 7600; }
long get_vbatt_max() { return 8100; }
long get_vbatt_min() { return 6450; }
unsigned int GetFreeCardSpaceKb(void) { return 6 * 1024 * 1024; }
unsigned int GetTotalCardSpaceKb(void) { return 8 * 1024 * 1024; }
unsigned char get_active_partition(void) { return 1; }
int get_part_count(void) { return 1; }
int is_partition_changed(void) { return 0; }
int get_optical_temp() { return 31; }
int get_ccd_temp() { return 34; }
int get_battery_temp() { return 29; }
struct tm *get_localtime()
{
    static struct tm t = {.tm_hour = 14, .tm_min = 37, .tm_sec = 12, .tm_mday = 13, .tm_mon = 10, .tm_year = 126};
    return &t;
}

/* Shooting state: nothing CHDK overrides, no RAW, no video. */
unsigned int GetRawCount(void) { return 120; }
int is_video_recording() { return 0; }
long get_parameter_data(long id, void *buf, long bufsize) { (void)id; memset(buf, 0, bufsize); return bufsize; }
long get_property_case(long id, void *buf, long bufsize) { (void)id; memset(buf, 0, bufsize); return 0; }
int shooting_get_prop(int id) { (void)id; return 0; }
void shooting_set_prop(int id, int v) { (void)id; (void)v; }
short shooting_can_focus() { return 1; }
int shooting_get_aperture_from_av96(short av96) { (void)av96; return 4000; }
short shooting_get_av96_override_value() { return 0; }
short shooting_get_bv96() { return 0; }
int shooting_get_canon_image_format() { return 1; }
short shooting_get_canon_overexposure_value() { return 0; }
short shooting_get_common_focus_mode() { return 0; }
int shooting_get_digital_zoom_state(void) { return 0; }
short shooting_get_display_mode() { return 0; }
short shooting_get_drive_mode() { return 0; }
short shooting_get_ev_correction2() { return 0; }
short shooting_get_flash_mode() { return 0; }
short shooting_get_focus_mode() { return 0; }
int shooting_get_iso_mode() { return 0; }
short shooting_get_iso_override_value() { return 0; }
short shooting_get_iso_real() { return 200; }
int shooting_get_luminance() { return 0; }
int shooting_get_real_aperture() { return 4000; }
short shooting_get_resolution() { return 0; }
float shooting_get_shutter_speed_from_tv96(short tv96) { (void)tv96; return 0.004f; }
short shooting_get_sv96_real() { return 0; }
int shooting_get_zoom() { return 0; }
short shooting_iso_real_to_market(short isor) { return isor; }
void shooting_update_dof_values() {}
void shooting_video_bitrate_change(int v) { (void)v; }
int get_focal_length(int zp) { (void)zp; return 6100; }
int get_effective_focal_length(int zp) { (void)zp; return 28000; }
int get_zoom_x(int zp) { (void)zp; return 10; }
void EnterToCompensationEVF(void) {}
void MakeAFScan(void) {}

/* GUI, keys and the rest of CHDK. */
long kbd_is_key_clicked(long key) { (void)key; return 0; }
void console_draw(int force_redraw) { (void)force_redraw; }
void gui_reset_alt_helper() {}
void gui_lang_init() {}
const char *gui_alt_mode_button_enum(int change, int arg) { (void)change; (void)arg; return ""; }
const char *gui_hhmss_enum(int change, int arg) { (void)change; (void)arg; return ""; }
const char *gui_tv_override_value_enum(int change, int arg) { (void)change; (void)arg; return ""; }
const char *gui_subj_dist_override_value_enum(int change, int arg) { (void)change; (void)arg; return ""; }
const char *gui_subj_dist_override_koef_enum(int change, int arg) { (void)change; (void)arg; return ""; }
const char *gui_flash_power_modes_enum(int change, int arg) { (void)change; (void)arg; return ""; }
const char *gui_flash_exp_comp_modes_enum(int change, int arg) { (void)change; (void)arg; return ""; }
void init_user_menu(int num_items) { (void)num_items; }
void user_menu_save() {}
void user_menu_restore() {}
void save_params_values(int enforce) { (void)enforce; }
void script_load(const char *fn) { (void)fn; }
void set_usb_remote_state() {}
void disable_shutdown() {}
void enable_shutdown() {}
char *load_file(const char *name, int *size, int add0) { (void)name; (void)add0; if (size) *size = 0; return 0; }

/* Modules: none loaded, none drawing. */
static void no_grid(int force) { (void)force; }
static void no_histo(int is_osd_edit) { (void)is_osd_edit; }
static int no_zebra(int show) { (void)show; return 0; }
static libgrids_sym grids = {.gui_grid_draw_osd = no_grid};
static libhisto_sym histo = {.gui_osd_draw_histo = no_histo};
static libzebra_sym zebra = {.gui_osd_draw_zebra = no_zebra};
libgrids_sym *libgrids = &grids;
libhisto_sym *libhisto = &histo;
libzebra_sym *libzebra = &zebra;
libdng_sym *libdng;                 /* RAW off: conf.c does not reach it */
libconfmigrate_sym *libconfmigrate;
