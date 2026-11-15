/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Re-develop module's NATIVE.TXT lookup: the photo's own backup, the last
 * one when a photo was re-developed, nothing for other folders, numbers,
 * incomplete shots or malformed lines; a long log read in pieces. */
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define FILM_REDEV_HOST
#include "../camera/film_redev.c"
int main(int argc,char **argv) {
 char out[24];
 assert(argc==2&&!chdir(argv[1]));mkdir("A",0700);mkdir("A/CHDK",0700);mkdir("A/CHDK/FILM",0700);
 const char *ok="id=7 stage=jpeg_saved rc=0 raw=A/FGRAW/R0000007.RAW jpg=A/DCIM/117___01/IMG_0512.JPG backup_ms=3 total_ms=9";
 assert(film_redev_match(ok,117,512,out)&&!strcmp(out,"A/FGRAW/R0000007.RAW"));
 assert(!film_redev_match(ok,118,512,out)&&!film_redev_match(ok,117,513,out)&&!film_redev_match(ok,117,51,out));
 assert(!film_redev_match("id=7 stage=begin rc=0 raw=A/FGRAW/R0000007.RAW jpg= backup_ms=0",117,512,out));
 assert(!film_redev_match("id=7 stage=jpeg_saved rc=0 raw=A/FGRAW/R7.RAW jpg=A/DCIM/117___01/IMG_0512.JPG",117,512,out));
 assert(!film_redev_match("id=7 raw=A/FGRAW/R0000007.RAW jpg=A/DCIM/117___01/IMG_0512.JPGX",117,512,out));
 FILE *f=fopen("A/CHDK/FILM/NATIVE.TXT","w");assert(f);
 for(int i=1;i<=400;i++)fprintf(f,"id=%d stage=jpeg_saved rc=0 raw=A/FGRAW/R%07d.RAW jpg=A/DCIM/117___01/IMG_%04d.JPG backup_ms=1 total_ms=2\n",i,i,i+100);
 fprintf(f,"%0300d\n",0);                                            /* an over-long line is skipped */
 fprintf(f,"id=0 stage=redevelop rc=0 raw=A/FGRAW/R0000050.RAW jpg= backup_ms=0 total_ms=0\n");
 fprintf(f,"id=0 stage=jpeg_saved rc=0 raw=A/FGRAW/R0000050.RAW jpg=A/DCIM/117___01/IMG_0601.JPG backup_ms=0 total_ms=40000\n");
 fclose(f);
 assert(film_redev_find(117,101,out)&&!strcmp(out,"A/FGRAW/R0000001.RAW"));
 assert(film_redev_find(117,500,out)&&!strcmp(out,"A/FGRAW/R0000400.RAW"));
 assert(film_redev_find(117,601,out)&&!strcmp(out,"A/FGRAW/R0000050.RAW"));   /* a re-developed photo points at its source */
 assert(!film_redev_find(117,99,out)&&!film_redev_find(116,101,out));
 unlink("A/CHDK/FILM/NATIVE.TXT");assert(!film_redev_find(117,101,out));
 puts("Re-develop lookup: photo-to-backup match, folder/number/incomplete/malformed rejection, re-developed photos, long log in pieces, missing log passed");
 return 0;
}
