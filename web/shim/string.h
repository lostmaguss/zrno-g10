/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Freestanding string.h for the WebAssembly build: Rust's runtime provides
 * these symbols. */
#ifndef ZRNO_SHIM_STRING_H
#define ZRNO_SHIM_STRING_H
#include <stddef.h>
void *memcpy(void *dst,const void *src,size_t n);
void *memmove(void *dst,const void *src,size_t n);
void *memset(void *dst,int c,size_t n);
int memcmp(const void *a,const void *b,size_t n);
size_t strlen(const char *s);
#endif
