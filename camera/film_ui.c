/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Capture publishes state; SpyTask owns all drawing. */
#include "camera_info.h"
#include "clock.h"
#include "conf.h"
#include "gui.h"
#include "gui_draw.h"
#include "font.h"
#include "film_ui.h"
#include "film_menu.h"
#include "film_queue.h"
#include "film_gbui.h"
#include "film_grui.h"
#include "debug_led.h"
static volatile int active,shot,percent,start_tick;
static volatile int result_code,result_shot,result_detail,result_tick;
static volatile int native_stage;
static volatile int fused_pass;
static volatile int gboy_shot;          /* this shot is a ZrnoBoy or Print one: no film effects */
static char look_name[21]="Warm 200";
static int look_halation;
static int look_grain,look_bloom;
static int processing_on,indicator=FILM_UI_INDICATOR_BOTTOM,look_vignette,look_leaks;
void film_ui_set_vignette(int level){look_vignette=level>=0&&level<=3?level:0;}
void film_ui_set_leaks(int level){look_leaks=level>=0&&level<=3?level:0;}
static int look_toy;
void film_ui_set_toy(int level){look_toy=level>=0&&level<=3?level:0;}
static int look_dust,look_stamp,look_soft;
void film_ui_set_soft(int level){look_soft=level>=0&&level<=3?level:0;}
void film_ui_set_print(int dust,int stamp){look_dust=dust>=0&&dust<=3?dust:0;look_stamp=stamp!=0;}
void film_ui_set_processing(int on){processing_on=on!=0;}
void film_ui_set_indicator(int where){indicator=where>=FILM_UI_INDICATOR_OFF&&where<=FILM_UI_INDICATOR_TOP?where:FILM_UI_INDICATOR_BOTTOM;}
void film_ui_set_bloom(int level){look_bloom=level>=0&&level<=3?level:0;}
void film_ui_set_grain(int level){look_grain=level>=0&&level<=3?level:0;}
void film_ui_set_halation(int enabled){look_halation=enabled>=0&&enabled<=3?enabled:0;}
void film_ui_set_look(const char *name){int n=strlen(name);if(n>20)n=20;memcpy(look_name,name,n);look_name[n]=0;}
/* The saved look the settings match ("" for none): small text after the LUT. */
static char preset_name[21];
void film_ui_set_preset(const char *name){int n=name?strlen(name):0;if(n>20)n=20;memcpy(preset_name,name,n);preset_name[n]=0;}
static void barrier(void){asm volatile("" ::: "memory");}
/* graded: the grade (stage 4) has started, so stage 3 is now Canon's JPEG
 * encode rather than its develop. backup_seen: the backup reported progress
 * here (a foreground backup or a re-develop load), so it is a stage of its
 * own; a background backup is not. run counts shots for the bar's latch. */
static volatile int graded,backup_seen,stage_tick,run;
static int plan_fused;
void film_ui_set_fused(int on){plan_fused=on!=0;}
void film_ui_raw_begin(void) {
    native_stage=1;fused_pass=0;gboy_shot=film_engine_alt();graded=0;backup_seen=0;shot=0;percent=0;
    start_tick=stage_tick=get_tick_count();run++;barrier();active=1;
}
void film_ui_raw_stage(int stage){
    if(stage==FILM_UI_STAGE_BLOOM_GRAIN)fused_pass=1;
    if(stage==FILM_UI_STAGE_GBOY)gboy_shot=1;
    if(stage!=1&&stage!=3)graded=1;
    stage_tick=get_tick_count();native_stage=stage;percent=0;
}
void film_ui_raw_filename(const char *src) {
    int i,n=0;
    if(strlen(src)!=28)return;
    for(i=20;i<24;i++){if(src[i]<'0'||src[i]>'9')return;n=n*10+src[i]-'0';}
    shot=n;
}
void film_ui_progress(int row,int total) {
    if(total>0){int p=row*100/total;percent=p<0?0:p>100?100:p;}
    if(native_stage==1&&percent>0)backup_seen=1;
}
void film_ui_result(int result,int detail) {
    result_shot=shot;result_detail=detail;result_tick=get_tick_count();
    barrier();result_code=result;barrier();active=0;
}
int film_ui_busy(void){return active;}
/* SAVED clears quickly; FAILED stays long enough to be noticed. */
#define FILM_UI_SAVED_MS 500u
#define FILM_UI_FAILED_MS 20000u
static int recent(void) {
    unsigned shown=result_code==FILM_UI_SAVED?FILM_UI_SAVED_MS:FILM_UI_FAILED_MS;
    return result_code && (unsigned)(get_tick_count()-result_tick)<shown;
}
/* Busy LEDs: while a shot processes, alternate the upper (orange) and lower
 * (yellow) indicators beside the viewfinder. Driven only from SpyTask, which
 * gets the CPU whenever processing yields (about twice a second), so the
 * pair swaps roughly every half second. Both are switched off at the end. */
