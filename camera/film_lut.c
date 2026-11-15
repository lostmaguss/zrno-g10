/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. CHDK G12 look catalogue and double-record selection.
 * A look is its DIGIC colour table (.HWT), which Canon's develop applies.
 * Menu callbacks only publish requests. SpyTask loads/validates/saves/swaps
 * while idle; the Canon task can never see a freed or partly loaded table. */
#include "camera_info.h"
#include "task.h"
#include "shooting.h"
#include "gui.h"
#include "clock.h"
#include "film_ui.h"
#include "film_colour.h"
#include "film_grain.h"
#include "film_bloom.h"
#include "film_toy.h"
#include "film_lut.h"
#include "meminfo.h"
#include "gui_tbox.h"
#include "film_print.h"
#include "film_clock.h"
#include "film_hwcolour.h"
#include "film_gbui.h"
#include "film_rsui.h"
#include "film_grui.h"
#define MAX_LOOKS 16
#define MAX_LABEL 20
#define CATALOG_BYTES 1024
typedef struct {char file[13],label[MAX_LABEL+1];} look_entry;
static look_entry looks[MAX_LOOKS];
static int count,choice,active=-1,initialized,legacy,current_slot=-1;
static unsigned generation;
static volatile int request=-1,loading;
static volatile unsigned request_version,request_tick,handled_version;
static int last_failed;
static void request_selection(void);
static void normalize_choices(void);
static void presets_load(void);
static void presets_reload_poll(void);
static void presets_store(void);
static void publish_look_name(void);
static void publish_preset_changed(void);
/* After a change is applied the screen is brought up to date. The Zrno
 * menus paint the whole screen, so while one is up a redraw is enough: a
 * restore would first erase it and show the live view for a moment. */
extern int film_menu_showing(void);
extern void gui_set_need_redraw(void);
static void refresh_screen(void){if(film_menu_showing())gui_set_need_redraw();else gui_set_need_restore();}
static int halo_choice,halo_active,grain_choice,grain_active,bloom_choice,bloom_active;
static volatile int requested_halo,requested_grain,requested_bloom,requested_on,requested_osd,requested_vig,requested_leak,requested_leds,requested_toy,
 requested_dust,requested_stamp,requested_smode,requested_soft,requested_sharp,requested_sbtn,requested_hcol;
static char requested_text[FILM_PRINT_TEXT+1];
/* Processing switch. Off leaves every capture exactly as Canon makes it.
 * The marker file is the stored setting; it is absent by default. */
#define PROCESSING_MARKER "A/CHDK/FILM/FILMON.TXT"
static int on_choice,on_active;
static const char *switch_names[2]={"Off","On"};
static int processing_stored(void){struct stat st;return !stat(PROCESSING_MARKER,&st);}
static int store_marker(const char *path,const char *tmp,const char *text,int present);
static int store_processing(int on){return store_marker(PROCESSING_MARKER,"A/CHDK/FILM/FILMON.TMP","Film processing on\n",on);}
/* Small settings files are written to a temporary name, verified by size and
 * then renamed over the old file, so a failed write keeps the old setting. */
static int replace_file(const char *path,const char *tmp,const void *data,int n) {
    struct stat st;int fd=open(tmp,O_WRONLY|O_CREAT|O_TRUNC,0777),w;
    if(fd<0)return -1;
    w=write(fd,data,n);
    if(close(fd)||w!=n||stat(tmp,&st)||st.st_size!=(unsigned)n){remove(tmp);return -1;}
    if(!stat(path,&st)&&remove(path)){remove(tmp);return -1;}
    if(rename(tmp,path)){remove(tmp);return -1;}
    return 0;
}
/* Shooting-screen indicator position. No file means Bottom; the file holds
 * one digit, 0 for Off or 2 for Top. */
#define INDICATOR_FILE "A/CHDK/FILM/FILMOSD.TXT"
static int osd_choice=FILM_UI_INDICATOR_BOTTOM,osd_active=FILM_UI_INDICATOR_BOTTOM;
static const char *indicator_names[3]={"Off","Bottom","Top"};
static int indicator_stored(void) {
    char c=0;int n,fd=open(INDICATOR_FILE,O_RDONLY,0);
    if(fd<0)return FILM_UI_INDICATOR_BOTTOM;
    n=read(fd,&c,1);close(fd);
    return n!=1?FILM_UI_INDICATOR_BOTTOM:c=='0'?FILM_UI_INDICATOR_OFF:c=='2'?FILM_UI_INDICATOR_TOP:FILM_UI_INDICATOR_BOTTOM;
}
static int store_indicator(int where) {
    struct stat st;char c='0'+where;
    if(where==FILM_UI_INDICATOR_BOTTOM) {
        if(!stat(INDICATOR_FILE,&st)&&remove(INDICATOR_FILE))return -1;
        return indicator_stored()==FILM_UI_INDICATOR_BOTTOM?0:-1;
    }
    if(replace_file(INDICATOR_FILE,"A/CHDK/FILM/FILMOSD.TMP",&c,1))return -1;
    return indicator_stored()==where?0:-1;
}
/* Vignette, light-leak, toy-lens, soft-focus and dust levels: a
 * 16-byte record "G12RT01", vignette at byte 8, leaks at byte 9, toy lens at
 * byte 10, and at byte 11 soft focus (bits 2-3), dust (bits 4-5) and
 * sharpening (bits 6-7); bits 0-1 held toning, which is gone: they are
 * ignored and written as 0, FNV checksum at byte 12 (over bytes 0-11). Missing or invalid means all Off, so firmware without
 * these effects simply ignores the file. */
#define RETRO_FILE "A/CHDK/FILM/RETRO.CFG"
static int vig_choice,vig_active,leak_choice,leak_active,toy_choice,toy_active,dust_choice,dust_active,soft_choice,soft_active,sharp_choice,sharp_active;
static unsigned checksum(const unsigned char *p,unsigned n);
static void put32(unsigned char *p,unsigned x);
static unsigned le32(const unsigned char *p);
static int retro_stored(int *vig,int *leak,int *toy,int *dust,int *soft,int *sharp) {
    unsigned char rec[16];int fd,n;
    *vig=*leak=*toy=*dust=*soft=*sharp=0;
    fd=open(RETRO_FILE,O_RDONLY,0);if(fd<0)return -1;
    n=read(fd,rec,16);close(fd);
    if(n!=16||memcmp(rec,"G12RT01",8)||le32(rec+12)!=checksum(rec,12)||rec[8]>3||rec[9]>3||rec[10]>3)return -1;
    *vig=rec[8];*leak=rec[9];*toy=rec[10];*soft=(rec[11]>>2)&3;*dust=(rec[11]>>4)&3;*sharp=rec[11]>>6;return 0;
}
static int store_retro(int vig,int leak,int toy,int dust,int soft,int sharp) {
    unsigned char rec[16];int v,l,t,d,f,h;
    memset(rec,0,16);memcpy(rec,"G12RT01",8);rec[8]=vig;rec[9]=leak;rec[10]=toy;rec[11]=(unsigned char)(soft<<2|dust<<4|sharp<<6);put32(rec+12,checksum(rec,12));
    if(replace_file(RETRO_FILE,"A/CHDK/FILM/RETRO.TMP",rec,16))return -1;
    return retro_stored(&v,&l,&t,&d,&f,&h)||v!=vig||l!=leak||t!=toy||d!=dust||f!=soft||h!=sharp?-1:0;
}
/* Date stamp: a 40-byte record "G12ST01", corner at byte 8 (0 Off, 1 bottom
 * right, 2 bottom left, 3 top right, 4 top left), content at byte 9 (0 'YY MM
 * DD, 1 DD MM 'YY, 2 the text), the text at bytes 10-33 (printable ASCII,
 * NUL padded), FNV checksum at byte 36 (over bytes 0-35). */
#define STAMP_FILE "A/CHDK/FILM/STAMP.CFG"
static int stamp_choice,stamp_active,smode_choice,smode_active;
static char stext_choice[FILM_PRINT_TEXT+1],stext_active[FILM_PRINT_TEXT+1];
static int printable_text(const unsigned char *t){int i;for(i=0;i<FILM_PRINT_TEXT&&t[i];i++)if(t[i]<32||t[i]>126)return 0;for(;i<FILM_PRINT_TEXT;i++)if(t[i])return 0;return 1;}
static int stamp_stored(int *corner,int *mode,char *text) {
    unsigned char rec[40];int fd,n;
    *corner=*mode=0;text[0]=0;
    fd=open(STAMP_FILE,O_RDONLY,0);if(fd<0)return -1;
    n=read(fd,rec,40);close(fd);
    if(n!=40||memcmp(rec,"G12ST01",8)||le32(rec+36)!=checksum(rec,36)||rec[8]>4||rec[9]>2||!printable_text(rec+10))return -1;
    *corner=rec[8];*mode=rec[9];memcpy(text,rec+10,FILM_PRINT_TEXT);text[FILM_PRINT_TEXT]=0;return 0;
}
static int store_stamp(int corner,int mode,const char *text) {
    unsigned char rec[40];int c,m;char t[FILM_PRINT_TEXT+1];
    memset(rec,0,40);memcpy(rec,"G12ST01",8);rec[8]=corner;rec[9]=mode;strncpy((char*)rec+10,text,FILM_PRINT_TEXT);put32(rec+36,checksum(rec,36));
    if(replace_file(STAMP_FILE,"A/CHDK/FILM/STAMP.TMP",rec,40))return -1;
    return stamp_stored(&c,&m,t)||c!=corner||m!=mode||strcmp(t,text)?-1:0;
}
/* Busy LEDs: On by default; the marker file exists only when turned Off. */
#define LEDS_OFF_MARKER "A/CHDK/FILM/LEDOFF.TXT"
static int leds_choice=1,leds_active=1;
static int leds_stored(void){struct stat st;return stat(LEDS_OFF_MARKER,&st)!=0;}
/* A setting kept as a marker file: present (written through tmp) or absent.
 * Returns 0 when the card then holds what was asked. */
