/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host run of the G10 develop-task copy (film_reloc.c with MAGUS_G10) on a
 * ROM dump: prints the result and hash, writes the copy and its map for
 * tools/g10_reloc_check.py. Usage: g10_reloc_host ROM OUT.bin MAP.bin */
#define FILM_RELOC_HOST 1
#define MAGUS_G10 1
#include <stdio.h>
#include <stdlib.h>
#include "../camera/film_reloc.c"

#define ROM_BASE 0xff810000u
static unsigned char *rom;
static long rom_size;

static unsigned rom_word(unsigned a)
{
    if (a < ROM_BASE || a - ROM_BASE + 4 > (unsigned long)rom_size) { fprintf(stderr, "ROM read out of range %08x\n", a); exit(2); }
    const unsigned char *p = rom + (a - ROM_BASE);
    return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24;
}

int main(int argc, char **argv)
{
    static unsigned out[FILM_RELOC_WORDS];
    static unsigned short map[N_INS];
    unsigned h = 0;
    FILE *f;
    int n;
    if (argc != 4) return 1;
    f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END); rom_size = ftell(f); fseek(f, 0, SEEK_SET);
    rom = malloc(rom_size);
    if (fread(rom, 1, rom_size, f) != (size_t)rom_size) return 1;
    fclose(f);
    n = film_reloc_build(rom_word, out, 0x00100000u, 0x00200001u, 0x00300001u, &h, map);
    printf("result %d words, hash 0x%08x, %d instructions\n", n, h, (int)N_INS);
    if (n <= 0) return 3;
    f = fopen(argv[2], "wb"); fwrite(out, 4, n, f); fclose(f);
    f = fopen(argv[3], "wb"); fwrite(map, 2, N_INS, f); fclose(f);
    return 0;
}
