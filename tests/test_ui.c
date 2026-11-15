/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual core UI with mocked drawing/time/power-policy callbacks. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../camera/film_ui.c"
static int now,power_updates,power_busy,restore_count;
static char drawn[2048];
static int last_y,last_colour,route=FILM_ROUTE_READY;
film_test_conf conf={0x1234};
int user_color(int c){return c^0x5a5a;}
int film_route_reason(void){return route;}
/* ZrnoBoy (1) and Print (2): off unless a test turns one on; the live
 * screen, when drawn, stands in for the indicator. */
static int gboy_engine,gboy_live,gboy_live_calls;
int film_gboy_engine(void){return gboy_engine==1;}
int film_engine_alt(void){return gboy_engine!=0;}
int film_grade_engine(void){return gboy_engine==3;}
const char *film_grui_slot_name(void){return "Grade 2";}
const char *film_gbui_engine_name(void){static const char *n[]={"Film","ZrnoBoy","Print","Grade"};return n[gboy_engine];}
const char *film_gbui_stage_text(void){return gboy_engine==2?"Printing in 2 inks":"Dithering in 4 shades";}
int film_gbui_live_draw(void (*card)(void)){assert(!card);gboy_live_calls++;return gboy_live;}
int film_gbui_live_shown(void){return gboy_live;}
int film_preview_showing(void){return 0;}
static int led_state[8]={-1,-1,-1,-1,-1,-1,-1,-1},led_calls;
void camera_set_led(int led,int state,int bright){(void)bright;assert(led==1||led==2);led_state[led]=state;led_calls++;}
static int rectangles,fills,last_fill,bar_frames;
/* Every overlay frame is composed: begin and end in pairs, all fills inside. */
static int in_frame,frames,frame_box[4];
int film_draw_begin(int x1,int y1,int x2,int y2){assert(!in_frame&&x1<=x2&&y1<=y2);in_frame=1;frames++;frame_box[0]=x1;frame_box[1]=y1;frame_box[2]=x2;frame_box[3]=y2;return 1;}
void film_draw_end(void){assert(in_frame);in_frame=0;}
static int clears;
int film_draw_clear(int c){assert(in_frame&&c==FILM_BG);clears++;
 assert(frame_box[0]==0&&frame_box[1]==0&&frame_box[2]==359&&frame_box[3]==239);return 1;} /* the panel covers the screen */
static int facing;
int film_screen_facing_lens(void){return facing;}
int get_tick_count(void){return now;}
void conf_update_prevent_shutdown(void){power_updates++;power_busy=film_ui_busy();}
void gui_set_need_restore(void){restore_count++;}
void draw_string(int x,int y,const char *s,int c){(void)x;last_colour=c;last_y=y;strcat(drawn,s);strcat(drawn,"\n");}
void draw_rectangle(int x1,int y1,int x2,int y2,int c,int flags){
 assert(x1>=0&&y1>=0&&x2>=x1&&y2>=y1&&x2<360&&y2<240);rectangles++;
 (void)c;(void)flags;
}
/* Themed drawing: rounded fills inside the screen; the progress bar is the
 * accent fill on the bar row. Text goes to the same record as draw_string. */
