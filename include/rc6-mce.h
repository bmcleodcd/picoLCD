/*
 * Changes by bmcleodcd: 2026-10-18.
 * Modification/licence notice added by bmcleodcd: 2026-10-18.
 * This file is part of picoLCD and is distributed under the GNU General
 * Public License, version 2. See COPYING for the terms and warranty disclaimer.
 */
#ifndef PICOLCD_RC6_MCE_H
#define PICOLCD_RC6_MCE_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* RC6 mode 6A: 444us half bits, double-width trailer, 32-bit MCE body.
 * picoLCD marks are negative; its demodulator extends marks by about 75us.
 * Frames span USB reports. The final idle space is not reported by firmware. */
struct rc6_mce_decoder {
    unsigned char halves[76];
    unsigned count, state;
    int last_mark;
    double last_packet;
};
typedef void (*rc6_mce_callback)(void *, unsigned, unsigned);
static inline void rc6_mce_feed(struct rc6_mce_decoder *d,
    const unsigned char *p,size_t len,double now,rc6_mce_callback cb,void *ctx)
{
    if(!d || !p || !cb) return;
    if((len&1) || now-d->last_packet>0.3) d->state=0;
    d->last_packet=now;
    if(len&1) return;
    for(size_t i=0;i<len;i+=2) {
        int v=(int16_t)((unsigned)p[i]|((unsigned)p[i+1]<<8));
        int mark=v<0;
        if(v<=-2300 && v>=-3100) {
            d->state=1;d->count=0;d->last_mark=1;continue;
        }
        if(!d->state) continue;
        if(mark==d->last_mark) {d->state=0;continue;}
        d->last_mark=mark;
        if(d->state==1) {
            if(mark || v<600 || v>1150) {d->state=0;continue;}
            d->state=2;continue;
        }
        int corrected=abs(v)+(mark?-75:75);
        int n=(corrected+222)/444;
        if(n<1 || n>3 || abs(corrected-n*444)>190 || d->count+(unsigned)n>76) {
            d->state=0;continue;
        }
        while(n--) d->halves[d->count++]=(unsigned char)mark;
        if(d->count==75 && d->halves[74]) d->halves[d->count++]=0;
        if(d->count!=76) continue;
        d->state=0;
        static const unsigned char prefix[12]={1,0,1,0,1,0,0,1,0,0,1,1};
        if(memcmp(d->halves,prefix,sizeof(prefix))) continue;
        uint32_t value=0;int valid=1;
        for(unsigned b=12;b<76;b+=2) {
            if(d->halves[b]==d->halves[b+1]) {valid=0;break;}
            value=(value<<1)|d->halves[b];
        }
        /* Restrict to Microsoft's customer and device codes. */
        if(valid && (value&0xffff7f00u)==0x800f0400u)
            cb(ctx,value&255,(value>>15)&1);
    }
}
struct rc6_mce_repeat { unsigned key,toggle; int active; double seen,start,last; };
static inline int rc6_mce_action(struct rc6_mce_repeat *r,unsigned key,
    unsigned toggle,double now,int repeatable)
{
    int fresh=!r->active || r->key!=key || r->toggle!=toggle || now-r->seen>0.3;
    r->seen=now;
    if(fresh) {
        r->active=1;r->key=key;r->toggle=toggle;r->start=r->last=now;return 1;
    }
    if(repeatable && now-r->start>=0.4 && now-r->last>=0.1) {r->last=now;return 1;}
    return 0;
}
#endif
