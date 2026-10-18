/*
 * Changes by bmcleodcd: 2026-10-18.
 * Modification/licence notice added by bmcleodcd: 2026-10-18.
 * This file is part of picoLCD and is distributed under the GNU General
 * Public License, version 2. See COPYING for the terms and warranty disclaimer.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "xbox-dvd.h"
static unsigned hits,keys[256],expected;
static int checking;
static void received(void *ctx,unsigned key)
{ (void)ctx;hits++;keys[key]++;if(checking)assert(key==expected); }
static size_t encode(unsigned key,unsigned char *raw,int distortion)
{
    unsigned v=(key<<12)|(key^0xfff);int widths[51]={-4000,4000};
    for(unsigned i=0;i<24;i++) {
        widths[2+2*i]=-500;widths[3+2*i]=(v&(1u<<(23-i)))?1000:2000;
    }
    widths[50]=-500;
    for(unsigned i=0;i<51;i++) {
        int w=widths[i]-distortion;
        raw[2*i]=w&255;raw[2*i+1]=(w>>8)&255;
    }
    return 102;
}
static void replay(const char *path)
{
    FILE *f=fopen(path,"r");assert(f);char line[1024];struct xbox_decoder d={0};
    while(fgets(line,sizeof(line),f)) {
        double now;unsigned len;int off;
        if(sscanf(line,"%lf type=1 bytes=%u%n",&now,&len,&off)!=2)continue;
        unsigned char raw[256];assert(len<=sizeof(raw));char *p=line+off;
        for(unsigned i=0;i<len;i++) {unsigned b;int n;assert(sscanf(p," %x%n",&b,&n)==1);raw[i]=b;p+=n;}
        xbox_feed(&d,raw,len,now,received,NULL);
    }
    fclose(f);
}
int main(int argc,char **argv)
{
    checking=1;
    for(unsigned key=0;key<256;key++)for(int distortion=0;distortion<=150;distortion+=75) {
        unsigned char raw[102];size_t len=encode(0xa00|key,raw,distortion);expected=key;
        for(size_t split=2;split<len;split+=2) {
            struct xbox_decoder d={0};unsigned before=hits;
            xbox_feed(&d,raw,split,1,received,NULL);
            xbox_feed(&d,raw+split,len-split,1.01,received,NULL);
            assert(hits==before+1);
        }
        unsigned before=hits;struct xbox_decoder d={0};
        xbox_feed(&d,raw,10,1,received,NULL);xbox_feed(&d,raw+10,len-10,2,received,NULL);
        assert(hits==before);
        /* No callback until the final mark is checked. */
        d=(struct xbox_decoder){0};xbox_feed(&d,raw,len-2,3,received,NULL);assert(hits==before);
        raw[len-2]=0;raw[len-1]=0;xbox_feed(&d,raw+len-2,2,3.01,received,NULL);assert(hits==before);
        /* Wrong address, checksum, timing and polarity are rejected. */
        len=encode(0x900|key,raw,distortion);d=(struct xbox_decoder){0};
        xbox_feed(&d,raw,len,4,received,NULL);assert(hits==before);
        len=encode(0xa00|key,raw,distortion);raw[98]=0xe8;raw[99]=3; /* force last bit to one */
        if(key&1) {d=(struct xbox_decoder){0};xbox_feed(&d,raw,len,5,received,NULL);assert(hits==before);}
        len=encode(0xa00|key,raw,distortion);raw[8]=0;raw[9]=0;d=(struct xbox_decoder){0};
        xbox_feed(&d,raw,len,6,received,NULL);assert(hits==before);
        encode(0xa00|key,raw,distortion);raw[4]=0xf4;raw[5]=1;d=(struct xbox_decoder){0};
        xbox_feed(&d,raw,len,7,received,NULL);assert(hits==before);
        encode(0xa00|key,raw,distortion);d=(struct xbox_decoder){0};
        xbox_feed(&d,raw,3,8,received,NULL);xbox_feed(&d,raw+4,len-4,8.01,received,NULL);assert(hits==before);
    }
    checking=0;hits=0;memset(keys,0,sizeof(keys));assert(argc==2);replay(argv[1]);
    printf("Xbox live frames=%u: up=%u down=%u left=%u right=%u select=%u back=%u play=%u stop=%u\n",
        hits,keys[0xa6],keys[0xa7],keys[0xa9],keys[0xa8],keys[0x0b],keys[0xd8],keys[0xea],keys[0xe0]);
    assert(hits==35 && keys[0xa6]==3 && keys[0xa7]==4 && keys[0xa9]==4 && keys[0xa8]==4 &&
        keys[0x0b]==5 && keys[0xd8]==5 && keys[0xea]==5 && keys[0xe0]==5);
    replay("test-data/hp-rc6-capture.log");replay("test-data/ortek-capture.log");assert(hits==35);
    struct xbox_repeat r={0};assert(xbox_action(&r,0xa6,1,1));
    for(unsigned i=1;i<=6;i++)assert(!xbox_action(&r,0xa6,1+i*.063,1));
    assert(xbox_action(&r,0xa6,1.441,1));assert(!xbox_action(&r,0xa6,1.504,1));
    assert(xbox_action(&r,0xa6,1.567,1));assert(xbox_action(&r,0xa7,1.58,1));
    assert(xbox_action(&r,0xa7,1.8,1));
    r=(struct xbox_repeat){0};assert(xbox_action(&r,0xea,1,0));
    for(unsigned i=1;i<30;i++)assert(!xbox_action(&r,0xea,1+i*.063,0));
    puts("Xbox: all commands, packet splits, bad frames, other remotes and repeat rules passed");
}