void film_menu_fill(int x1,int y1,int x2,int y2,int c,int out,int r){
 assert(x1>=0&&y1>=0&&x2>=x1&&y2>=y1&&x2<360&&y2<240&&(r==0||r==2||r==4||r==6));(void)out;fills++;
 assert(in_frame&&x1>=frame_box[0]&&y1>=frame_box[1]&&x2<=frame_box[2]&&y2<=frame_box[3]);
 if(y1==146&&c==FILM_ACCENT){last_fill=x2-x1+1;bar_frames++;}
}
static int font_swaps;
int rbf_film_font(const char *f,int on){assert(!on||!strcmp(f,"A/CHDK/FILM/MENU.RBF"));font_swaps+=on?1:-1;return on?19:0;}
int rbf_draw_string(int x,int y,const char *s,int c){assert(font_swaps==1);(void)x;last_colour=c;last_y=y;strcat(drawn,s);strcat(drawn,"\n");return 0;}
int rbf_str_width(const char *s){return 9*(int)strlen(s);}
static int pixels,accent_pixels,max_x,min_y,max_y;
void draw_pixel(int x,int y,int c){assert(x>=0&&x<360&&y>=0&&y<240);pixels++;if(c==FILM_ICON_ACCENT)accent_pixels++;if(x>max_x)max_x=x;if(y<min_y)min_y=y;if(y>max_y)max_y=y;}
static const char *render(void){drawn[0]=0;film_ui_draw();assert(!in_frame);return drawn;}
int main(void) {
 camera_info.state.gui_mode_none=1;
 assert(!film_ui_busy()&&!strlen(render()));
 film_ui_raw_begin();film_ui_tick();assert(power_busy&&power_updates==1);
 now=70000;assert(strstr(render(),"Saving sensor backup"));assert(strstr(drawn,"Stage 1 of 4")&&strstr(drawn,": 0%"));
 camera_info.state.gui_mode_none=0;assert(!strlen(render())&&!film_ui_themed());camera_info.state.gui_mode_none=1;
 assert(strstr(render(),"Saving sensor backup"));assert(!strstr(drawn,"cancels"));
 /* One bar for the whole develop. Weights: backup 3, develop 2, LUT 10,
  * soft 4, bloom 5, grain 4, fused 7, JPEG 3 (here 18 in all). */
 film_ui_set_look("Portra 400");film_ui_progress(7817472,15634944);
 assert(strstr(render(),"Portra 400: 8%")&&strstr(drawn,"Stage 1 of 4"));     /* 3*0.5/18 */
 film_ui_raw_stage(3);now+=800;                                                  /* Canon's develop creeps: half its slice at 800 ms */
 assert(strstr(render(),"DIGIC developing image")&&strstr(drawn,": 22%")&&strstr(drawn,"Stage 2 of 4"));
 film_ui_raw_stage(4);film_ui_progress(1368,2736);
 assert(strstr(render(),"Portra 400: 55%"));assert(strstr(drawn,"Applying film 3D LUT"));
 assert(strstr(drawn,"Developing film")&&strstr(drawn,"Stage 3 of 4"));assert(fills>0&&last_fill==168&&!font_swaps);
 assert(film_ui_themed()&&clears>0);
 film_ui_progress(0,2736);render();assert(last_fill==168);                     /* never steps back */
 film_ui_progress(2736,2736);assert(strstr(render(),": 83%"));assert(last_fill==253);
 film_ui_progress(9999,2736);render();assert(last_fill==253); /* clamps invalid progress */
 film_ui_progress(-3,2736);render();assert(last_fill==253);
 film_ui_progress(1368,2736);
 film_ui_set_halation(1);assert(strstr(render(),"LUT + halation Low"));
 film_ui_set_halation(2);assert(strstr(render(),"LUT + halation Mid"));
 film_ui_set_halation(3);assert(strstr(render(),"LUT + halation High"));film_ui_set_halation(0);
 film_ui_set_soft(2);film_ui_raw_stage(FILM_UI_STAGE_SOFT);assert(strstr(render(),"Soft focus Mid")&&strstr(drawn,"Stage 4 of 5"));film_ui_set_soft(0);
 film_ui_raw_stage(5);
 film_ui_set_grain(1);assert(strstr(render(),"Adding grain Low"));
 film_ui_set_grain(2);assert(strstr(render(),"Adding grain Mid"));
 film_ui_set_grain(3);assert(strstr(render(),"Adding grain High"));film_ui_set_grain(0);
 film_ui_set_grain(3);assert(strstr(render(),"Stage 4 of 5"));
 /* After the grade, stage 3 is the JPEG encode: the last stage, which creeps
  * toward the end and leaves 100% to the SAVED card. */
 film_ui_raw_stage(3);assert(strstr(render(),"Saving JPEG"));assert(strstr(drawn,"Stage 5 of 5"));
 now+=100000;assert(strstr(render(),": 99%")&&last_fill==303);now-=100000;
 film_ui_set_bloom(1);film_ui_raw_stage(6);assert(strstr(render(),"Adding bloom Low")&&strstr(drawn,"Stage 4 of 6"));
 film_ui_set_bloom(2);assert(strstr(render(),"Adding bloom Mid"));film_ui_set_bloom(3);assert(strstr(render(),"Adding bloom High"));
 film_ui_raw_stage(5);assert(strstr(render(),"Stage 5 of 6"));film_ui_raw_stage(3);assert(strstr(render(),"Stage 6 of 6"));
 film_ui_raw_stage(FILM_UI_STAGE_BLOOM_GRAIN);
 assert(strstr(render(),"Adding bloom + grain")&&strstr(drawn,"Stage 4 of 5"));
 film_ui_raw_stage(3);assert(strstr(render(),"Stage 5 of 5"));
 /* With FUSEFX.TXT the merged pass is one stage from the start. */
 film_ui_set_fused(1);film_ui_raw_begin();film_ui_raw_stage(3);assert(strstr(render(),"Stage 1 of 4"));
 film_ui_raw_stage(4);assert(strstr(render(),"Stage 2 of 4"));film_ui_raw_stage(FILM_UI_STAGE_BLOOM_GRAIN);assert(strstr(render(),"Stage 3 of 4"));
 film_ui_set_fused(0);
 film_ui_set_bloom(0);film_ui_set_grain(0);
 /* A new shot starts from zero; a background backup is not a stage here. */
 film_ui_raw_begin();film_ui_raw_stage(3);assert(strstr(render(),": 0%")&&strstr(drawn,"Stage 1 of 3")&&strstr(drawn,"DIGIC developing image"));
 film_ui_raw_stage(4);assert(strstr(render(),"Stage 2 of 3"));
 film_ui_raw_filename("A/DCIM/117___01/IMG_0512.JPG");film_ui_result(FILM_UI_SAVED,0);film_ui_tick();
 assert(strstr(render(),"Film 0512 saved in DCIM")&&film_ui_themed());assert(!power_busy&&power_updates==2);
 now+=499;assert(strstr(render(),"saved"));now++;film_ui_tick();assert(!strlen(render())&&!film_ui_themed()); /* SAVED clears after 500 ms */
 film_ui_raw_begin();film_ui_result(FILM_UI_FAILED,-4);film_ui_tick();
 assert(strstr(render(),"failed (code -4)"));assert(strstr(drawn,"See NATIVE.TXT"));assert(restore_count>0);
 /* FAILED stays for 20 s. */
 now+=19998;film_ui_tick();assert(strstr(render(),"failed"));now+=2;film_ui_tick();assert(!strlen(render()));
 /* Busy LEDs: the upper orange and lower yellow indicators alternate while a
  * shot processes, swap at most every 400 ms, and both end off. */
 led_calls=0;film_ui_set_leds(1);film_ui_raw_begin();film_ui_tick();
 assert(led_calls==2&&led_state[1]!=led_state[2]&&(led_state[1]==1||led_state[2]==1));
 int first=led_state[1];now+=399;film_ui_tick();assert(led_calls==2);
 now+=1;film_ui_tick();assert(led_calls==4&&led_state[1]==!first&&led_state[2]==first);
 now+=400;film_ui_tick();assert(led_calls==6&&led_state[1]==first);
 film_ui_result(FILM_UI_SAVED,0);film_ui_tick();assert(led_calls==8&&led_state[1]==0&&led_state[2]==0);
 film_ui_tick();now+=1000;film_ui_tick();assert(led_calls==8); /* nothing more while idle */
 /* Turned off mid-shot: the LEDs go dark at once; with the setting off they stay untouched. */
 film_ui_raw_begin();film_ui_tick();assert(led_calls==10);film_ui_set_leds(0);film_ui_tick();
 assert(led_calls==12&&led_state[1]==0&&led_state[2]==0);
 now+=2000;film_ui_tick();assert(led_calls==12);film_ui_result(FILM_UI_FAILED,-1);film_ui_tick();assert(led_calls==12);
 film_ui_set_leds(1);now+=30000;film_ui_tick();
 assert(frames>20);
 /* Swinging the screen to face the lens, and back, asks for a full redraw once each way. */
 {int r=restore_count;film_ui_tick();assert(restore_count==r);facing=1;film_ui_tick();assert(restore_count==r+1);
  film_ui_tick();assert(restore_count==r+1);facing=0;film_ui_tick();assert(restore_count==r+2);}
 /* Shooting-screen indicator. */
 now+=100000;assert(!film_ui_busy());
 /* The card: name text, then icons as letter+level pairs in osd_code. */
 #define OSD() (drawn[0]=0,osd_code[0]=0,pixels=accent_pixels=max_x=0,min_y=240,max_y=0,film_ui_draw_osd(),assert(!in_frame),drawn)
 film_ui_set_look("Portra 400");film_ui_set_halation(3);film_ui_set_bloom(2);film_ui_set_grain(0);
 film_ui_set_processing(0);assert(!strlen(OSD())&&!film_ui_indicator_shown());   /* Off: nothing shown */
 film_ui_set_processing(1);film_ui_set_indicator(FILM_UI_INDICATOR_BOTTOM);
 assert(!strcmp(OSD(),"Portra 400\n")&&last_y==188&&!strcmp(osd_code,"H3B2"));
 assert(last_colour==((COLOR_BLACK<<8)|COLOR_WHITE)&&pixels>40&&accent_pixels>0&&min_y>=188&&max_y<=188+19);
 assert(film_ui_indicator_shown());now+=999;assert(film_ui_indicator_shown());now+=1;assert(!film_ui_indicator_shown()); /* orange slot held while drawn */
 film_ui_set_indicator(FILM_UI_INDICATOR_TOP);OSD();assert(last_y==32&&max_y<=32+19);
 film_ui_set_indicator(FILM_UI_INDICATOR_OFF);assert(!strlen(OSD()));
 film_ui_set_indicator(7);OSD();assert(last_y==188);           /* invalid falls back to Bottom */
 route=FILM_ROUTE_MODE;assert(strstr(OSD(),"paused: scene mode")&&!osd_code[0]&&!accent_pixels);
 route=FILM_ROUTE_DRIVE;assert(strstr(OSD(),"paused: drive mode"));
 route=FILM_ROUTE_ASPECT;assert(strstr(OSD(),"paused: not 4:3"));
 route=FILM_ROUTE_SIZE;assert(strstr(OSD(),"paused: image size"));
 route=FILM_ROUTE_COLORS;assert(strstr(OSD(),"paused: My Colors"));
 route=FILM_ROUTE_FIRMWARE;assert(strstr(OSD(),"paused: unknown firmware"));
 route=FILM_ROUTE_NOT_RECORDING;assert(!strlen(OSD()));        /* playback: nothing */
 route=FILM_ROUTE_READY;
 film_ui_set_vignette(2);film_ui_set_leaks(1);OSD();assert(!strcmp(osd_code,"H3B2V2L1"));
 film_ui_set_toy(3);OSD();assert(!strcmp(osd_code,"H3B2V2L1T3"));film_ui_set_toy(0);
 film_ui_set_soft(2);OSD();assert(!strcmp(osd_code,"H3B2F2V2L1"));film_ui_set_soft(0);
 film_ui_set_print(2,1);OSD();assert(!strcmp(osd_code,"H3B2V2L1D2S1"));film_ui_set_print(0,0);
 film_ui_set_halation(0);film_ui_set_bloom(0);film_ui_set_vignette(0);film_ui_set_leaks(0);OSD();assert(!osd_code[0]&&pixels==0);film_ui_set_halation(3);film_ui_set_bloom(2);
 film_ui_set_vignette(9);film_ui_set_leaks(-1);film_ui_set_print(-1,0);OSD();assert(!strcmp(osd_code,"H3B2")); /* invalid levels show Off */
 film_ui_set_look("Teal & Orange");film_ui_set_halation(3);film_ui_set_bloom(3);film_ui_set_grain(3);film_ui_set_vignette(3);film_ui_set_leaks(3);film_ui_set_toy(3);film_ui_set_print(3,1);film_ui_set_soft(3);
 OSD();assert(strlen(osd_code)==18&&max_x<=353);                  /* every effect: nine icons, on screen */
 film_ui_set_look("A twenty-char label!");OSD();assert(max_x<=353);film_ui_set_print(0,0);film_ui_set_soft(0);   /* longest look stays on screen */
 /* The saved look's name in a tab above the card: small letters over the
  * LUT's name; the card stays put at the bottom and moves down by the tab
  * at the top; the tab stays narrower than the card, shortening the name
  * with ".." (left out below three letters); icons are never given up. */
 film_ui_set_look("Portra 400");film_ui_set_print(0,0);film_ui_set_soft(0);film_ui_set_toy(0);film_ui_set_leaks(0);film_ui_set_vignette(0);
 OSD();assert(last_y==188&&min_y>=188&&!osd_preset[0]);
 {int plain=pixels;film_ui_set_preset("Look 2");OSD();
  assert(!strcmp(osd_preset,"Look 2")&&pixels>plain&&!strcmp(drawn,"Portra 400\n")&&!strcmp(osd_code,"H3B3G3"));
  assert(last_y==188&&min_y>=186-11&&min_y<186&&max_y<=188+19);}           /* tab rows 175-185 above the card */
 film_ui_set_indicator(FILM_UI_INDICATOR_TOP);OSD();assert(last_y==32+11&&min_y>=30);   /* the card moves down by the tab */
 film_ui_set_indicator(FILM_UI_INDICATOR_BOTTOM);
 film_ui_set_preset("A twenty-char label!");OSD();assert(!strcmp(osd_preset,"A twenty-char label!")&&max_x<=174);
 film_ui_set_halation(0);film_ui_set_bloom(0);film_ui_set_grain(0);OSD();                  /* a card no wider than the name */
 assert(strlen(osd_preset)>=5&&strlen(osd_preset)<20&&!strcmp(osd_preset+strlen(osd_preset)-2,"..")&&max_x<=120);
 film_ui_set_look("Teal & Orange");film_ui_set_halation(3);film_ui_set_bloom(3);film_ui_set_grain(3);film_ui_set_vignette(3);film_ui_set_leaks(3);film_ui_set_toy(3);film_ui_set_print(3,1);film_ui_set_soft(3);
 OSD();assert(strlen(osd_code)==18&&max_x<=353&&!strcmp(osd_preset,"A twenty-char label!"));
 film_ui_set_look("A twenty-char label!");OSD();assert(max_x<=353&&osd_preset[0]);
 route=FILM_ROUTE_ASPECT;film_ui_set_look("Portra 400");OSD();assert(strstr(drawn,"paused: not 4:3")&&!strcmp(osd_preset,"A twenty-char label!")&&max_x<=353);route=FILM_ROUTE_READY;
 film_ui_set_preset("Hi");OSD();assert(!strcmp(osd_preset,"Hi"));               /* short names that fit show */
 film_ui_set_preset("");OSD();assert(!osd_preset[0]&&min_y>=188);film_ui_set_preset(0);OSD();assert(!osd_preset[0]);
 film_ui_set_print(0,0);film_ui_set_soft(0);film_ui_set_toy(0);film_ui_set_leaks(0);film_ui_set_vignette(0);
 film_ui_raw_begin();assert(!strlen(OSD()));                   /* the processing panel owns the screen */
 film_ui_result(FILM_UI_SAVED,0);assert(!strlen(OSD()));       /* and the SAVED line for 500 ms */
 now+=501;assert(strlen(OSD()));
 /* ZrnoBoy: the indicator names the engine, with none of the film icons,
  * and the saved look's tab as in every engine; while its live screen
  * shows, the indicator stays off. */
 now+=30000;gboy_engine=1;film_ui_set_preset("Look 2");film_ui_set_halation(3);film_ui_set_bloom(2);film_ui_set_grain(3);
 assert(!strcmp(OSD(),"ZrnoBoy\n")&&!osd_code[0]&&!strcmp(osd_preset,"Look 2"));
 gboy_live=1;assert(!strlen(OSD()));
 {int calls=gboy_live_calls;render();assert(gboy_live_calls==calls+1);}  /* drawn on every GUI pass */
 /* A ZrnoBoy shot: its own stage label, the engine's name, and the film
  * effects left out of the stage count. */
 film_ui_raw_begin();film_ui_raw_stage(FILM_UI_STAGE_GBOY);
 assert(strstr(render(),"ZrnoBoy: ")&&strstr(drawn,"Dithering in 4 shades")&&strstr(drawn,"Stage 2 of 3"));
 {int calls=gboy_live_calls;render();assert(gboy_live_calls==calls);}    /* not while developing */
 film_ui_result(FILM_UI_SAVED,0);now+=501;film_ui_tick();
 /* Print: the same, with its own name and stage label. */
 now+=30000;gboy_live=0;gboy_engine=2;
 assert(!strcmp(OSD(),"Print\n")&&!osd_code[0]&&!strcmp(osd_preset,"Look 2"));
 film_ui_set_preset("");assert(!strcmp(OSD(),"Print\n")&&!osd_preset[0]);
 film_ui_raw_begin();film_ui_raw_stage(FILM_UI_STAGE_GBOY);
 assert(strstr(render(),"Print: ")&&strstr(drawn,"Printing in 2 inks")&&strstr(drawn,"Stage 2 of 3"));
 film_ui_result(FILM_UI_SAVED,0);now+=501;film_ui_tick();
 gboy_live=0;gboy_engine=0;film_ui_set_preset(0);film_ui_set_grain(0);
 puts("UI: themed animated film panel (menu font, theme palette while shown), bounded rectangle geometry, one overall bar (weighted stages, monotonic, never 100% before SAVED), stage counts, stage labels, persistence/expiry, menu hiding, power policy and the icon indicator (levels, paused reasons, placement, orange slot hold, the look's tab in every engine) passed");
}
