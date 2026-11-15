/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../camera/film_pipe.h"
static unsigned count,reads;
static unsigned rd(unsigned a) {
 if(a==0x82350){reads++;return reads==1?2:10;}
 int valid=0;for(unsigned i=0;i<4;i++)if(a>=pipe_blocks[i].address&&a<pipe_blocks[i].address+4*pipe_blocks[i].count)valid=1;
 assert(valid);count++;return a;
}
int main(void){
 film_pipe_sample s;assert(sizeof(s)==528);
 film_pipe_snapshot(&s,12,12345,1,rd);
 assert(s.magic==PIPE_MAGIC&&s.version==1&&s.bytes==528&&s.raw_id==12&&s.tick==12345&&s.phase==1);
 assert(s.op_before==2&&s.op_after==10); /* concurrent state change must remain visible */
 assert(s.words[0]==0xb3ed4&&s.words[32]==0xb3f54&&s.words[40]==0x4e40&&s.words[76]==0x8231c);
 assert(count==123&&reads==3); /* word at operation offset also read inside the block */
 puts("Pipeline snapshot: fixed address bounds, record layout and detectable concurrent operation change passed");
}
