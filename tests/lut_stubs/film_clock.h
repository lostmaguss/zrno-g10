/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host stub: a fixed camera date for the date stamp tests. */
#ifndef FILM_CLOCK_H
#define FILM_CLOCK_H
static int stub_year=2026,stub_month=11,stub_day=5;
static inline void film_clock_date(int *year,int *month,int *day){*year=stub_year;*month=stub_month;*day=stub_day;}
#endif
