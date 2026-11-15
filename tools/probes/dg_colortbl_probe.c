/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Research probe (G12 1.00G only): DIGIC 4's hardware
 * colour table, the 9x9x9 RGB -> YCbCr table with tetrahedral interpolation
 * that My Colors uses (ROM: PFMyColor.c 0xff914f30 copies a 4000-byte preset
 * to 0xa2e88; ManageImgTbl.c 0xff940188 / 0xff9401ac load it; Color Accent's
 * ColExt.c 0xffa982cc and the software copy 0xffa98f8c give the format:
 * 1000 words, index r + 10g + 100b, Y bits 20-29, signed U/V bits 10-19/0-9).
 *
 * Canon finds each My Colors preset through a pointer table in RAM at
 * 0x5b4c; entry 0 is "My Colors Off" (the identity table at 0xffbcb7c0).
 * Run 1: loads A/CHDK/FILM/HWLUT.BIN (4000 bytes) and points entry 0 at it.
 * Canon then copies our table whenever it selects My Colors Off: switch My
 * Colors to another setting and back to Off in the FUNC menu, and live view
 * and new shots should show the look. Run 2: puts Canon's table back (as does
 * turning the camera off). No code is patched and nothing else changes.
 * Logs to A/CHDK/FILM/DGCOLOR.TXT. Not part of Zrno. */
#include "camera_info.h"
#include "stdlib.h"
#include "module_def.h"

#define PRESETS    ((unsigned *volatile *)0x5b4c)
#define CANON_OFF  ((unsigned *)0xffbcb7c0)
#define WORK_TABLE ((volatile unsigned *)0xa2e88)   /* last table Canon selected */
#define CUR_TABLE  ((volatile unsigned *)0xa9864)   /* last table loaded */

static char logbuf[1024];
static int logn;
static void lg(const char *s) { int n = strlen(s); if (logn + n < (int)sizeof(logbuf)) { memcpy(logbuf + logn, s, n); logn += n; } }
static void grey(const char *name, volatile const unsigned *t)
{
    char s[48];
    int k;
    lg(name);
    for (k = 0; k < 10; k++) { sprintf(s, " %u", (t[111 * k] >> 20) & 0x3ff); lg(s); }
    lg("\n");
}

int _run()
{
    char t[160];
    unsigned *now = PRESETS[0];
    int fd, fd2;
    logn = 0;
    sprintf(t, "dgcolor preset0=%08x canon=%08x rec=%d play=%d\n", (unsigned)now, (unsigned)CANON_OFF, camera_info.state.mode_rec, camera_info.state.mode_play);
    lg(t);
    grey("canon off grey Y:", CANON_OFF);
    grey("selected (0xa2e88) grey Y:", WORK_TABLE);
    grey("loaded (0xa9864) grey Y:", CUR_TABLE);
    if (now != CANON_OFF) {                           /* ours is in: put Canon's back */
        PRESETS[0] = CANON_OFF;
        lg("restored Canon's My Colors Off table (switch My Colors away and back to apply)\n");
    } else if (CANON_OFF[111] != (64u << 20) || CANON_OFF[888] != (512u << 20)) {
        lg("refused: the ROM table is not the expected identity (not G12 1.00G?)\n");
    } else {
        unsigned *tab = umalloc(4000);                /* kept: Canon reads it from now on */
        int n = -1;
        fd = open("A/CHDK/FILM/HWLUT.BIN", O_RDONLY, 0777);
        if (fd >= 0 && tab) { n = read(fd, tab, 4000); close(fd); }
        sprintf(t, "HWLUT.BIN read %d bytes into %08x\n", n, (unsigned)tab);
        lg(t);
        if (n == 4000) {
            grey("ours grey Y:", tab);
            PRESETS[0] = tab;
            lg("My Colors Off now uses our table: in the FUNC menu pick another My Colors setting, then Off\n");
        } else {
            if (tab) ufree(tab);
            lg("refused: HWLUT.BIN missing or not 4000 bytes\n");
        }
    }
    fd2 = open("A/CHDK/FILM/DGCOLOR.TXT", O_WRONLY | O_CREAT | O_APPEND, 0777);
    if (fd2 >= 0) { write(fd2, logbuf, logn); write(fd2, "\n", 1); close(fd2); }
    return 0;
}

int _module_can_unload() { return 1; }
int _module_exit_alt() { return 0; }
typedef struct { base_interface_t base; } libdgcolor_sym;
libdgcolor_sym _libdgcolor = {{0, 0, _module_can_unload, _module_exit_alt, _run}};
ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM, sizeof(ModuleInfo), {1, 0},
    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE, ANY_PLATFORM_ALLOWED,
    (int)"DIGIC colour table probe",
    MTYPE_TOOL,
    &_libdgcolor.base,
    ANY_VERSION, ANY_VERSION, ANY_VERSION, CAM_INFO_VERSION,
    0x28,
};