#define FILM_UI_LED_UPPER_ORANGE 1
#define FILM_UI_LED_LOWER_YELLOW 2
#define FILM_UI_LED_SWAP_MS 400u
static int leds_enabled=1,leds_lit,led_phase;
static unsigned led_tick;
void film_ui_set_leds(int on){leds_enabled=on!=0;}
static void leds_update(int busy) {
    unsigned now=(unsigned)get_tick_count();
    if(busy&&leds_enabled) {
        if(!leds_lit||now-led_tick>=FILM_UI_LED_SWAP_MS) {
            led_phase=!led_phase;led_tick=now;leds_lit=1;
            camera_set_led(FILM_UI_LED_UPPER_ORANGE,led_phase,0);
            camera_set_led(FILM_UI_LED_LOWER_YELLOW,!led_phase,0);
        }
    } else if(leds_lit) {
        leds_lit=0;
        camera_set_led(FILM_UI_LED_UPPER_ORANGE,0,0);
        camera_set_led(FILM_UI_LED_LOWER_YELLOW,0,0);
    }
}
void film_ui_tick(void) {
    static int was_active,was_recent,was_facing;
    int busy=active,show=recent(),facing=film_screen_facing_lens();
    /* A swing leaves the overlays at the old columns: erase and redraw all. */
    if(facing!=was_facing){was_facing=facing;gui_set_need_restore();}
    leds_update(busy);
    if(busy!=was_active) {
        /* Restores the user's normal power policy when processing ends. */
        conf_update_prevent_shutdown();was_active=busy;gui_set_need_restore();
    }
    if(show!=was_recent){was_recent=show;gui_set_need_restore();}
}
/* Nonzero while the developing panel or its result shows: the palette
 * loader then holds the menu's theme colours (see film_menu.h). */
int film_ui_themed(void){return camera_info.state.gui_mode_none&&(active||recent());}
/* Drawn in the Zrno menu's theme: dark rounded panel, orange
 * accents, value text in its font (CHDK's 8x16 if MENU.RBF is missing). */
static void text(int x,int y,const char *s,int fg){rbf_draw_string(x,y,s,MAKE_COLOR(FILM_BG,fg));}
static const char *levels[4]={"Off","Low","Mid","High"};
/* One bar for the whole develop: 0% is the shutter, 100% the end of the last
 * stage. Each stage present for this shot owns a slice sized by its typical
 * time (weights in about half seconds, from NATIVE.TXT/STAGELOG timings);
 * a stage's own progress fills its slice. Canon's develop and JPEG encode
 * report nothing, so they creep toward their slice's end and never reach it:
 * the bar only completes when the SAVED card replaces it. */
