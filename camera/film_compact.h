/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. FILM_COMPACT marks code that runs once per shot or per
 * glow pixel, not per pixel, inside files built as ARM for their hot loops:
 * on the camera it is compiled as Thumb, which takes much less core space
 * (the core must leave CHDK's ARAM heap room; see tools/core_room.py).
 * Such functions cannot use the ARM-only always_inline helpers. */
#ifndef FILM_COMPACT_H
#define FILM_COMPACT_H
#if defined(__arm__)
#define FILM_COMPACT __attribute__((target("thumb"),noinline))
#else
#define FILM_COMPACT __attribute__((noinline))
#endif
#endif
