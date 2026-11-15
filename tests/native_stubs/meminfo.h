/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host stub of CHDK meminfo.h: fixed figures for the memory log. */
#ifndef MEMINFO_H
#define MEMINFO_H
typedef struct {int start_address,end_address,total_size,allocated_size,allocated_peak,allocated_count,free_size,free_block_max_size,free_block_count;} cam_meminfo;
static inline void GetCombinedMemInfo(cam_meminfo *m){*m=(cam_meminfo){0};m->free_size=300000;m->free_block_max_size=250000;}
#endif
