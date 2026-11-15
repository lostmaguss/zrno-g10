/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. G12 experimental native development route. */
#ifndef FILM_RAW_H
#define FILM_RAW_H
int film_raw_init(void);
void film_raw_latch(int eligible);
void film_raw_process(char *raw);
void film_raw_opened(const char *name,int fd);
void film_raw_written(int fd,int expected,int actual);
void film_raw_closed(int fd,int result);
void film_raw_poll(void);
/* Waits for a background sensor backup (BGBACKUP.TXT): 0 or its error. */
int film_raw_backup_wait(void);
/* Re-develop: grade the next capture (CHDK RAW develop of src) without a
 * new backup; 0 cancels. Exported to modules. */
void film_raw_redevelop(const char *src);
#endif