static int store_marker(const char *path,const char *tmp,const char *text,int present) {
    struct stat st;
    if(!present){if(!stat(path,&st)&&remove(path))return -1;}
    else if(replace_file(path,tmp,text,strlen(text)))return -1;
    return (stat(path,&st)==0)==present?0:-1;
}
static int store_leds(int on){return store_marker(LEDS_OFF_MARKER,"A/CHDK/FILM/LEDOFF.TMP","Busy LEDs off\n",!on);}
/* S button: opens the Zrno menu (default). The marker file exists
 * only when it is set to enter CHDK's <ALT> mode as stock CHDK does. */
#define SBUTTON_CHDK_MARKER "A/CHDK/FILM/SCHDK.TXT"
static int sbtn_choice=1,sbtn_active=1;
static int sbtn_stored(void){struct stat st;return stat(SBUTTON_CHDK_MARKER,&st)!=0;}
static int store_sbtn(int film){return store_marker(SBUTTON_CHDK_MARKER,"A/CHDK/FILM/SCHDK.TMP","S button opens CHDK\n",!film);}
/* Halation colour: Red (the default, today's red-orange glow) or Amber. The
 * marker file exists only for Amber, so firmware without the row keeps Red. */
#define HALO_AMBER_MARKER "A/CHDK/FILM/HALOAMB.TXT"
static int hcol_choice,hcol_active;
static const char *hcol_names[2]={"Red","Amber"};
static int hcol_stored(void){struct stat st;return !stat(HALO_AMBER_MARKER,&st);}
static int store_hcol(int amber){return store_marker(HALO_AMBER_MARKER,"A/CHDK/FILM/HALOAMB.TMP","Halation colour amber\n",amber);}
static void publish_route(void){
    film_ui_set_processing(on_active);film_ui_set_indicator(osd_active);film_ui_set_leds(leds_active);
    film_ui_set_vignette(vig_active);film_ui_set_leaks(leak_active);film_ui_set_toy(toy_active);film_ui_set_soft(soft_active);
    film_ui_set_print(dust_active,stamp_active);
}
static const char *status_text="Starting";
static unsigned *resident;                     /* the look's hardware table */
static int hw_installed;
/* A .HWT is the 4000-byte table, "HWTINT1\0", the halation tint (three u16
 * Q8 gains, a zero u16) and 256 highlight weights per graded luma: the look's
 * colour and mask for the glow, fitted as if added after Canon's grade. */
#define HWT_BYTES (FILM_HW_TABLE_BYTES+16+256)
/* Beside it, a .HWA holds the tint for the Amber halation colour: 16 bytes,
 * "HWAMBR1\0", three u16 Q8 gains and a zero u16. It is a file of its own so
 * that firmware from before the colour row still reads every .HWT. In
 * memory the Amber gains follow the .HWT's bytes (LOOK_BYTES). */
#define HWA_BYTES 16
#define LOOK_BYTES (HWT_BYTES+8)
static film_halo_tint resident_tint;
static void *fx_work,*grain_work;
static size_t fx_bytes;
/* One effect pool. The colour pass uses halation and toy lens together, so
 * toy lens sits after halation (word aligned); Bloom reuses the pool later. */
static size_t halo_span_bytes(int halo){return halo?(film_halo_work_size(3648)+3u)&~(size_t)3u:0;}
static size_t effect_work_size(int halo,int bloom,int toy){
    size_t a=halo_span_bytes(halo)+(toy?film_toy_work_size(3648):0),b=bloom?film_bloom_work_size(3648):0;return a>b?a:b;
}
/* Heap headroom around each effect allocation, so a camera test shows what
 * the effects leave (A/CHDK/FILM/MEMLOG.TXT, capped at 8 KiB). */
