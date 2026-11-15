/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NATIVE_TEST_COMMON
#define NATIVE_TEST_COMMON
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 2
#define O_APPEND 4
#define O_TRUNC 8
struct mock_stat;
struct {int cam_uncached_bit;struct {int state_kbd_script_run,mode_play;} state;} camera_info;
struct {
 int bits_per_pixel,black_level,white_level,cfa_pattern;
 unsigned raw_rowpix,raw_rows,raw_rowlen,raw_size;
 struct {int x1,y1,x2,y2;} active_area;
} camera_sensor;
unsigned mock_stage_read(unsigned);
#define FILM_STAGE_READ(a) mock_stage_read(a)
int get_tick_count(void);
void msleep(int);
int is_video_recording(void);
int shooting_in_progress(void);
int remotecap_get_target(void);
unsigned GetFreeCardSpaceKb(void);
void dcache_clean_all(void);
int mock_stat(const char*,struct mock_stat*);
int mock_open(const char*,int,int);
int mock_write(int,const void*,unsigned);
int mock_read(int,void*,unsigned);
long get_property_case(long,void*,long);
int mock_close(int);
int mock_mkdir(const char*);
int mock_rename(const char*,const char*);
int mock_remove(const char*);
#define stat mock_stat
#define open mock_open
#define read mock_read
#define write mock_write
#define close mock_close
#define mkdir mock_mkdir
#define rename mock_rename
#define remove mock_remove
/* The stat macro applies to both the function and the structure name. */
struct mock_stat {unsigned st_size;};
#endif
