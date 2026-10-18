/*
 * Changes by bmcleodcd: 2026-10-18.
 * Modification/licence notice added by bmcleodcd: 2026-10-18.
 * This file is part of picoLCD and is distributed under the GNU General
 * Public License, version 2. See COPYING for the terms and warranty disclaimer.
 */
#include <assert.h>
#include <stdio.h>
#include "rc6-mce.h"
static unsigned hits, keys[256], expected_key, expected_toggle;
static int checking;
static void received(void *ctx,unsigned key,unsigned toggle)
{
    (void)ctx;hits++;keys[key]++;
    if(checking) assert(key==expected_key && toggle==expected_toggle);
}
static size_t encode(uint32_t body,unsigned char *raw,int distortion)
{
    unsigned char bits[76]={1,0,1,0,1,0,0,1,0,0,1,1};
    for(unsigned i=0;i<32;i++) {
        bits[12+2*i]=(body>>(31-i))&1;
        bits[13+2*i]=!bits[12+2*i];
    }
    int widths[80]={-2664,888};unsigned count=2;
    for(unsigned i=0;i<76;) {
        unsigned j=i+1;while(j<76 && bits[j]==bits[i])j++;
        if(j==76 && !bits[i])break; /* Firmware omits final idle space. */
        widths[count++]=bits[i]?-(int)(j-i)*444:(int)(j-i)*444;i=j;
    }
    for(unsigned i=0;i<count;i++) {
        int v=widths[i]-distortion;
        raw[2*i]=v&255;raw[2*i+1]=(v>>8)&255;
    }
    return 2*count;
}
int main(int argc,char **argv)
{
    checking=1;
    for(unsigned key=0;key<256;key++)for(unsigned toggle=0;toggle<2;toggle++)
    for(int distortion=0;distortion<=75;distortion+=75) {
        unsigned char raw[160];size_t len=encode(0x800f0400u|(toggle<<15)|key,raw,distortion);
        expected_key=key;expected_toggle=toggle;
        for(size_t split=2;split<len;split+=2) {
            struct rc6_mce_decoder d={0};unsigned before=hits;
            rc6_mce_feed(&d,raw,split,1,received,NULL);
            rc6_mce_feed(&d,raw+split,len-split,1.01,received,NULL);
            assert(hits==before+1);
        }
        struct rc6_mce_decoder d={0};unsigned before=hits;
        rc6_mce_feed(&d,raw,10,1,received,NULL);
        rc6_mce_feed(&d,raw+10,len-10,2,received,NULL);
        assert(hits==before); /* Stale partial frames cannot join a new tap. */
        len=encode(0x800e0400u|key,raw,distortion);d=(struct rc6_mce_decoder){0};
        rc6_mce_feed(&d,raw,len,3,received,NULL);assert(hits==before);
        len=encode(0x800f0400u|key,raw,distortion);raw[8]=0;raw[9]=0;
        d=(struct rc6_mce_decoder){0};rc6_mce_feed(&d,raw,len,4,received,NULL);
        assert(hits==before); /* Invalid duration. */
    }
    checking=0;hits=0;memset(keys,0,sizeof(keys));
    assert(argc==2);FILE *f=fopen(argv[1],"r");assert(f);
    struct rc6_mce_decoder d={0};char line[1024];
    while(fgets(line,sizeof(line),f)) {
        double now;unsigned len;int offset;
        if(sscanf(line,"%lf type=1 bytes=%u%n",&now,&len,&offset)!=2)continue;
        unsigned char raw[256];assert(len<=sizeof(raw));char *p=line+offset;
        for(unsigned i=0;i<len;i++) {unsigned b;int n;assert(sscanf(p," %x%n",&b,&n)==1);raw[i]=b;p+=n;}
        rc6_mce_feed(&d,raw,len,now,received,NULL);
    }
    fclose(f);
    assert(hits==18 && keys[0x1e]==2 && keys[0x1f]==2 && keys[0x20]==3 &&
        keys[0x21]==2 && keys[0x22]==2 && keys[0x23]==2 && keys[0x16]==2 && keys[0x18]==3);
    struct rc6_mce_repeat r={0};
    assert(rc6_mce_action(&r,0x1e,0,1,1));
    for(int i=1;i<=4;i++)assert(!rc6_mce_action(&r,0x1e,0,1+i*.09,1));
    assert(rc6_mce_action(&r,0x1e,0,1.45,1));
    assert(rc6_mce_action(&r,0x1e,1,1.46,1)); /* A new tap toggles immediately. */
    r=(struct rc6_mce_repeat){0};assert(rc6_mce_action(&r,0x0c,0,1,0));
    for(int i=1;i<20;i++)assert(!rc6_mce_action(&r,0x0c,0,1+i*.1,0));
    puts("RC6: all commands/toggles/splits, invalid signals, repeat rules and 18 live frames passed");
}
