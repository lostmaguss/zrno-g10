/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GPL-2.0-or-later. Bounded reads of verified G12 1.00G descriptors.
 * SpyTask polling cannot guarantee observing a transient processing stage. */
#ifndef FILM_PIPE_H
#define FILM_PIPE_H
#define PIPE_MAGIC 0x50495045u
#define PIPE_WORDS 124
#define PIPE_SAMPLES 8
typedef struct {
    unsigned magic,version,bytes,raw_id,tick,op_before,op_after,phase;
    unsigned words[PIPE_WORDS];
} film_pipe_sample;
typedef struct {unsigned address,count;} film_pipe_block;
static const film_pipe_block pipe_blocks[4]={
    {0xb3ed4,32},{0xb3f54,8},{0x4e40,36},{0x8231c,48}
};
static void film_pipe_snapshot(film_pipe_sample *s,unsigned id,unsigned tick,
                               unsigned phase,unsigned (*read_word)(unsigned)) {
    unsigned i,j,n=0;
    s->magic=PIPE_MAGIC;s->version=1;s->bytes=sizeof(*s);
    s->raw_id=id;s->tick=tick;s->phase=phase;
    s->op_before=read_word(0x82350);
    for(i=0;i<4;i++)for(j=0;j<pipe_blocks[i].count;j++)
        s->words[n++]=read_word(pipe_blocks[i].address+4*j);
    s->op_after=read_word(0x82350);
}
#endif