#define MEMLOG "A/CHDK/FILM/MEMLOG.TXT"
static int largest_free(void){cam_meminfo m;GetCombinedMemInfo(&m);return m.free_block_max_size;}
static void log_memory(const char *event,size_t need,int before,int ok){
    cam_meminfo m;char line[128];struct stat st;int fd,n;
    if(!stat(MEMLOG,&st)&&st.st_size>8192)return;
    GetCombinedMemInfo(&m);
    n=sprintf(line,"%s need=%u ok=%d largest_before=%d largest_after=%d free_after=%d\n",event,(unsigned)need,ok,before,m.free_block_max_size,m.free_size);
    fd=open(MEMLOG,O_WRONLY|O_CREAT|O_APPEND,0777);if(fd<0)return;
    write(fd,line,n);close(fd);
}
static const char *halo_names[4]={"Off","Low","Mid","High"};
static void barrier(void){asm volatile("" ::: "memory");}
static unsigned le32(const unsigned char *p){return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static void put32(unsigned char *p,unsigned x){p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24;}
static unsigned checksum(const unsigned char *p,unsigned n){unsigned h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static int valid_file(const char *s) {
    int i=0;
    for(;s[i]&&s[i]!='.';i++)if(i>=8||!((s[i]>='A'&&s[i]<='Z')||(s[i]>='0'&&s[i]<='9')||s[i]=='_'||s[i]=='-'))return 0;
    return i>0&&!strcmp(s+i,".LUT");
}
static int find_file(const char *s){int i;for(i=0;i<count;i++)if(!strcmp(s,looks[i].file))return i;return -1;}
static int selection_halo(const unsigned char *p) {
    if(p[25]>1)return -1;
    if(p[27]==0)return p[26]==0?p[25]:-1;
    if(p[27]!=1||p[26]>3||p[25]!=(p[26]!=0))return -1;
    return p[26];
}
static void fallback_catalog(void){count=1;choice=0;legacy=1;strcpy(looks[0].file,"GOLD17.LUT");strcpy(looks[0].label,"Warm 200");}
static int load_catalog(void) {
    struct stat st;char text[CATALOG_BYTES+1],*line;int fd,n,bad=0;
    fallback_catalog();
    if(stat("A/CHDK/FILM/LUTS.TXT",&st))return 0;
    if(!st.st_size||st.st_size>CATALOG_BYTES)return -1;
    fd=open("A/CHDK/FILM/LUTS.TXT",O_RDONLY,0);if(fd<0)return -1;
    n=read(fd,text,st.st_size);if(close(fd)||n!=(int)st.st_size)return -1;
    if(memchr(text,0,n))return -1;
    text[n]=0;count=0;legacy=0;line=text;
    while(*line) {
        char *end=strchr(line,'\n'),*sep;int len,i;
        if(end)*end=0;
        len=strlen(line);if(len&&line[len-1]=='\r')line[--len]=0;
        if(len&&line[0]!='#') {
            sep=strchr(line,'|');if(!sep){bad=1;break;}*sep++=0;
            len=strlen(sep);
            if(count>=MAX_LOOKS||strlen(line)>12||!valid_file(line)||find_file(line)>=0||!len||len>MAX_LABEL){bad=1;break;}
            for(i=0;i<len;i++)if(sep[i]<32||sep[i]>126||sep[i]=='|')bad=1;
            if(bad)break;
            strcpy(looks[count].file,line);strcpy(looks[count].label,sep);count++;
        }
        if(!end)break;
        line=end+1;
    }
    if(bad||!count){fallback_catalog();return -1;}
    return 0;
}
static int read_legacy_selection(int slot,unsigned char out[32]) {
    char path[32];struct stat st;int fd,n;
    sprintf(path,"A/CHDK/FILM/LUTSEL%d.CFG",slot);
    if(stat(path,&st)||st.st_size!=32)return -1;
    fd=open(path,O_RDONLY,0);if(fd<0)return -1;
    n=read(fd,out,32);if(close(fd)||n!=32)return -1;
    if(memcmp(out,"G12SEL1",8)||le32(out+28)!=checksum(out,28)||!memchr(out+12,0,13)||selection_halo(out)<0||!valid_file((char*)out+12))return -1;
    return find_file((char*)out+12);
}
/* FXSEL stores bloom as well. Earlier FILMSEL/LUTSEL files stay untouched
 * for migration and restoration of the previous firmware. */
static int read_film_selection(int slot,unsigned char out[32],int previous) {
    char path[32];struct stat st;int fd,n;
    sprintf(path,previous?"A/CHDK/FILM/FILMSEL%d.CFG":"A/CHDK/FILM/FXSEL%d.CFG",slot);
    if(stat(path,&st)||st.st_size!=32)return -1;
    fd=open(path,O_RDONLY,0);if(fd<0)return -1;
    n=read(fd,out,32);if(close(fd)||n!=32)return -1;
    if(memcmp(out,previous?"G12FX01":"G12FX02",8)||le32(out+28)!=checksum(out,28)||!memchr(out+12,0,13)||out[25]>3||out[26]>3||!valid_file((char*)out+12))return -1;
    if(previous?out[27]!=1:out[27]>3)return -1;
    return find_file((char*)out+12);
}
static int read_selection(int slot,unsigned char out[32]){return read_film_selection(slot,out,0);}
static int save_selection(int index,int halo,int grain,int bloom) {
    unsigned char record[32],check[32];char path[32];int fd,n,slot=current_slot==0?1:0;
    memset(record,0,sizeof(record));memcpy(record,"G12FX02",8);put32(record+8,generation+1);
    strcpy((char*)record+12,looks[index].file);record[25]=halo;record[26]=grain;record[27]=bloom;put32(record+28,checksum(record,28));
    sprintf(path,"A/CHDK/FILM/FXSEL%d.CFG",slot);
    fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0777);if(fd<0)return -1;
    n=write(fd,record,32);if(close(fd)||n!=32)return -1;
    if(read_selection(slot,check)!=index||memcmp(check,record,32))return -1;
    generation++;current_slot=slot;return 0;
}
/* The look's .HWT beside the catalogue's .LUT name; 0 if missing or
 * malformed (the look cannot be used). The .LUT is the source the table is
 * fitted from (tools/hw_table.py); the camera does not read it. Without a
 * catalogue the one look is Warm 200 (GOLD17). */
static void load_amber(char *path,unsigned *t);
static unsigned *load_look(int index) {
    char path[32],*dot;struct stat st;int fd,n;unsigned *t;const unsigned char *trailer;
    sprintf(path,"A/CHDK/FILM/LUTS/%s",looks[index].file);
    dot=strrchr(path,'.');if(!dot)return 0;strcpy(dot,".HWT");
    if(stat(path,&st)||st.st_size!=HWT_BYTES)return 0;
    t=malloc(LOOK_BYTES);if(!t)return 0;
    fd=open(path,O_RDONLY,0);if(fd<0){free(t);return 0;}
    n=read(fd,t,HWT_BYTES);trailer=(const unsigned char*)t+FILM_HW_TABLE_BYTES;
    if(close(fd)||n!=HWT_BYTES||!film_hw_table_valid(t)||memcmp(trailer,"HWTINT1\0",8)){free(t);return 0;}
    load_amber(path,t);
    return t;
}
static unsigned u16le(const unsigned char *p){return p[0]|p[1]<<8;}
/* The Amber tint from the look's .HWA (path: its .HWT). Without a valid one
 * it is estimated from the Red tint, as tools/hw_table.py's fits come out:
 * a grey look's glow about 1.8 times as bright; a colour look's green up by
 * 5/16 of its red (the identity's Red 256, 51, 0 gives Amber 256, 131, 0). */
static void load_amber(char *path,unsigned *t) {
    unsigned char a[HWA_BYTES],*out=(unsigned char*)t+HWT_BYTES;const unsigned char *red=(const unsigned char*)t+FILM_HW_TABLE_BYTES+8;
    struct stat st;int fd,n=-1,k;unsigned v[3];
    strcpy(strrchr(path,'.'),".HWA");
    if(!stat(path,&st)&&st.st_size==HWA_BYTES&&(fd=open(path,O_RDONLY,0))>=0){n=read(fd,a,HWA_BYTES);if(close(fd))n=-1;}
    if(n==HWA_BYTES&&!memcmp(a,"HWAMBR1\0",8)&&!a[14]&&!a[15]&&u16le(a+8)<=1024&&u16le(a+10)<=1024&&u16le(a+12)<=1024){memcpy(out,a+8,6);return;}
    for(k=0;k<3;k++)v[k]=u16le(red+2*k);
    if(v[0]==v[1]&&v[1]==v[2])v[0]=v[1]=v[2]=v[0]*29u>>4;
    else v[1]+=v[0]*5u>>4;
    for(k=0;k<3;k++){if(v[k]>1024)v[k]=1024;out[2*k]=(unsigned char)v[k];out[2*k+1]=(unsigned char)(v[k]>>8);}
}
/* The halation tint of a loaded look for a colour (0 Red, 1 Amber). */
static void read_tint(const unsigned *t,film_halo_tint *tint,int amber) {
    const unsigned char *p=(const unsigned char*)t+FILM_HW_TABLE_BYTES+8,*g=amber?(const unsigned char*)t+HWT_BYTES:p;
    tint->red=u16le(g);tint->green=u16le(g+2);tint->blue=u16le(g+4);tint->highlight=p+8;
}
/* Canon grades with the look's table while processing is on; otherwise
 * Canon's own table is in place. The Sharpening setting sets Canon's edge
 * enhancement while processing is on. */
static const char *sharp_names[4]={"Original","Soft","Off","Crisp"};   /* Original: Canon's own */
static const short sharp_gain[4]={-1,32,0,112};    /* Canon's own is 64 */
/* The live view shows Canon's copy of the table, made when My Colors is set
 * and when shooting mode starts, so after a change of table it is reloaded
 * once: a new look, or Processing turned on or off, shows in the preview
 * straight away. The first table is installed about 5 s after power-on
 * (film_queue.c), when a camera started in shooting mode has long made its
 * copy of Canon's own: that copy is reloaded by film_lut_poll once shooting
 * mode has run for a second (start_reload). */
extern int film_preview_reloadable(void);
static const unsigned *previewed;
static int previewed_known,start_reload;
static unsigned start_rec_tick;
static const unsigned *grade_installed;         /* Grade's table, while Canon has it */
static void refresh_preview(void) {
    const unsigned *now=hw_installed?resident:grade_installed;
    if(!previewed_known){previewed=now;previewed_known=1;start_reload=now!=0;start_rec_tick=0;return;}
    if(now==previewed)return;
    if(film_preview_reloadable())film_hw_reload_preview();
    previewed=now;start_reload=0;
}
static void start_reload_poll(void) {
    if(!start_reload)return;
    if(!film_preview_reloadable()){start_rec_tick=0;return;}
    if(!start_rec_tick){start_rec_tick=get_tick_count()|1u;return;}
    if((unsigned)(get_tick_count()-start_rec_tick)<1000u)return;
    film_hw_reload_preview();start_reload=0;
}
/* ZrnoBoy and Print: Canon's own table. Canon applies the colour table
 * in the JPEG step, after the frame is written (a chart shot with
 * Tri-X came out grey), so a look would recolour their palette
 * and inks. Grade: the grade's own table, built afresh into the buffer
 * Canon is not reading each time the grade changes. */
static int hw_engine;                            /* 0 Film, 1 ZrnoBoy or Print, 2 Grade */
static unsigned hw_grade_serial,hw_grade_tick;
static int engine_kind(void){return film_grade_engine()?2:film_engine_alt();}
static void apply_hw(void) {
    const unsigned *grade=0;
    if(resident)read_tint(resident,&resident_tint,hcol_active);
    hw_engine=engine_kind();
    hw_grade_serial=film_grui_serial();hw_grade_tick=get_tick_count();
    if(on_active&&hw_engine==2)grade=film_grui_build(grade_installed);
    hw_installed=0;
    if(on_active&&resident&&!hw_engine)hw_installed=!film_hw_install(resident);
    if(grade&&!film_hw_install(grade))grade_installed=grade;
    else{grade_installed=0;if(!hw_installed)film_hw_restore();}
    film_hw_sharpen(on_active?sharp_gain[sharp_active]:-1);
    refresh_preview();
}
const unsigned *film_lut_hw_table(void){return hw_installed?resident:0;}
const film_halo_tint *film_lut_hw_tint(void){return hw_installed?&resident_tint:0;}
void film_lut_init(void) {
    int i,target=0,catalog_bad,saved_halo=0,saved_grain=0,saved_bloom=0;unsigned char record[32];unsigned *p;
    if(initialized)return;
    catalog_bad=load_catalog();
    for(i=0;i<2;i++) {
        int found=read_selection(i,record);
        if(found>=0) {
            unsigned gen=le32(record+8);
            if(current_slot<0||(int)(gen-generation)>0){target=found;generation=gen;current_slot=i;saved_halo=record[25];saved_grain=record[26];saved_bloom=record[27];}
        }
    }
    if(current_slot<0) {
        unsigned previous_generation=0;int found_previous=0;
        for(i=0;i<2;i++) {
            int found=read_film_selection(i,record,1);
            if(found>=0) {
                unsigned gen=le32(record+8);
                if(!found_previous||(int)(gen-previous_generation)>0){target=found;previous_generation=gen;found_previous=1;saved_halo=record[25];saved_grain=record[26];}
            }
        }
        if(!found_previous) {
        unsigned old_generation=0;int found_old=0;
        for(i=0;i<2;i++) {
            int found=read_legacy_selection(i,record);
            if(found>=0) {
                unsigned gen=le32(record+8);
                if(!found_old||(int)(gen-old_generation)>0){target=found;old_generation=gen;found_old=1;saved_halo=selection_halo(record);}
            }
        }
    }
    }
    p=load_look(target);
    if(!p&&target!=0){target=0;p=load_look(target);status_text="Using default";}
    else status_text=catalog_bad?"Catalog fallback":"Ready";
    on_choice=on_active=processing_stored();osd_choice=osd_active=indicator_stored();
    retro_stored(&vig_active,&leak_active,&toy_active,&dust_active,&soft_active,&sharp_active);vig_choice=vig_active;leak_choice=leak_active;
    dust_choice=dust_active;soft_choice=soft_active;sharp_choice=sharp_active;
    stamp_stored(&stamp_active,&smode_active,stext_active);stamp_choice=stamp_active;smode_choice=smode_active;strcpy(stext_choice,stext_active);
    leds_choice=leds_active=leds_stored();
    sbtn_choice=sbtn_active=sbtn_stored();
    hcol_choice=hcol_active=hcol_stored();
    presets_load();
    if(p){resident=p;active=target;choice=target;
        apply_hw();
        size_t need=effect_work_size(saved_halo,saved_bloom||soft_active,toy_active);
        if(need){int before=largest_free();fx_work=malloc(need);
            if(fx_work){fx_bytes=need;halo_choice=halo_active=saved_halo;bloom_choice=bloom_active=saved_bloom;}else{status_text="No glow memory";toy_active=0;soft_active=0;}
            log_memory("init",need,before,fx_work!=0);}
        if(saved_grain){grain_work=malloc(film_grain_work_size(3648));if(grain_work)grain_choice=grain_active=saved_grain;else status_text="No grain memory";}
        film_ui_set_halation(halo_active);film_ui_set_grain(grain_active);film_ui_set_bloom(bloom_active);
    }
    else toy_active=soft_active=0;
    toy_choice=toy_active;soft_choice=soft_active;publish_route();
    if(p)publish_look_name();
    else{status_text="LUT load failed";film_ui_set_look("LUT unavailable");}
    barrier();initialized=1;
}
void film_lut_poll(int stage_busy) {
    int index,halo,grain,bloom,on,osd,vig,leak,leds,toy,dust,stamp,smode,soft,sharp,sbtn,hcol,new_fx=0,new_grain=0,ok=0,same_lut;
    char text[FILM_PRINT_TEXT+1];
    size_t need;
    unsigned version;
    unsigned *next,*previous;void *fx_next,*grain_next;
    if(!initialized||stage_busy||film_ui_busy()||shooting_in_progress()||camera_info.state.state_kbd_script_run)return;
    start_reload_poll();
    if(engine_kind()!=hw_engine)apply_hw();            /* the engine changed: its table */
    /* A grade edit: at most one rebuild and live-view reload every 150 ms
     * while left/right is held. */
    else if(hw_engine==2&&film_grui_serial()!=hw_grade_serial&&(unsigned)(get_tick_count()-hw_grade_tick)>=150u)apply_hw();
    presets_reload_poll();
    presets_store();
    publish_preset_changed();
    if(request<0||request_version==handled_version)return;
    version=request_version;barrier();
    if(version&1u||(unsigned)(get_tick_count()-request_tick)<250u)return;
    index=request;halo=requested_halo;grain=requested_grain;bloom=requested_bloom;on=requested_on;osd=requested_osd;vig=requested_vig;leak=requested_leak;leds=requested_leds;toy=requested_toy;
    dust=requested_dust;stamp=requested_stamp;smode=requested_smode;soft=requested_soft;sharp=requested_sharp;sbtn=requested_sbtn;hcol=requested_hcol;memcpy(text,requested_text,sizeof(text));text[FILM_PRINT_TEXT]=0;barrier();
    if(version!=request_version)return;
    if(index==active&&halo==halo_active&&grain==grain_active&&bloom==bloom_active&&on==on_active&&osd==osd_active&&vig==vig_active&&leak==leak_active&&leds==leds_active&&toy==toy_active&&
       dust==dust_active&&soft==soft_active&&sharp==sharp_active&&sbtn==sbtn_active&&hcol==hcol_active&&stamp==stamp_active&&smode==smode_active&&!strcmp(text,stext_active)) {
        last_failed=0;status_text="Ready";barrier();handled_version=version;refresh_screen();return;
    }
    loading=1;status_text="Updating";
    same_lut=index==active&&resident;
    next=same_lut?resident:load_look(index);fx_next=fx_work;grain_next=grain_work;need=effect_work_size(halo,bloom||soft,toy);
    if(next&&need>fx_bytes){int before=largest_free();fx_next=malloc(need);new_fx=fx_next!=0;log_memory("change",need,before,new_fx);}
    if(next&&grain&&!grain_next){grain_next=malloc(film_grain_work_size(3648));new_grain=grain_next!=0;}
    /* A new key event can arrive during card I/O: discard this stale load
     * before persistence, then let the latest complete request run. */
    if(version!=request_version) {
        if(!same_lut)free(next);
        if(new_fx)free(fx_next);
        if(new_grain)free(grain_next);
        loading=0;return;
    }
    if(!next)status_text="LUT load failed";
    else if(need&&!fx_next)status_text="No glow memory";
    else if(grain&&!grain_next)status_text="No grain memory";
    else if(on!=on_active&&store_processing(on))status_text="Save failed";
    else if(osd!=osd_active&&store_indicator(osd))status_text="Save failed";
    else if((vig!=vig_active||leak!=leak_active||toy!=toy_active||dust!=dust_active||soft!=soft_active||sharp!=sharp_active)&&store_retro(vig,leak,toy,dust,soft,sharp))status_text="Save failed";
    else if((stamp!=stamp_active||smode!=smode_active||strcmp(text,stext_active))&&store_stamp(stamp,smode,text))status_text="Save failed";
    else if(leds!=leds_active&&store_leds(leds))status_text="Save failed";
    else if(sbtn!=sbtn_active&&store_sbtn(sbtn))status_text="Save failed";
    else if(hcol!=hcol_active&&store_hcol(hcol))status_text="Save failed";
    else if(save_selection(index,halo,grain,bloom))status_text="Save failed";
    else ok=1;
    if(!ok){
        if(!same_lut)free(next);
        if(new_fx)free(fx_next);
        if(new_grain)free(grain_next);
        /* The switch files may have changed before a later step failed:
         * report what is actually stored on the card. */
        on_active=processing_stored();osd_active=indicator_stored();retro_stored(&vig_active,&leak_active,&toy_active,&dust_active,&soft_active,&sharp_active);leds_active=leds_stored();sbtn_active=sbtn_stored();hcol_active=hcol_stored();
        stamp_stored(&stamp_active,&smode_active,stext_active);
        apply_hw();
        /* The stored toy level may now exceed the pool kept from before. */
        if(toy_active&&effect_work_size(halo_active,bloom_active||soft_active,toy_active)>fx_bytes)toy_active=0;
        if(soft_active&&effect_work_size(halo_active,1,toy_active)>fx_bytes)soft_active=0;
    }
    else {
        previous=resident;resident=next;active=index;barrier();
        if(need){if(new_fx){free(fx_work);fx_work=fx_next;fx_bytes=need;}}else {free(fx_work);fx_work=0;fx_bytes=0;}
        if(grain)grain_work=grain_next;else {free(grain_work);grain_work=0;}
        halo_active=halo;grain_active=grain;bloom_active=bloom;on_active=on;osd_active=osd;vig_active=vig;leak_active=leak;leds_active=leds;sbtn_active=sbtn;hcol_active=hcol;toy_active=toy;
        dust_active=dust;soft_active=soft;sharp_active=sharp;stamp_active=stamp;smode_active=smode;strcpy(stext_active,text);
        film_ui_set_halation(halo_active);film_ui_set_grain(grain_active);film_ui_set_bloom(bloom_active);
        status_text="Ready";
        apply_hw();                           /* Canon points at the new table before the old is freed */
        if(!same_lut)free(previous);
    }
    /* Only the keyboard task edits choices. Publishing completion by version
     * cannot erase an event arriving during this load/save. */
    publish_route();publish_look_name();
    last_failed=!ok;barrier();handled_version=version;
    barrier();loading=0;refresh_screen();
}
const void *film_lut_data(void){return resident;}
int film_lut_processing(void){return on_active;}
unsigned film_lut_vignette_level(void){return vig_active;}
unsigned film_lut_toy_level(void){return toy_active;}
void *film_lut_toy_work(void){return toy_active&&fx_work?(unsigned char*)fx_work+halo_span_bytes(halo_active):0;}
const char *film_lut_menu_toy(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();toy_choice=(toy_choice+change%4+4)%4;request_selection();}return halo_names[request_version==handled_version&&last_failed?toy_active:toy_choice];}
int film_lut_leds(void){return leds_active;}
int film_lut_s_opens_film_menu(void){return sbtn_active;}
const char *film_lut_menu_sbutton(int change,int arg){static const char *names[2]={"CHDK","Zrno menu"};(void)arg;if(initialized&&change){normalize_choices();sbtn_choice=!sbtn_choice;request_selection();}return names[request_version==handled_version&&last_failed?sbtn_active:sbtn_choice];}
const char *film_lut_menu_leds(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();leds_choice=!leds_choice;request_selection();}return switch_names[request_version==handled_version&&last_failed?leds_active:leds_choice];}
unsigned film_lut_leak_level(void){return leak_active;}
const char *film_lut_menu_vignette(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();vig_choice=(vig_choice+change%4+4)%4;request_selection();}return halo_names[request_version==handled_version&&last_failed?vig_active:vig_choice];}
const char *film_lut_menu_leaks(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();leak_choice=(leak_choice+change%4+4)%4;request_selection();}return halo_names[request_version==handled_version&&last_failed?leak_active:leak_choice];}
const char *film_lut_menu_indicator(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();osd_choice=(osd_choice+(change>0?1:2))%3;request_selection();}return indicator_names[request_version==handled_version&&last_failed?osd_active:osd_choice];}
const char *film_lut_menu_processing(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();on_choice=!on_choice;request_selection();}return switch_names[request_version==handled_version&&last_failed?on_active:on_choice];}
const char *film_lut_name(void){return active>=0?looks[active].label:"Unavailable";}
const char *film_lut_file(void){return active>=0?looks[active].file:"none";}
void *film_lut_halo_work(void){return halo_active?fx_work:0;}
unsigned film_lut_halo_level(void){return halo_active;}
static void normalize_choices(void) {
    if(request_version==handled_version&&last_failed){choice=active>=0?active:0;halo_choice=halo_active;grain_choice=grain_active;bloom_choice=bloom_active;on_choice=on_active;osd_choice=osd_active;vig_choice=vig_active;leak_choice=leak_active;leds_choice=leds_active;sbtn_choice=sbtn_active;hcol_choice=hcol_active;toy_choice=toy_active;
        dust_choice=dust_active;soft_choice=soft_active;sharp_choice=sharp_active;stamp_choice=stamp_active;smode_choice=smode_active;strcpy(stext_choice,stext_active);}
}
const char *film_lut_menu_halo(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();halo_choice=(halo_choice+change%4+4)%4;request_selection();}return halo_names[request_version==handled_version&&last_failed?halo_active:halo_choice];}
void *film_lut_grain_work(void){return grain_active?grain_work:0;}
unsigned film_lut_grain_level(void){return grain_active;}
const char *film_lut_menu_grain(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();grain_choice=(grain_choice+change%4+4)%4;request_selection();}return halo_names[request_version==handled_version&&last_failed?grain_active:grain_choice];}
void *film_lut_bloom_work(void){return bloom_active?fx_work:0;}
/* Soft focus runs before Bloom in the same workspace. */
unsigned film_lut_soft_level(void){return soft_active;}
void *film_lut_soft_work(void){return soft_active?fx_work:0;}
const char *film_lut_menu_sharpen(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();sharp_choice=(sharp_choice+change%4+4)%4;request_selection();}return sharp_names[request_version==handled_version&&last_failed?sharp_active:sharp_choice];}
const char *film_lut_menu_soft(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();soft_choice=(soft_choice+change%4+4)%4;request_selection();}return halo_names[request_version==handled_version&&last_failed?soft_active:soft_choice];}
unsigned film_lut_bloom_level(void){return bloom_active;}
const char *film_lut_menu_bloom(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();bloom_choice=(bloom_choice+change%4+4)%4;request_selection();}return halo_names[request_version==handled_version&&last_failed?bloom_active:bloom_choice];}
const char *film_lut_menu_choice(int change,int arg) {
    (void)arg;
    if(!initialized)return "Starting";
    if(change){normalize_choices();choice=(choice+change%count+count)%count;request_selection();}
    return looks[request_version==handled_version&&last_failed&&active>=0?active:choice].label;
}
const char *film_lut_menu_status(int change,int arg){(void)change;(void)arg;if(request_version!=handled_version)return film_ui_busy()||shooting_in_progress()?"After capture":"Updating";return status_text;}
static void request_selection(void) {
    if(!initialized)return;
    request_version++;barrier();
    requested_halo=halo_choice;requested_grain=grain_choice;requested_bloom=bloom_choice;requested_on=on_choice;requested_osd=osd_choice;requested_vig=vig_choice;requested_leak=leak_choice;requested_leds=leds_choice;requested_sbtn=sbtn_choice;requested_hcol=hcol_choice;requested_toy=toy_choice;
    requested_dust=dust_choice;requested_soft=soft_choice;requested_sharp=sharp_choice;requested_stamp=stamp_choice;requested_smode=smode_choice;memcpy(requested_text,stext_choice,sizeof(requested_text));
    request=choice;request_tick=(unsigned)get_tick_count();
    barrier();request_version++;
}

