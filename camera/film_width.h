/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The widest frame the effect kernels accept: the G12's 3744, or the G10's
 * L frame, 4416 (MAGUS_G10, docs/PORT-G10.md). Workspaces grow with it. */
#ifndef FILM_WIDTH_H
#define FILM_WIDTH_H
#ifndef FILM_MAX_WIDTH
#ifdef MAGUS_G10
#define FILM_MAX_WIDTH 4416u
#else
#define FILM_MAX_WIDTH 3744u
#endif
#endif
#endif
