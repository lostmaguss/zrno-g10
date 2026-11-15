/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FILM_QUEUE_H
#define FILM_QUEUE_H
void film_capture_latch(void);
void film_opened(const char *name,int fd);
void film_write_result(int fd,int expected,int actual);
int film_closed(int fd,int result);
int film_special_closed(int result);
void film_boot_poll(void);
/* Why the current settings would or would not take the film route. */
enum {
    FILM_ROUTE_READY,FILM_ROUTE_OFF,FILM_ROUTE_NOT_RECORDING,
    FILM_ROUTE_MODE,FILM_ROUTE_DRIVE,FILM_ROUTE_ASPECT,FILM_ROUTE_SIZE,FILM_ROUTE_COLORS,
    FILM_ROUTE_FIRMWARE   /* the ROM is not G12 1.00G: Canon's own develop task runs (film_reloc.c) */
};
int film_route_reason(void);
/* Shooting mode with My Colors Off: the live view can take a new colour
 * table now (film_hw_reload_preview). */
int film_preview_reloadable(void);
#endif