enum{ST_BACKUP,ST_DEVELOP,ST_LUT,ST_SOFT,ST_BLOOM,ST_GRAIN,ST_FUSED,ST_JPEG,ST_COUNT};
static const unsigned char stage_weight[ST_COUNT]={3,2,10,4,5,4,7,3};
static const unsigned short stage_creep_ms[ST_COUNT]={0,800,0,0,0,0,0,1200};
static int current_stage(void) {
    switch(native_stage) {
        case 1: return ST_BACKUP;
        case 3: return graded?ST_JPEG:ST_DEVELOP;
        case FILM_UI_STAGE_SOFT: return ST_SOFT;
        case 6: return ST_BLOOM;
        case 5: return ST_GRAIN;
        case FILM_UI_STAGE_BLOOM_GRAIN: return ST_FUSED;
        case FILM_UI_STAGE_GBOY: return ST_LUT;
    }
    return ST_LUT;
}
/* Overall progress in tenths of a percent; sets the stage number and count. */
static int overall(int tick,int *number,int *count) {
    static int latched_run=-1,latched;
    int present[ST_COUNT],cur=current_stage(),i,sum=0,done=0,frac,pm;
    /* Fused when the stage said so, or planned and not already running apart. */
    int fused=fused_pass||(plan_fused&&look_bloom&&look_grain&&cur!=ST_BLOOM&&cur!=ST_GRAIN);
    present[ST_BACKUP]=backup_seen||cur==ST_BACKUP;
    present[ST_DEVELOP]=present[ST_LUT]=present[ST_JPEG]=1;
    present[ST_SOFT]=look_soft!=0&&!gboy_shot;
    present[ST_BLOOM]=look_bloom!=0&&!fused&&!gboy_shot;
    present[ST_GRAIN]=look_grain!=0&&!fused&&!gboy_shot;
    present[ST_FUSED]=fused&&!gboy_shot;
    present[cur]=1;
    *number=0;*count=0;
    for(i=0;i<ST_COUNT;i++)if(present[i]) {
        (*count)++;sum+=stage_weight[i];
        if(i<cur){done+=stage_weight[i];(*number)++;}
    }
    (*number)++;
    if(stage_creep_ms[cur]){int e=tick-stage_tick;if(e<0)e=0;frac=e*1000/(e+stage_creep_ms[cur]);}
    else frac=percent*10;
    pm=(done*1000+stage_weight[cur]*frac)/sum;
    if(pm>999)pm=999;
    /* Never step back (a fused pass shrinks the plan; a kernel may restart
     * its rows), and start from zero on every shot. */
    if(latched_run!=run){latched_run=run;latched=0;}
    if(pm<latched)pm=latched;else latched=pm;
    return pm;
}
static void native_panel(int h) {
    char msg[64];int tick=get_tick_count(),frame=(unsigned)tick/160u;
    int left=28,right=331,top=146,bottom=155;
    int number,count,pm=overall(tick,&number,&count),i,y;
    /* The whole screen, so the photo is not seen under the panel. */
    if(!film_draw_begin(0,0,359,239))return;
    if(!film_draw_clear(FILM_BG))film_menu_fill(0,0,359,239,FILM_BG,FILM_BG,0);
    film_menu_fill(left,54,left+2,52+h,FILM_ACCENT,FILM_BG,2);
    text(left+8,52,"Developing film",COLOR_WHITE);
    sprintf(msg,"%s: %d%%  %ds",film_engine_alt()?film_gbui_engine_name():look_name,pm/10,(tick-start_tick)/1000);
    text(left+8,78,msg,FILM_MUTED);
    /* Animated film strip: sprocket holes run down a rounded card and the
     * lit exposure frame steps along. Geometry only, fixed memory. */
    film_menu_fill(292,52,331,108,FILM_SURFACE,FILM_BG,4);
    for(y=55-(frame%8);y<104;y+=8)
        if(y>=55&&y+3<=104){film_menu_fill(296,y,298,y+3,FILM_MUTED,FILM_SURFACE,0);film_menu_fill(325,y,327,y+3,FILM_MUTED,FILM_SURFACE,0);}
    for(i=0;i<3;i++)film_menu_fill(303,56+i*17,320,68+i*17,(frame/2)%3==i?FILM_ACCENT:FILM_BG,FILM_SURFACE,2);
    if(native_stage==FILM_UI_STAGE_GBOY)strcpy(msg,film_gbui_stage_text());
    else if(native_stage==4&&look_halation)sprintf(msg,"LUT + halation %s",levels[look_halation]);
    else if(native_stage==FILM_UI_STAGE_BLOOM_GRAIN)strcpy(msg,"Adding bloom + grain");
    else if(native_stage==FILM_UI_STAGE_SOFT&&look_soft)sprintf(msg,"Soft focus %s",levels[look_soft]);
    else if(native_stage==6&&look_bloom)sprintf(msg,"Adding bloom %s",levels[look_bloom]);
    else if(native_stage==5&&look_grain)sprintf(msg,"Adding grain %s",levels[look_grain]);
    else if(native_stage==3)strcpy(msg,graded?"Saving JPEG":"DIGIC developing image");
    else strcpy(msg,native_stage==1?"Saving sensor backup":"Applying film 3D LUT");
    text(left+8,114,msg,COLOR_WHITE);
    film_menu_fill(left,top,right,bottom,FILM_SURFACE,FILM_BG,4);
    {
        int fill=(right-left+1)*pm/1000;
        if(fill>0)film_menu_fill(left,top,left+fill-1,bottom,FILM_ACCENT,FILM_SURFACE,fill>8?4:0);
    }
    sprintf(msg,"Stage %d of %d   Keep camera on",number,count);
    text(left+8,166,msg,FILM_MUTED);
    film_draw_end();
}
/* SAVED or FAILED: a small rounded card at the top left. */
static void result_card(int h,const char *a,const char *b) {
    int w=rbf_str_width(a),n=b?2:1;
    if(b&&rbf_str_width(b)>w)w=rbf_str_width(b);
    if(!film_draw_begin(8,38,8+w+34,38+n*(h+4)+10))return;
    film_menu_fill(8,38,8+w+34,38+n*(h+4)+10,FILM_BG,COLOR_TRANSPARENT,6);
    film_menu_fill(18,44+h/2-3,22,44+h/2+3,result_code==FILM_UI_SAVED?FILM_ACCENT:COLOR_RED,FILM_BG,2);
    text(30,43,a,COLOR_WHITE);
    if(b)text(30,47+h,b,FILM_MUTED);
    film_draw_end();
}
void film_ui_draw(void) {
    char msg[64];int h;
    /* ZrnoBoy's live screen, on every pass (half-press too, when CHDK's
     * OSD pass is skipped); the look preview draws its own with its card. */
    if(!active&&!recent()&&!film_preview_showing())film_gbui_live_draw(0);
    /* Don't obscure CHDK menus or a script; their GUI erases this overlay. */
    if(!camera_info.state.gui_mode_none||!(active||recent()))return;
    h=rbf_film_font(FILM_MENU_FONT,1);
    if(active)native_panel(h);
    else if(result_code==FILM_UI_SAVED){sprintf(msg,"Film %04d saved in DCIM",result_shot);result_card(h,msg,0);}
    else{sprintf(msg,"Film %04d failed (code %d)",result_shot,result_detail);result_card(h,msg,"See NATIVE.TXT for RAW/JPEG status");}
    rbf_film_font(0,0);
}

