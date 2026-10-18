/*
 * Changes by bmcleodcd: 2026-10-18.
 * Modification/licence notice added by bmcleodcd: 2026-10-18.
 * This file is part of picoLCD and is distributed under the GNU General
 * Public License, version 2. See COPYING for the terms and warranty disclaimer.
 */
/* OrtekMCE as observed on VRC-1100: 2ms mark, 1ms space, Manchester
 * 16-bit LSB-first payload, checksum = 4 + popcount(low 12 bits).
 * Protocol reference: peterhinch/micropython_ir ir_rx/mce.py (MIT).
 * This implementation also validates polarity and both Manchester half bits.
 */
#include <stdint.h>
#include <stdlib.h>
#include "ortek.h"
static int near(int v, int lo, int hi) { return v >= lo && v <= hi; }
static void decode(struct ortek_decoder *d, ortek_callback cb, void *ctx)
{
    if(d->count<3 || !near(d->widths[0],-2300,-1800) ||
       !near(d->widths[1],800,1200)) return;
    unsigned x=2, bit=1, value=0;
    for(unsigned n=0;n<16;n++) {
        if(x>=d->count) return;
        int width=abs(d->widths[x]);
        if(!near(width,250,1250)) { d->count=0; return; }
        int short_bit=width<700;
        if(short_bit && n<15) {
            if(x+1>=d->count) return;
            if(!near(abs(d->widths[x+1]),250,700) ||
               (d->widths[x]<0)==(d->widths[x+1]<0)) { d->count=0; return; }
        }
        bit ^= !short_bit;
        /* Check the level at the start of this Manchester bit. */
        if((d->widths[x]<0) != (bit==(unsigned)short_bit)) { d->count=0; return; }
        value |= bit<<n;
        x += 1+short_bit;
    }
    unsigned sum=4;
    for(unsigned v=value&4095;v;v>>=1) sum+=v&1;
    d->count=0;
    if(sum!=(value>>12) || (value&15)!=10 || ((value>>4)&3)==3) return;
    cb(ctx,(value>>6)&63,(value>>4)&3);
}
void ortek_feed(struct ortek_decoder *d,const unsigned char *p,size_t len,
                double now,ortek_callback cb,void *ctx)
{
    if(!d || !p || !cb) return;
    if((len&1) || now-d->last_packet>0.25) d->count=0;
    d->last_packet=now;
    if(len&1) return;
    for(size_t i=0;i<len;i+=2) {
        int v=(int16_t)((unsigned)p[i]|((unsigned)p[i+1]<<8));
        if(near(v,-2300,-1800)) d->count=0;
        else if(!d->count) continue;
        if(d->count>=40 || (d->count && ((v<0)==(d->widths[d->count-1]<0)))) {
            d->count=0; continue;
        }
        d->widths[d->count++]=v;
        decode(d,cb,ctx);
    }
}
/* Emit complete key taps. A lost release can never leave an X key held. */
int ortek_action(struct ortek_repeat *r,unsigned key,unsigned phase,double now,int repeatable)
{
    if(phase==2) { if(r->active && r->key==key) r->active=0; return 0; }
    if(phase>2) return 0;
    int fresh=!r->active || r->key!=key || now-r->seen>0.3;
    r->seen=now;
    if(fresh) {
        r->active=1; r->key=key; r->started=r->last=now; return 1;
    }
    if(repeatable && now-r->started>=0.4 && now-r->last>=0.1) {
        r->last=now; return 1;
    }
    return 0;
}

/* Mouse reports have the same leader but a single 480us header space,
 * fixed address 20, two button bits, a five-bit movement field and checksum.
 * Movement is 0 (stop) or 16+angle, clockwise from up in 22.5 degree steps.
 * Determined from synchronized picoLCD/Ortek USB captures on 2026-10-18.
 */
static void mouse_decode(struct ortek_decoder *d,ortek_callback cb,void *ctx)
{
    if(d->count<3 || !near(d->widths[1],250,700)) return;
    unsigned levels[64],nlevels=0;
    for(unsigned i=2;i<d->count;i++) {
        int width=abs(d->widths[i]);
        if(!near(width,250,1250)) { d->count=0;return; }
        unsigned units=width<700?1:2;
        if(nlevels+units>32) { d->count=0;return; }
        while(units--) levels[nlevels++]=d->widths[i]>0;
    }
    if(nlevels<31) return;
    /* Last zero ends in an unmeasured idle space; its mark is sufficient. */
    if(nlevels==31) {
        if(levels[30]!=0) return;
        levels[nlevels++]=1;
    }
    unsigned word=0;
    for(unsigned i=0;i<32;i+=2) {
        if(levels[i]==levels[i+1]) { d->count=0;return; }
        word|=levels[i]<<(i/2);
    }
    unsigned sum=3;
    for(unsigned v=word&4095;v;v>>=1) sum+=v&1;
    d->count=0;
    unsigned direction=(word>>7)&31;
    if((word&31)!=20 || sum!=(word>>12) || (direction && direction<16)) return;
    cb(ctx,direction,(word>>5)&3);
}
void ortek_mouse_feed(struct ortek_decoder *d,const unsigned char *p,size_t len,
                      double now,ortek_callback cb,void *ctx)
{
    if(!d || !p || !cb) return;
    if((len&1) || now-d->last_packet>0.25) d->count=0;
    d->last_packet=now;
    if(len&1) return;
    for(size_t i=0;i<len;i+=2) {
        int v=(int16_t)((unsigned)p[i]|((unsigned)p[i+1]<<8));
        if(near(v,-2300,-1800)) d->count=0;
        else if(!d->count) continue;
        if(d->count>=40 || (d->count && ((v<0)==(d->widths[d->count-1]<0)))) {
            d->count=0;continue;
        }
        d->widths[d->count++]=v;
        if(d->count==2 && !near(v,250,700)) { d->count=0;continue; }
        mouse_decode(d,cb,ctx);
    }
}
void ortek_mouse_update(struct ortek_mouse_state *s,unsigned direction,unsigned buttons,double now)
{
    if((direction && (direction<16 || direction>31)) || buttons>3) return;
    if(!s->direction || now-s->seen>0.2) { s->started=now;s->next_step=now; }
    s->direction=direction;s->buttons=buttons;s->seen=now;
}
void ortek_mouse_step(struct ortek_mouse_state *s,double now,int *dx,int *dy,unsigned *buttons)
{
    static const int xs[16]={0,1,2,2,2,2,2,1,0,-1,-2,-2,-2,-2,-2,-1};
    static const int ys[16]={-2,-2,-2,-1,0,1,2,2,2,2,2,1,0,-1,-2,-2};
    *dx=*dy=0;
    if(now-s->seen>0.2) { s->direction=0;s->buttons=0; }
    *buttons=s->buttons;
    if(!s->direction || now<s->next_step) return;
    double held=now-s->started;
    int gain=held<0.696?1:held<0.992?2:held<1.288?3:4;
    /* Native input now receives the same raw steps as the USB receiver. */
    const int pointer_scale=1;
    *dx=xs[s->direction&15]*gain*pointer_scale;
    *dy=ys[s->direction&15]*gain*pointer_scale;
    s->next_step=now+0.024;
}