/* Dust and the date stamp. */
/* Monochrome looks: every node of the table has zero chroma (U, V). */
int film_lut_mono(void) {
    unsigned i;
    if(!resident)return 0;
    for(i=0;i<FILM_HW_TABLE_WORDS;i++)if(resident[i]&0xfffffu)return 0;
    return 1;
}
static const char *stamp_names[5]={"Off","Bottom right","Bottom left","Top right","Top left"};
static const char *smode_names[3]={"'YY MM DD","DD MM 'YY","Text"};
#define SHOWN(choice,active) (request_version==handled_version&&last_failed?active:choice)
const char *film_lut_menu_halo_colour(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();hcol_choice=!hcol_choice;request_selection();}return hcol_names[SHOWN(hcol_choice,hcol_active)];}
const char *film_lut_menu_dust(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();dust_choice=(dust_choice+change%4+4)%4;request_selection();}return halo_names[SHOWN(dust_choice,dust_active)];}
const char *film_lut_menu_stamp(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();stamp_choice=(stamp_choice+change%5+5)%5;request_selection();}return stamp_names[SHOWN(stamp_choice,stamp_active)];}
const char *film_lut_menu_stamp_mode(int change,int arg){(void)arg;if(initialized&&change){normalize_choices();smode_choice=(smode_choice+change%3+3)%3;request_selection();}return smode_names[SHOWN(smode_choice,smode_active)];}
/* Entered text: printable ASCII is kept, anything else becomes '?'. A new
 * text also switches the stamp to show it. Cancel (0) changes nothing. */