/* Shooting-screen indicator, drawn in CHDK's OSD pass, so it is hidden with
 * OSD off, in Canon menus and during half-press. A compact black card: an
 * orange dot and the LUT's name (the saved look's name, if the settings
 * match one, in a small tab above it), then one icon per active effect with its
 * level (1-3) as orange pips under it. With processing off nothing is shown;
 * when the current mode, drive, aspect or size would skip processing, the
 * card says why instead (grey dot). Black, white and greys are Canon's own;
 * the orange borrows CHDK's dark-blue slot while the card shows. */
#define FILM_UI_INDICATOR_X 6
#define FILM_UI_INDICATOR_TOP_Y 30
/* Clear of Canon's metering icon below (rows 214-235, read over USB from
 * the screen buffer): the card's bottom edge is row 209, four rows above. */
#define FILM_UI_INDICATOR_BOTTOM_Y 186
/* Indicator icons, 13 x 11, 2 bits a pixel (bit 0 = leftmost):
 * 0 clear, 1 white, 2 accent orange, 3 grey. Order: halation, bloom,
 * soft focus (feather), grain, vignette, light leaks, toy lens (child),
 * dust, date stamp. */
static const unsigned int film_icons[FILM_UI_ICONS][11] = {
    {0x000cc00,0x00c00c0,0x030a830,0x0c2aa0c,0x00a5680,0x30a5683,0x00a5680,0x0c2aa0c,0x030a830,0x00c00c0,0x000cc00},
    {0x0004400,0x0044440,0x0115510,0x0455544,0x0155550,0x1155551,0x0155550,0x0455544,0x0115510,0x0044440,0x0004400},
    {0x1400000,0x1540000,0x1150000,0x0514000,0x0145000,0x0051400,0x0014500,0x0005540,0x0000540,0x0000010,0x0000004},
    {0x0104041,0x1040410,0x0404101,0x0104044,0x1040440,0x0411011,0x0040110,0x1104401,0x0404044,0x0041040,0x0411001},
    {0x1540055,0x1400005,0x1000001,0x0000000,0x0000000,0x0000000,0x0000000,0x0000000,0x1000001,0x1400005,0x1540055},
    {0x1555555,0x10002a9,0x10000a9,0x1000029,0x1000009,0x1000001,0x1800001,0x1a00001,0x1a80001,0x1aa0001,0x1555555},
    {0x0005400,0x0015500,0x0015500,0x0005400,0x0155550,0x0415504,0x0015500,0x0015500,0x0014500,0x0050140,0x0050140},
    {0x0400001,0x0000400,0x0100000,0x0000014,0x0040140,0x0000400,0x0010400,0x0000100,0x0040050,0x0000001,0x0400100},
    {0x0100010,0x1555555,0x1555555,0x1000001,0x1051451,0x1000001,0x1051451,0x1000001,0x1001451,0x1000001,0x1555555},
};
/* What the card shows, as letter and level pairs ("H3B2S1"), and the
 * small look name as drawn (shortened to fit), for tests. */
