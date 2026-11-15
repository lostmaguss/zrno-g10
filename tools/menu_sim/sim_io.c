/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Mac menu simulator, host side: file access for CHDK's font loader, the
 * tick clock, and a line protocol on stdin/stdout for tools/menu_sim.py.
 *
 * Each input line is a button name (up, down, left, right, set, menu, s,
 * half, erase, display, redraw). Each answer is "OK <state>\n" or
 * "ERR <message>\n", then the screen: 360 x 240 palette indexes and a
 * 256-entry RGBA palette. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
void sim_init(int show_root);
int sim_key(const char *name);
const char *sim_state(void);
void sim_indexes(unsigned char *out);
void sim_palette(unsigned char *rgba);
void film_sim_lut_poll(void);
/* The CHDK code passes CHDK's open flags (include/std/stdlib.h), which CHDK's
 * own open() maps for the camera's OS; this maps them to the host's. Passed
 * through unchanged they mean other things: on macOS CHDK's O_TRUNC is
 * O_CREAT (and files are never truncated), on Linux nothing is created. */
#define CHDK_O_ACCMODE 3
#define CHDK_O_APPEND 0x8
#define CHDK_O_CREAT 0x100
#define CHDK_O_TRUNC 0x200
int sim_open(const char *name, int flags, int mode)
{
    int host = flags & CHDK_O_ACCMODE;      /* RDONLY 0, WRONLY 1, RDWR 2 on the camera and the hosts */
    (void)mode;                             /* the camera ignores the mode */
    if (flags & CHDK_O_APPEND) host |= O_APPEND;
    if (flags & CHDK_O_CREAT) host |= O_CREAT;
    if (flags & CHDK_O_TRUNC) host |= O_TRUNC;
    return open(name, host, 0644);
}
int sim_read(int fd, void *p, long n) { return (int)read(fd, p, (size_t)n); }
int sim_lseek(int fd, long off, int whence) { return (int)lseek(fd, off, whence); }
int sim_close(int fd) { return close(fd); }
void *sim_malloc(long n) { return malloc((size_t)n); }
void ufree(void *p) { free(p); }
/* A simulated clock: 300 ms pass per button press, so settings apply (after
 * their 250 ms settle) and the overlay animates, as between real presses. */
static int sim_now = 10000;
int get_tick_count(void) { return sim_now; }
static void frame(void)
{
    static unsigned char idx[360 * 240], pal[1024];
    sim_indexes(idx);
    sim_palette(pal);
    fwrite(idx, 1, sizeof(idx), stdout);
    fwrite(pal, 1, sizeof(pal), stdout);
    fflush(stdout);
}
int main(int argc, char **argv)
{
    char line[64];
    sim_init(argc > 1 && !strcmp(argv[1], "--root"));
    printf("OK %s\n", sim_state());
    frame();
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        sim_now += 300;
        film_sim_lut_poll();
        if (sim_key(line)) printf("OK %s\n", sim_state());
        else printf("ERR unknown button %s\n", line);
        frame();
    }
    return 0;
}
