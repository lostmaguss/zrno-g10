/* SPDX-License-Identifier: GPL-2.0-or-later */
/* G12 1.00G pre-encoder LUT and bounded observations. GPL-2.0-or-later. */
#ifndef FILM_STAGE_H
#define FILM_STAGE_H
int film_stage_patch_requested(void);
void film_stage_init(int native_enabled);
void film_stage_arm(unsigned raw_id);
void film_stage_observe(unsigned phase);
void film_stage_finish(void);
void film_stage_poll(void);
/* 0 when the stage graded shot raw_id, else a negative reason
 * (-27: not armed for it, -28: develop not observed, or the patch's code). */
int film_stage_result(unsigned raw_id);
/* Research (FILM_DEVTRACE builds, with CHDK/FILM/DEVTRACE.TXT): called by
 * the develop task with each message it receives; the type and 192-byte
 * body of each go to CHDK/FILM/DEVTRACE.BIN once the task is quiet. Other
 * builds compile it empty. */
void film_develop_trace(const unsigned *msg);
/* Research hook: called with the developed frame before the colour pass. */
extern void (*film_stage_probe)(unsigned char *frame);
#endif
