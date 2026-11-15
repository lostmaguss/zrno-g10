/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Camera clock for the date stamp. */
#ifndef FILM_CLOCK_H
#define FILM_CLOCK_H
#include "time.h"
static inline void film_clock_date(int *year,int *month,int *day) {
    struct tm *t=get_localtime();
    *year=t->tm_year+1900;*month=t->tm_mon+1;*day=t->tm_mday;
}
#endif