static void stamp_text_entered(const char *text) {
    int i;
    if(!text||!initialized)return;
    normalize_choices();
    for(i=0;i<FILM_PRINT_TEXT&&text[i];i++){unsigned char c=(unsigned char)text[i];stext_choice[i]=(char)(c>=32&&c<=126?c:'?');}
    stext_choice[i]=0;smode_choice=2;request_selection();
}
const char *film_lut_menu_stamp_text(int change,int arg) {
    static char shown[16];const char *t;
    (void)arg;
    if(initialized&&change){normalize_choices();libtextbox->textbox_init((int)(size_t)"Stamp text",(int)(size_t)"Printable ASCII, up to 24",stext_choice,FILM_PRINT_TEXT,stamp_text_entered,0);}
    t=SHOWN(0,1)?stext_active:stext_choice;
    if(!t[0])return "(none)";
    if(strlen(t)<=12)return t;
    memcpy(shown,t,10);strcpy(shown+10,"..");return shown;
}
/* Looks whose dust shows dark: slides and instant film. Prints from
 * negatives show it light. */
static int dark_dust_look(const char *file) {
    static const char *dark[]={"VELVIA.LUT","KCHROME.LUT","PROVIA.LUT","ASTIA.LUT","POLA669.LUT"};
    unsigned i;
    for(i=0;i<sizeof(dark)/sizeof(dark[0]);i++)if(!strcmp(file,dark[i]))return 1;
    return 0;
}
void film_lut_print_settings(film_print_settings *s) {
    memset(s,0,sizeof(*s));
    s->dust=dust_active;s->stamp=stamp_active;
    s->dust_dark=active>=0&&dark_dust_look(looks[active].file);
    if(!stamp_active)return;
    if(smode_active==2){strncpy(s->text,stext_active,FILM_PRINT_TEXT);return;}
    {
        int y,m,d;film_clock_date(&y,&m,&d);
        if(smode_active==0)sprintf(s->text,"'%02d %02d %02d",y%100,m,d);
        else sprintf(s->text,"%02d %02d '%02d",d,m,y%100);
    }
}