static char osd_code[24],osd_preset[24];
/* Small text: the date stamp's 5x7 dot-matrix glyphs, set proportionally
 * (each glyph only as wide as its dots, one column apart; a space is 3). */
#include "film_font.h"
static const unsigned char *tiny_glyph(char c,int *first,int *width) {
    unsigned ch=(unsigned char)c;int r,bits=0,b;
    if(ch<32||ch>126)ch='?';
    for(r=0;r<7;r++)bits|=film_font5x7[ch-32][r];
    if(!bits){*first=0;*width=2;return film_font5x7[0];}
    for(b=0;!(bits&(0x10>>b));b++);
    *first=b;
    for(b=4;!(bits&(0x10>>b));b--);
    *width=b-*first+1;
    return film_font5x7[ch-32];
}
static int tiny_width(const char *s){int w=0,f,n;for(;*s;s++){tiny_glyph(*s,&f,&n);w+=n+1;}return w?w-1:0;}
static void tiny_text(int x,int y,const char *s,int c) {
    int r,b,first,width;
    for(;*s;s++) {
        const unsigned char *g=tiny_glyph(*s,&first,&width);
        for(r=0;r<7;r++)for(b=0;b<width;b++)if(g[r]&(0x10>>(first+b)))draw_pixel(x+b,y+r,c);
        x+=width+1;
    }
}
static int osd_tick,osd_drawn;
int film_ui_indicator_shown(void){return osd_drawn&&(unsigned)(get_tick_count()-osd_tick)<1000u;}
static void icon_in(int x,int y,const unsigned int *rows,int white,int accent,int grey) {
    int r,i;
    for(r=0;r<11;r++)for(i=0;i<13;i++) {
        int v=(rows[r]>>(2*i))&3;
        if(v)draw_pixel(x+i,y+r,v==1?white:v==2?accent:grey);
    }
}
static void draw_icon(int x,int y,const unsigned int *rows){icon_in(x,y,rows,COLOR_WHITE,FILM_ICON_ACCENT,COLOR_GREY);}
void film_ui_icon(int x,int y,int icon,int white,int accent,int grey) {
    if(icon>=0&&icon<FILM_UI_ICONS)icon_in(x,y,film_icons[icon],white,accent,grey);
}
/* The saved look's name sits in a tab rising from the card's top-left, its
 * small letters over the LUT's name: TAB_H rows, the tab's top corners
 * rounded, the card's top-left corner squared under it and a small inner
 * curve where the tab meets the card's top edge. At the top of the screen
 * the card moves down by the tab instead, so nothing is drawn higher. */
