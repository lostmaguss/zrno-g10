/* SPDX-License-Identifier: GPL-2.0-or-later */
#define FONT_HEIGHT 16
#define COLOR_BLACK 0
#define COLOR_WHITE 1
#define MAKE_COLOR(a,b) (((a)<<8)|(b))
#define RECT_BORDER0 0
#define RECT_BORDER1 1
#define DRAW_FILLED 8
void draw_string(int,int,const char*,int);
void draw_rectangle(int,int,int,int,int,int);
int user_color(int);
#define COLOR_TRANSPARENT 0x10
#define COLOR_RED 0x11
/* The film theme colours (CHDK slot colours on the camera). */
#define FILM_BG 0x20
#define FILM_SURFACE 0x21
#define FILM_ACCENT 0x22
#define FILM_MUTED 0x23
#define COLOR_GREY 0x12
#define COLOR_GREY_DK 0x13
#define COLOR_GREY_LT 0x14
#define FILM_ICON_ACCENT 0x24
void draw_pixel(int x,int y,int c);