/* Looks: saved sets of settings, recalled with one menu row and no typing,
 * in every engine. A look keeps its engine and that engine's settings:
 * Film's LUT, every effect, dust and the date stamp; ZrnoBoy's and
 * Print's rows; Grade's grade (which of the four, and its values) and its
 * sharpening. Recalling it switches to its engine and leaves the other
 * engines' settings as they are. Saving names a look by its place in the
 * list ("Look 1", "Look 2"); a name can also be typed on the camera or
 * edited on a computer. Processing, the indicator, Busy LEDs, the S button
 * and Live preview are camera settings, not part of a look. LOOKS.TXT holds
 * one look per line:
 *   Name|LUT file|halation=Low bloom=Off ...|stamp text   (Film)
 *   Name|ZrnoBoy|palette=1 frame=7 ...   Name|Print|style=0 ...   Name|Grade|grade=1 ...
 * Unknown keys are ignored and a setting left out takes its first value (a
 * grade's, neutral). The stamp text (the rest of the line) counts only with
 * shows=Text, and the stamp's content only with the stamp on, so a look
 * without its own text keeps the text last typed. The menu edits the list
 * in memory; SpyTask writes the file while idle. */
#define LOOKS_FILE "A/CHDK/FILM/LOOKS.TXT"
#define LOOKS_BYTES 16384               /* the largest file read or written: 16 Grade looks with every value set fit */
#define MAX_PRESETS 16
#define PRESET_NAME 20
enum {ENGINE_FILM,ENGINE_GBOY,ENGINE_PRINT,ENGINE_GRADE};
enum {PV_HALO,PV_BLOOM,PV_SOFT,PV_SHARP,PV_GRAIN,PV_VIG,PV_LEAK,PV_TOY,PV_DUST,PV_STAMP,PV_SMODE,PV_HCOL,PV_COUNT};
/* v: Film's settings (Grade's sharpening too, in v[PV_SHARP]); gboy, riso,
 * slot and grade: the other engines'. Each look fills only its engine's. */
typedef struct {
    char name[PRESET_NAME+1],file[13],text[FILM_PRINT_TEXT+1];
    unsigned char engine,v[PV_COUNT],slot;
    film_gboy_settings gboy;film_riso_settings riso;film_grade_settings grade;
} preset_entry;
static preset_entry presets[MAX_PRESETS];
static int preset_count,preset_base=-1,preset_armed,published_preset=-2;
static volatile unsigned presets_version,presets_saved;
static const char *engine_words[FILM_ENGINES]={"Film","ZrnoBoy","Print","Grade"};
static const char *pv_keys[PV_COUNT]={"halation","bloom","soft","sharpening","grain","vignette","leaks","toy","dust","stamp","shows","halationcolour"};
static const char *stamp_tokens[5]={"Off","BottomRight","BottomLeft","TopRight","TopLeft"};
static const char *smode_tokens[3]={"YYMMDD","DDMMYY","Text"};
static const char **pv_names(int k,int *n) {
    switch(k){
    case PV_SHARP:*n=4;return sharp_names;
    case PV_STAMP:*n=5;return stamp_tokens;
    case PV_SMODE:*n=3;return smode_tokens;
    case PV_HCOL:*n=2;return hcol_names;
    default:*n=4;return halo_names;
    }
}
/* ZrnoBoy's and Print's settings in the file, in the menu's order: each
 * row's place in its list, counting from 0 (bytes of the engine's
 * settings). */
typedef struct {const char *key;unsigned char at,count;} engine_key;
#define AT(type,field) __builtin_offsetof(type,field)   /* CHDK's stddef.h has no offsetof */
static const engine_key gboy_keys[]={
    {"palette",AT(film_gboy_settings,palette),FILM_GBOY_PALETTES},{"frame",AT(film_gboy_settings,frame),FILM_GBOY_FRAMES},
    {"resolution",AT(film_gboy_settings,resolution),FILM_GBOY_RESOLUTIONS},{"contrast",AT(film_gboy_settings,contrast),FILM_GBOY_CONTRASTS},
    {"brightness",AT(film_gboy_settings,brightness),FILM_GBOY_BRIGHTNESS},{"edges",AT(film_gboy_settings,edges),FILM_GBOY_EDGES},
    {"dither",AT(film_gboy_settings,dither),2}};
static const engine_key riso_keys[]={
    {"style",AT(film_riso_settings,style),FILM_RISO_STYLES},{"palette",AT(film_riso_settings,variant),255},   /* by style: Print checks it */
    {"inks",AT(film_riso_settings,inks),FILM_RISO_SETS},{"separation",AT(film_riso_settings,separation),FILM_RISO_SEPARATIONS},
    {"screen",AT(film_riso_settings,screen),FILM_RISO_SCREENS},{"noise",AT(film_riso_settings,noise),FILM_RISO_NOISES},
    {"register",AT(film_riso_settings,reg),FILM_RISO_REGISTERS},{"flaws",AT(film_riso_settings,flaws),FILM_RISO_FLAWS},
    {"paper",AT(film_riso_settings,paper),FILM_RISO_PAPERS}};
#define GBOY_KEYS (int)(sizeof(gboy_keys)/sizeof(gboy_keys[0]))
#define RISO_KEYS (int)(sizeof(riso_keys)/sizeof(riso_keys[0]))
static int lower(int c){return c>='A'&&c<='Z'?c+32:c;}
static int same_word(const char *a,const char *b){while(*a&&lower(*a)==lower(*b)){a++;b++;}return !*a&&!*b;}
/* A grade's values in the file: the menu's numbers, by field
 * (film_grade.h's order), HSL as "redhue", "redsaturation", "redlightness". */
static const char *grade_words[FILM_GRADE_HSL_HUE]={"contrast","midtones","shadows","highlights","fade","temperature","tint","saturation","vibrance",
    "shadowtone","shadowamount","highlighttone","highlightamount"};
static void grade_key(int f,char *out) {
    int i,k;const char *hue;
    if(f<FILM_GRADE_HSL_HUE){strcpy(out,grade_words[f]);return;}
    if(f>=FILM_GRADE_BLACKS){strcpy(out,f==FILM_GRADE_BLACKS?"blacks":f==FILM_GRADE_WHITES?"whites":"blackwhite");return;}
    k=(f-FILM_GRADE_HSL_HUE)/FILM_GRADE_HUES;hue=film_grade_hue_name((unsigned)((f-FILM_GRADE_HSL_HUE)%FILM_GRADE_HUES));
    for(i=0;hue[i];i++)out[i]=(char)lower(hue[i]);
    strcpy(out+i,k==0?"hue":k==1?"saturation":"lightness");
}
/* A whole number with an optional sign, up to four digits. */
static int parse_number(const char *s,int *out) {
    int sign=1,v=0,n=0;
    if(*s=='+'||*s=='-'){if(*s=='-')sign=-1;s++;}
    for(;*s>='0'&&*s<='9'&&n<4;s++,n++)v=v*10+*s-'0';
    if(!n||*s)return 0;
    *out=sign*v;return 1;
}
/* Film's value for key k: a name in any case, or its digit. */
static int pv_value(int k,const char *val,unsigned char *out) {
    int i,n;const char **names=pv_names(k,&n);
    if(val[0]>='0'&&val[0]<'0'+n&&!val[1]){*out=(unsigned char)(val[0]-'0');return 1;}
    for(i=0;i<n;i++)if(same_word(names[i],val)){*out=(unsigned char)i;return 1;}
    if(k==PV_SHARP&&same_word("Canon",val)){*out=0;return 1;}   /* Original's earlier name */
    return 0;
}
/* Content that only counts when used: no mode with the stamp off, no text
 * unless it shows Text, no halation colour with halation off. */
