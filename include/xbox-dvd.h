#ifndef PICOLCD_XBOX_DVD_H
#define PICOLCD_XBOX_DVD_H
#include <stddef.h>
#include <stdint.h>
/* RCA DVD: 4ms leader mark/space, 24 MSB-first bits, 500us marks,
 * 1ms (one) or 2ms (zero) spaces, then a final mark. First 12 bits
 * contain the address/command, followed by their complement.
 * Observed picoLCD marks are extended by roughly 50-150us. */
struct xbox_decoder { unsigned state,count,value; double seen; };
typedef void (*xbox_callback)(void *,unsigned);
static inline void xbox_feed(struct xbox_decoder *d,const unsigned char *p,
    size_t len,double now,xbox_callback cb,void *ctx)
{
    if(!d || !p || !cb)return;
    if((len&1) || now-d->seen>0.3)d->state=0;
    d->seen=now;
    if(len&1)return;
    for(size_t i=0;i<len;i+=2) {
        int v=(int16_t)((unsigned)p[i]|((unsigned)p[i+1]<<8));
        if(v>=-4600 && v<=-3500) {
            d->state=1;d->count=0;d->value=0;continue;
        }
        if(!d->state)continue;
        if(d->state==1) {
            d->state=(v>=3400 && v<=4500)?2:0;continue;
        }
        if(d->state==2) {
            if(v> -350 || v< -850) {d->state=0;continue;}
            if(d->count==24) {
                unsigned key=d->value>>12;
                d->state=0;
                if((key^(d->value&0xfff))==0xfff && (key>>8)==0xa)
                    cb(ctx,key&255);
            } else d->state=3;
            continue;
        }
        unsigned bit;
        if(v>=650 && v<=1200)bit=1;
        else if(v>=1550 && v<=2250)bit=0;
        else {d->state=0;continue;}
        d->value=(d->value<<1)|bit;d->count++;d->state=2;
    }
}
struct xbox_repeat { unsigned key; int active; double seen,start,last; };
static inline int xbox_action(struct xbox_repeat *r,unsigned key,double now,int repeat)
{
    int fresh=!r->active || r->key!=key || now-r->seen>0.18;
    r->seen=now;
    if(fresh) {r->active=1;r->key=key;r->start=r->last=now;return 1;}
    if(repeat && now-r->start>=0.4 && now-r->last>=0.1) {r->last=now;return 1;}
    return 0;
}
#endif