#define TAB_H FILM_UI_TAB_H
static void draw_tab(int x1,int ty,int x2,int y,int c) {
    static const unsigned char inset[4]={2,1,0,0};
    int r;
    for(r=0;r<TAB_H;r++) {
        int i=r<4?inset[r]:0;
        if(i){film_menu_fill(x1,ty+r,x1+i-1,ty+r,COLOR_TRANSPARENT,COLOR_TRANSPARENT,0);film_menu_fill(x2-i+1,ty+r,x2,ty+r,COLOR_TRANSPARENT,COLOR_TRANSPARENT,0);}
        film_menu_fill(x1+i,ty+r,x2-i,ty+r,c,c,0);
    }
    film_menu_fill(x1,y,x1+3,y+3,c,c,0);              /* square the card's corner under the tab */
    film_menu_fill(x2+1,y-1,x2+2,y-1,c,c,0);         /* inner curve */
    film_menu_fill(x2+1,y-2,x2+1,y-2,c,c,0);
}
int film_ui_tiny_width(const char *s){return tiny_width(s);}
void film_ui_tiny_text(int x,int y,const char *s,int c){tiny_text(x,y,s,c);}
void film_ui_tab(int x1,int ty,int x2,int y,int c){draw_tab(x1,ty,x2,y,c);}
void film_ui_draw_osd(void) {
    static const char *paused[]={"","","","scene mode","drive mode","not 4:3","image size","My Colors","unknown firmware"};
    static const char letters[FILM_UI_ICONS]={'H','B','F','G','V','L','T','D','S'};
    static int last[4]={-1,0,0,0};                        /* the area drawn last time */
    int levels[FILM_UI_ICONS],i,n=0,reason,h,y,ty,x,x2,tab_x2=0,room,box[4],u[4];char msg[32];
    /* The Grade engine names the grade itself ("Grade 2"); the tab is the
     * saved look's name, in any engine. */
    const char *name=film_grade_engine()?film_grui_slot_name():film_engine_alt()?film_gbui_engine_name():look_name;
    const char *tab_name=preset_name;
    if(!processing_on||indicator==FILM_UI_INDICATOR_OFF||active||recent()||film_gbui_live_shown())return;
    reason=film_route_reason();
    if(reason==FILM_ROUTE_OFF||reason==FILM_ROUTE_NOT_RECORDING)return;
    levels[0]=look_halation;levels[1]=look_bloom;levels[2]=look_soft;levels[3]=look_grain;levels[4]=look_vignette;
    levels[5]=look_leaks;levels[6]=look_toy;levels[7]=look_dust;levels[8]=look_stamp;
    if(film_engine_alt())memset(levels,0,sizeof(levels));       /* ZrnoBoy, Print: none of the film effects */
    osd_code[0]=0;
    if(reason==FILM_ROUTE_READY)
        for(i=0;i<FILM_UI_ICONS;i++)if(levels[i]){osd_code[n++]=letters[i];osd_code[n++]='0'+levels[i];osd_code[n]=0;}
    h=rbf_film_font(FILM_MENU_FONT,1);
    x=FILM_UI_INDICATOR_X+18+rbf_str_width(name)+8;
    if(reason!=FILM_ROUTE_READY){sprintf(msg,"paused: %s",paused[reason]);x2=x+rbf_str_width(msg)+6;}
    else x2=x+n/2*18-2;
    if(x2>353)x2=353;
    /* The tab's text starts over the LUT's name and the tab stays at least
     * 8 px narrower than the card: a longer name is shortened with "..",
     * and left out if that leaves fewer than 3 letters. */
    room=x2-8-6-(FILM_UI_INDICATOR_X+18);
    osd_preset[0]=0;
    if(tab_name[0]) {
        int k=strlen(tab_name);
        strcpy(osd_preset,tab_name);
        while(k>0&&tiny_width(osd_preset)>room){k--;memcpy(osd_preset,tab_name,k);strcpy(osd_preset+k,"..");}
        if(k<3&&k<(int)strlen(tab_name))osd_preset[0]=0;
        else tab_x2=FILM_UI_INDICATOR_X+18+tiny_width(osd_preset)+6;
    }
    y=indicator==FILM_UI_INDICATOR_TOP?FILM_UI_INDICATOR_TOP_Y+(tab_x2?TAB_H:0):FILM_UI_INDICATOR_BOTTOM_Y;
    ty=tab_x2?y-TAB_H:y;
    box[0]=FILM_UI_INDICATOR_X;box[1]=ty;box[2]=x2;box[3]=y+h+4;
    /* Compose over the last area too, so a tab or card that moved or went
     * away leaves nothing behind. */
    memcpy(u,box,sizeof(u));
    if(last[0]>=0){if(last[1]<u[1])u[1]=last[1];if(last[2]>u[2])u[2]=last[2];if(last[3]>u[3])u[3]=last[3];}
    if(!film_draw_begin(u[0],u[1],u[2],u[3])){rbf_film_font(0,0);return;}
    if(last[0]>=0&&memcmp(last,box,sizeof(box)))film_menu_fill(last[0],last[1],last[2],last[3],COLOR_TRANSPARENT,COLOR_TRANSPARENT,0);
    memcpy(last,box,sizeof(box));
    film_menu_fill(FILM_UI_INDICATOR_X,y,x2,y+h+4,COLOR_BLACK,COLOR_TRANSPARENT,6);
    if(tab_x2){draw_tab(FILM_UI_INDICATOR_X,ty,tab_x2,y,COLOR_BLACK);tiny_text(FILM_UI_INDICATOR_X+18,ty+3,osd_preset,COLOR_GREY_LT);}
    film_menu_fill(FILM_UI_INDICATOR_X+8,y+h/2-1,FILM_UI_INDICATOR_X+12,y+h/2+5,reason==FILM_ROUTE_READY?FILM_ICON_ACCENT:COLOR_GREY,COLOR_BLACK,2);
    rbf_draw_string(FILM_UI_INDICATOR_X+18,y+2,name,MAKE_COLOR(COLOR_BLACK,COLOR_WHITE));
    if(reason!=FILM_ROUTE_READY)rbf_draw_string(x,y+2,msg,MAKE_COLOR(COLOR_BLACK,COLOR_GREY_LT));
    else for(i=0;i<FILM_UI_ICONS;i++)if(levels[i]&&x+13<=353) {
        draw_icon(x,y+2,film_icons[i]);
        if(i<8){int k;for(k=0;k<3;k++)film_menu_fill(x+3+k*3,y+h-2,x+4+k*3,y+h-1,k<levels[i]?FILM_ICON_ACCENT:COLOR_GREY_DK,COLOR_BLACK,0);}
        x+=18;
    }
    film_draw_end();
    rbf_film_font(0,0);
    osd_tick=get_tick_count();osd_drawn=1;
}