static void preset_tidy(preset_entry *p){if(p->engine)return;if(!p->v[PV_STAMP])p->v[PV_SMODE]=0;if(p->v[PV_SMODE]!=2)p->text[0]=0;if(!p->v[PV_HALO])p->v[PV_HCOL]=0;}
/* The settings now: as the menu shows them (shown), or as applied. */
static void preset_now(preset_entry *p,int shown) {
    int lut=shown?SHOWN(choice,active):active;
    memset(p,0,sizeof(*p));
    p->engine=(unsigned char)film_gbui_engine();
    switch(p->engine){
    case ENGINE_GBOY:film_gbui_get(&p->gboy);return;
    case ENGINE_PRINT:film_rsui_get(&p->riso);return;
    case ENGINE_GRADE:{int slot;film_grui_get(&slot,&p->grade);p->slot=(unsigned char)slot;}
        p->v[PV_SHARP]=shown?SHOWN(sharp_choice,sharp_active):sharp_active;return;
    }
    if(lut>=0)strcpy(p->file,looks[lut].file);
    p->v[PV_HALO]=shown?SHOWN(halo_choice,halo_active):halo_active;
    p->v[PV_BLOOM]=shown?SHOWN(bloom_choice,bloom_active):bloom_active;
    p->v[PV_SOFT]=shown?SHOWN(soft_choice,soft_active):soft_active;
    p->v[PV_SHARP]=shown?SHOWN(sharp_choice,sharp_active):sharp_active;
    p->v[PV_GRAIN]=shown?SHOWN(grain_choice,grain_active):grain_active;
    p->v[PV_VIG]=shown?SHOWN(vig_choice,vig_active):vig_active;
    p->v[PV_LEAK]=shown?SHOWN(leak_choice,leak_active):leak_active;
    p->v[PV_TOY]=shown?SHOWN(toy_choice,toy_active):toy_active;
    p->v[PV_DUST]=shown?SHOWN(dust_choice,dust_active):dust_active;
    p->v[PV_STAMP]=shown?SHOWN(stamp_choice,stamp_active):stamp_active;
    p->v[PV_SMODE]=shown?SHOWN(smode_choice,smode_active):smode_active;
    p->v[PV_HCOL]=shown?SHOWN(hcol_choice,hcol_active):hcol_active;
    strcpy(p->text,shown?SHOWN(stext_choice,stext_active):stext_active);
    preset_tidy(p);
}
static int preset_same(const preset_entry *a,const preset_entry *b) {
    if(a->engine!=b->engine)return 0;
    switch(a->engine){
    case ENGINE_GBOY:return !memcmp(&a->gboy,&b->gboy,sizeof(a->gboy));
    case ENGINE_PRINT:return !memcmp(&a->riso,&b->riso,sizeof(a->riso));
    case ENGINE_GRADE:return a->slot==b->slot&&a->v[PV_SHARP]==b->v[PV_SHARP]&&!memcmp(&a->grade,&b->grade,sizeof(a->grade));
    }
    return !strcmp(a->file,b->file)&&!memcmp(a->v,b->v,PV_COUNT)&&!strcmp(a->text,b->text);
}
/* Custom picked on the Look row: the settings stay as they are and no
 * saved look is current, not even one they equal, until a look is
 * recalled or saved. RAM only, like preset_base. */
static int preset_none;
/* The saved look the settings equal, preferring the last one recalled. */
static int preset_matching(int shown) {
    preset_entry now;int i;
    if(preset_none)return -1;
    preset_now(&now,shown);
    if(preset_base>=0&&preset_base<preset_count&&preset_same(&presets[preset_base],&now))return preset_base;
    for(i=0;i<preset_count;i++)if(preset_same(&presets[i],&now))return i;
    return -1;
}
static int preset_current(void){int i=preset_matching(1);if(i>=0)preset_base=i;return i;}
/* The indicator's tab: the saved look the applied settings equal. The
 * other engines' rows change their settings without film_lut.c, so SpyTask
 * also checks it on each poll (publish_preset_changed). */
static void publish_preset(void){int i=preset_matching(0);published_preset=i;film_ui_set_preset(i>=0?presets[i].name:"");}
static void publish_preset_changed(void){if(preset_matching(0)!=published_preset)publish_preset();}
static void publish_look_name(void){film_ui_set_look(active>=0?looks[active].label:"LUT unavailable");publish_preset();}
/* Printable ASCII up to max characters, spaces trimmed (names) or kept. */
static void copy_text(char *out,const char *in,int max,int trim) {
    int n=0;
    if(trim)while(*in==' '||*in=='\t')in++;
    for(;*in&&n<max;in++)out[n++]=(char)(*in>=32&&*in<=126?*in:'?');
    if(trim)while(n&&out[n-1]==' ')n--;
    out[n]=0;
}
/* One key=value of a look's settings, by its engine. Values out of range
 * are ignored (the setting keeps its first value). */
static void preset_key(preset_entry *p,const char *key,const char *val) {
    int k,n;unsigned char b;
    switch(p->engine){
    case ENGINE_FILM:
        for(k=0;k<PV_COUNT;k++)if(same_word(pv_keys[k],key)&&pv_value(k,val,&b))p->v[k]=b;
        return;
    case ENGINE_GBOY:case ENGINE_PRINT:{
        const engine_key *t=p->engine==ENGINE_GBOY?gboy_keys:riso_keys;
        unsigned char *out=p->engine==ENGINE_GBOY?(unsigned char*)&p->gboy:(unsigned char*)&p->riso;
        int keys=p->engine==ENGINE_GBOY?GBOY_KEYS:RISO_KEYS;
        for(k=0;k<keys;k++)if(same_word(t[k].key,key)&&parse_number(val,&n)&&n>=0&&n<t[k].count)out[t[k].at]=(unsigned char)n;
        return;}
    default:{
        char name[24];
        if(same_word("grade",key)){if(parse_number(val,&n)&&n>=1&&n<=FILM_GRUI_SLOTS)p->slot=(unsigned char)(n-1);return;}
        if(same_word("sharpening",key)){if(pv_value(PV_SHARP,val,&b))p->v[PV_SHARP]=b;return;}
        for(k=0;k<FILM_GRADE_FIELDS;k++){
            grade_key(k,name);
            if(same_word(name,key)){if(parse_number(val,&n)&&n>=film_grade_min(k)&&n<=film_grade_max(k))p->grade.v[k]=(signed char)n;return;}
        }}
    }
}
static void preset_parse(char *line) {
    char *field[3],*s=line,*tok;preset_entry *p=&presets[preset_count];int k,fields;
    if(preset_count>=MAX_PRESETS||line[0]=='#')return;
    for(fields=0;fields<3;fields++){field[fields]=strchr(s,'|');if(!field[fields])break;*field[fields]++=0;s=field[fields];}
    if(fields<2)return;
    memset(p,0,sizeof(*p));
    copy_text(p->name,line,PRESET_NAME,1);
    { char file[32];copy_text(file,field[0],31,1);if(!p->name[0]||!file[0]||strlen(file)>12)return;
      for(k=1;k<FILM_ENGINES;k++)if(same_word(engine_words[k],file))p->engine=(unsigned char)k;
      if(!p->engine)for(k=0;file[k];k++)p->file[k]=(char)(file[k]>='a'&&file[k]<='z'?file[k]-32:file[k]); }
    if(p->engine==ENGINE_GRADE)film_grade_neutral(&p->grade);
    for(tok=field[1];*tok;) {
        char *end,*eq;
        while(*tok==' '||*tok=='\t'||*tok==',')tok++;
        if(!*tok)break;
        for(end=tok;*end&&*end!=' '&&*end!='\t'&&*end!=',';end++);
        if(*end)*end++=0;
        eq=strchr(tok,'=');
        if(eq){*eq++=0;preset_key(p,tok,eq);}
        tok=end;
    }
    if(fields==3&&!p->engine)copy_text(p->text,field[2],FILM_PRINT_TEXT,0);
    preset_tidy(p);preset_count++;
}
static void presets_load(void) {
    struct stat st;char *text,*line;int fd,n;
    preset_count=0;preset_base=-1;preset_armed=0;published_preset=-2;
    if(stat(LOOKS_FILE,&st)||!st.st_size||st.st_size>LOOKS_BYTES)return;
    text=malloc(st.st_size+1);if(!text)return;
    fd=open(LOOKS_FILE,O_RDONLY,0);
    if(fd<0){free(text);return;}
    n=read(fd,text,st.st_size);close(fd);
    if(n!=(int)st.st_size){free(text);return;}
    text[n]=0;
    for(line=text;*line;) {
        char *end=strchr(line,'\n');int len;
        if(end)*end=0;
        len=strlen(line);if(len&&line[len-1]=='\r')line[--len]=0;
        if(len)preset_parse(line);
        if(!end)break;
        line=end+1;
    }
    free(text);
}
static const char looks_header[]=
    "# Looks: the Look row of the Zrno menu, in every engine. One look per line:\n"
    "#   Name|LUT file|settings|stamp text   (Film)\n"
    "#   Name|ZrnoBoy|settings   Name|Print|settings   Name|Grade|settings\n"
    "# A setting left out takes its first value (Grade: the neutral grade's).\n"
    "# Film: LUT files are listed in LUTS.TXT.\n"
    "#   halation bloom soft grain vignette leaks toy dust: Off Low Mid High   halationcolour: Red Amber\n"
    "#   sharpening: Original Soft Off Crisp\n"
    "#   stamp: Off BottomRight BottomLeft TopRight TopLeft   shows: YYMMDD DDMMYY Text\n"
    "#   The stamp text is used with shows=Text.\n"
    "# ZrnoBoy and Print: each row's place in its menu list, counting from 0.\n"
    "#   ZrnoBoy: palette frame resolution contrast brightness edges dither\n"
    "#   Print: style palette inks separation screen noise register flaws paper\n"
    "# Grade: grade=1..4 (the menu's Grade row), sharpening as Film, and the values\n"
    "#   the menu shows: contrast midtones shadows highlights blacks whites fade\n"
    "#   temperature tint saturation vibrance blackwhite (0 Off, 1 No filter .. 6 Blue)\n"
    "#   shadowtone highlighttone (0 Off, 1 Red .. 8 Magenta) shadowamount highlightamount\n"
    "#   and redhue redsaturation redlightness, the same for orange yellow green aqua\n"
    "#   blue purple magenta.\n";
