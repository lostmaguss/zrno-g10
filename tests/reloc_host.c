// SPDX-License-Identifier: GPL-2.0-or-later
/* Runs film_reloc_build on the host against a ROM image loaded at
 * 0xff810000, for tests/test_reloc.py and tools/check_reloc.py.
 * Usage: reloc_host ROM.BIN OUT.BIN OUT_ADDR TRACE_FN OBSERVE_FN
 * OUT.BIN: int result, unsigned hash, the map (u16 per instruction, padded
 * to a word), then the copy's words. */
#include <stdio.h>
#include <stdlib.h>
#include "film_reloc.h"

#define ROM_BASE 0xff810000u
static unsigned char *rom;
static unsigned long rom_len;

static unsigned rom_word(unsigned a)
{
    unsigned long o = a - ROM_BASE;
    if (a < ROM_BASE || o + 4 > rom_len) return 0;
    return rom[o] | rom[o + 1] << 8 | rom[o + 2] << 16 | (unsigned)rom[o + 3] << 24;
}

int main(int argc, char **argv)
{
    static unsigned out[FILM_RELOC_WORDS];
    static unsigned short map[FILM_RELOC_WORDS];
    unsigned hash = 0;
    int n, nmap = (int)((0xff8e73ccu - 0xff8e6f3cu + 0xff8e60d4u - 0xff8e5ec4u) >> 2);
    FILE *f;
    if (argc != 6) { fprintf(stderr, "usage: reloc_host ROM.BIN OUT.BIN OUT_ADDR TRACE_FN OBSERVE_FN\n"); return 2; }
    if (!(f = fopen(argv[1], "rb"))) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END); rom_len = (unsigned long)ftell(f); fseek(f, 0, SEEK_SET);
    rom = malloc(rom_len);
    if (!rom || fread(rom, 1, rom_len, f) != rom_len) { perror(argv[1]); return 2; }
    fclose(f);
    n = film_reloc_build(rom_word, out, (unsigned)strtoul(argv[3], 0, 0),
                         (unsigned)strtoul(argv[4], 0, 0), (unsigned)strtoul(argv[5], 0, 0), &hash, map);
    if (!(f = fopen(argv[2], "wb"))) { perror(argv[2]); return 2; }
    fwrite(&n, 4, 1, f); fwrite(&hash, 4, 1, f);
    fwrite(map, 2, (size_t)(nmap + (nmap & 1)), f);
    if (n > 0) fwrite(out, 4, (size_t)n, f);
    fclose(f);
    return 0;
}
