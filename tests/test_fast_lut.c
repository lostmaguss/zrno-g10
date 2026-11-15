/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../camera/film_colour.c"
#include "../camera/film_retro.c"
#include "../camera/film_toy.c"
int main(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 unsigned char *lut=malloc(size);assert(fread(lut,1,size,f)==(size_t)size);fclose(f);assert(!film_lut_validate(lut,size));
 unsigned char *prep=malloc(size);memcpy(prep,lut,size);assert(!film_lut_prepare(prep,size));
 int mono=0;unsigned n=prepared_lattice(prep,&mono);assert(n==le16(lut+8));
 const node_t *nodes=(const node_t*)(prep+16);unsigned short axis[256];colour_tables tables;
 prepare_colour_tables(n,axis,0);prepare_colour_tables(n,axis,&tables);
 for(unsigned u=0;u<256;u++)for(unsigned v=0;v<256;v++) {
  unsigned char pair[4]={u,0,v,0};
  int r,g,b,expected_r,expected_g,expected_b;
  decode_chroma(pair,0,&expected_r,&expected_g,&expected_b);
  decode_chroma(pair,&tables,&r,&g,&b);
  assert(r==expected_r&&g==expected_g&&b==expected_b);
 }
 for(unsigned r=0;r<256;r++)for(unsigned g=0;g<256;g++)for(unsigned b=0;b<256;b++) {
  unsigned char in[3]={r,g,b},old[3],out[3],prepared[3],reference[3];film_lut_pixel(lut,in,old);
  film_lut_pixel(prep,in,reference);
  fast_pixel(nodes,n,axis,0,in,out,mono);
  fast_pixel(nodes,n,axis,&tables,in,prepared,mono);
  /* Monochrome LUTs compute one channel and copy it to the other two. */
  if(memcmp(old,out,3)||memcmp(old,prepared,3)||memcmp(old,reference,3)){fprintf(stderr,"Mismatch %u,%u,%u\n",r,g,b);return 1;}
 }
 printf("Exact tetrahedral LUT size %u: all 16777216 RGB inputs match original bytes with both table paths%s\n",n,mono?" (monochrome path)":"");free(lut);free(prep);
}