static int presets_format(char *buf) {
    film_grade_settings neutral;char key[24];
    int n=sizeof(looks_header)-1,i,k,c;
    film_grade_neutral(&neutral);
    memcpy(buf,looks_header,n);
    for(i=0;i<preset_count;i++) {
        const preset_entry *p=&presets[i];
        n+=sprintf(buf+n,"%s|%s|",p->name,p->engine?engine_words[p->engine]:p->file);
        switch(p->engine){
        case ENGINE_FILM:
            for(k=0;k<PV_COUNT;k++){const char **names=pv_names(k,&c);n+=sprintf(buf+n,"%s%s=%s",k?" ":"",pv_keys[k],names[p->v[k]<c?p->v[k]:0]);}
            n+=sprintf(buf+n,"|%s",p->text);
            break;
        case ENGINE_GBOY:case ENGINE_PRINT:{
            const engine_key *t=p->engine==ENGINE_GBOY?gboy_keys:riso_keys;
            const unsigned char *in=p->engine==ENGINE_GBOY?(const unsigned char*)&p->gboy:(const unsigned char*)&p->riso;
            int keys=p->engine==ENGINE_GBOY?GBOY_KEYS:RISO_KEYS;
            for(k=0;k<keys;k++)n+=sprintf(buf+n,"%s%s=%d",k?" ":"",t[k].key,in[t[k].at]);
            break;}
        default:
            n+=sprintf(buf+n,"grade=%d sharpening=%s",p->slot+1,sharp_names[p->v[PV_SHARP]<4?p->v[PV_SHARP]:0]);
            for(k=0;k<FILM_GRADE_FIELDS;k++)if(p->grade.v[k]!=neutral.v[k]){grade_key(k,key);n+=sprintf(buf+n," %s=%s%d",key,p->grade.v[k]>0&&film_grade_min(k)<0?"+":"",p->grade.v[k]);   /* signed as the menu shows them */}
        }
        buf[n++]='\n';
    }
    return n;
}
/* SpyTask: write the list once the menu has finished an edit. A failed write
 * is reported and not retried; the next edit writes again. */
static void presets_store(void) {
    unsigned version=presets_version;char *buf;int n;
    if(version==presets_saved||(version&1u))return;
    buf=malloc(LOOKS_BYTES);
    if(!buf){status_text="Look save failed";presets_saved=version;return;}
    n=presets_format(buf);barrier();
    if(version!=presets_version){free(buf);return;}
    if(replace_file(LOOKS_FILE,"A/CHDK/FILM/LOOKS.TMP",buf,n))status_text="Look save failed";
    free(buf);presets_saved=version;publish_look_name();refresh_screen();
}
/* The browser tool (web/) writes LOOKS.TXT over USB and then this marker:
 * the looks are read again, checked at most once a second. A look edit
 * made on the camera and not yet saved wins; the marker then waits. */
#define LOOKS_RELOAD "A/CHDK/FILM/LOOKSRLD.TXT"
static void presets_reload_poll(void) {
    static unsigned last;unsigned now=(unsigned)get_tick_count();struct stat st;
    if(now-last<1000u)return;
    last=now;
    if(presets_version!=presets_saved||stat(LOOKS_RELOAD,&st))return;
    remove(LOOKS_RELOAD);
    presets_load();
    publish_look_name();refresh_screen();
}
static void presets_begin(void){presets_version++;barrier();}
static void presets_end(void){barrier();presets_version++;}
/* Recall: the look's engine, then its settings. */
static void preset_apply(int i) {
    const preset_entry *p=&presets[i];int lut=find_file(p->file);
    normalize_choices();
    film_gbui_set_engine(p->engine);
    preset_base=i;preset_armed=0;preset_none=0;
    switch(p->engine){
    case ENGINE_GBOY:film_gbui_set(&p->gboy);return;
    case ENGINE_PRINT:film_rsui_set(&p->riso);return;
    case ENGINE_GRADE:film_grui_set(p->slot,&p->grade);sharp_choice=p->v[PV_SHARP];request_selection();return;
    }
    if(lut>=0)choice=lut;
    halo_choice=p->v[PV_HALO];bloom_choice=p->v[PV_BLOOM];soft_choice=p->v[PV_SOFT];sharp_choice=p->v[PV_SHARP];grain_choice=p->v[PV_GRAIN];
    vig_choice=p->v[PV_VIG];leak_choice=p->v[PV_LEAK];toy_choice=p->v[PV_TOY];dust_choice=p->v[PV_DUST];
    if(halo_choice)hcol_choice=p->v[PV_HCOL];      /* a look without halation keeps the colour */
    stamp_choice=p->v[PV_STAMP];
    if(stamp_choice){smode_choice=p->v[PV_SMODE];if(smode_choice==2)strcpy(stext_choice,p->text);}
    request_selection();
}
/* The Look row steps through Custom and the saved looks, in a circle:
 * Custom, Look 1 ... Look N, Custom. Custom keeps the settings and leaves
 * every look (Update, Rename and Delete then have none to act on). */
const char *film_lut_menu_look(int change,int arg) {
    static char edited[PRESET_NAME+2];int i;
    (void)arg;
    if(!initialized)return "Starting";
    if(!preset_count)return "None saved";
    if(change){
        int from=preset_none?-1:preset_current(),places=preset_count+1;
        if(from<0&&!preset_none&&preset_base>=0&&preset_base<preset_count)from=preset_base;
        i=((from+1+change)%places+places)%places-1;
        if(i<0){preset_none=1;preset_base=-1;preset_armed=0;publish_preset();}
        else preset_apply(i);
    }
    if(preset_none)return "Custom";
    i=preset_current();
    if(i>=0)return presets[i].name;
    if(preset_base<0||preset_base>=preset_count)return "Custom";
    sprintf(edited,"%s*",presets[preset_base].name);return edited;
}
/* A new look's name: "Look N", N its place in the list counting from 1.
 * After a delete that name can still be taken ("Look 2" moved up to first
 * place): the next free number then. */
static void preset_autoname(char *name) {
    int i,k,taken;
    for(k=preset_count+1;;k++) {
        sprintf(name,"Look %d",k);
        for(taken=0,i=0;i<preset_count;i++)if(!strcmp(presets[i].name,name))taken=1;
        if(!taken)return;
    }
}
static void preset_named(const char *text) {
    char name[PRESET_NAME+1];
    if(!text||preset_base<0||preset_base>=preset_count)return;
    copy_text(name,text,PRESET_NAME,1);if(!name[0])return;
    presets_begin();strcpy(presets[preset_base].name,name);presets_end();
}
static char rename_buffer[PRESET_NAME+1];
void film_lut_look_action(int item) {
    preset_entry now;int cur;
    if(!initialized)return;
    normalize_choices();cur=preset_current();
    if(item!=FILM_LOOK_DELETE)preset_armed=0;
    switch(item){
    case FILM_LOOK_SAVE:
        if(preset_count>=MAX_PRESETS)return;
        preset_now(&now,1);preset_autoname(now.name);
        presets_begin();presets[preset_count]=now;preset_base=preset_count++;preset_none=0;presets_end();
        return;
    case FILM_LOOK_UPDATE:
        if(cur>=0||preset_base<0||preset_base>=preset_count)return;
        preset_now(&now,1);strcpy(now.name,presets[preset_base].name);
        presets_begin();presets[preset_base]=now;presets_end();
        return;
    case FILM_LOOK_RENAME:
        if(preset_base<0||preset_base>=preset_count)return;
        strcpy(rename_buffer,presets[preset_base].name);
        libtextbox->textbox_init((int)(size_t)"Look name",(int)(size_t)"Up to 20 characters",rename_buffer,PRESET_NAME,preset_named,0);
        return;
    case FILM_LOOK_DELETE:
        if(preset_base<0||preset_base>=preset_count)return;
        if(!preset_armed){preset_armed=1;return;}
        presets_begin();
        for(cur=preset_base;cur<preset_count-1;cur++)memcpy(&presets[cur],&presets[cur+1],sizeof(presets[0]));   /* one at a time: CHDK has no memmove */
        preset_count--;preset_base=-1;preset_armed=0;
        presets_end();
        return;
    }
}
void film_lut_look_disarm(void){preset_armed=0;}
int film_lut_looks_saved(void){return preset_count;}
const char *film_lut_look_label(int item,int *enabled) {
    static char label[40];int cur=initialized?preset_current():-1,base=preset_base>=0&&preset_base<preset_count;
    const char *name=base?presets[preset_base].name:"";
    *enabled=1;
    switch(item){
    case FILM_LOOK_SAVE:
        if(preset_count<MAX_PRESETS)return "Save as new look";
        *enabled=0;return "Looks full (16)";
    case FILM_LOOK_UPDATE:
        if(!base||cur>=0){*enabled=0;return "Update look";}
        sprintf(label,"Update %s",name);return label;
    case FILM_LOOK_RENAME:
        if(!base){*enabled=0;return "Rename look";}
        sprintf(label,"Rename %s",name);return label;
    default:
        if(!base){*enabled=0;return "Delete look";}
        if(preset_armed)return "SET again to delete";
        sprintf(label,"Delete %s",name);return label;
    }
}
