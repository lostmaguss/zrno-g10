/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Freestanding stdlib.h for the WebAssembly build: malloc and free come from
 * src/lib.rs (Rust's allocator). film_print.c takes its print workspace
 * from the heap. */
#ifndef ZRNO_SHIM_STDLIB_H
#define ZRNO_SHIM_STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void free(void *p);
#endif
