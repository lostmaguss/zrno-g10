/* SPDX-License-Identifier: GPL-2.0-or-later */
/* One ZrnoBoy live-view frame at fixed addresses, for tests/bench_gboy_arm.py:
 * viewport 0x100000, overlay 0x300000, canvas and buffers from 0x380000. */
#include "film_gboy.h"
/* One live frame at fixed addresses: viewport 0x100000, overlay 0x300000, buffers 0x380000. */
int live_frame(int frame, int res) {
    unsigned char *view=(unsigned char*)0x100000,*ov=(unsigned char*)0x300000,*cv=(unsigned char*)0x380000,*luma=cv+80000,*work=luma+80000;
    static const unsigned char idx[4]={1,2,3,4};
    film_gboy_settings s={0,(unsigned char)frame,7,3,2,1,(unsigned char)res};
    film_gboy_geom g; film_gboy_levels lv;
    film_gboy_geometry(frame,res,&g);
    film_gboy_sample_live(view,720,480,1080,g.ww,g.wh,luma);
    film_gboy_levels_measure(luma,g.ww*g.wh,&lv);
    film_gboy_picture(luma,work,&s,&g,&lv,5,cv);
    film_gboy_live_paint(cv,&g,idx,ov,960,720,240);
    return 0;
}
void *memcpy(void *d,const void *s,unsigned n){char*a=d;const char*b=s;while(n--)*a++=*b++;return d;}
void *memset(void *d,int c,unsigned n){char*a=d;while(n--)*a++=c;return d;}
