/*
 * Changes by bmcleodcd: 2026-10-18.
 * Modification/licence notice added by bmcleodcd: 2026-10-18.
 * This file is part of picoLCD and is distributed under the GNU General
 * Public License, version 2. See COPYING for the terms and warranty disclaimer.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ortek.h"
static unsigned counts[64][3];
static void received(void *ctx,unsigned key,unsigned phase) { (void)ctx; counts[key][phase]++; }
static unsigned synthetic_count,expected_key,expected_phase;
static void synthetic(void *ctx,unsigned key,unsigned phase) {
 (void)ctx; assert(key==expected_key && phase==expected_phase); synthetic_count++;
}
static void synthetic_tests(void) {
 for(unsigned key=0;key<64;key++) for(unsigned phase=0;phase<3;phase++) {
  unsigned word=10|(phase<<4)|(key<<6),sum=4;
  for(unsigned v=word;v;v>>=1)sum+=v&1;
  word|=sum<<12;
  for(unsigned corrupt=0;corrupt<2;corrupt++) {
   unsigned v=word^(corrupt<<12); int pulses[40]={-2000,1000,-500}; unsigned n=3;
   for(unsigned bit=0;bit<16;bit++) {
    int carrier=pulses[n-1]<0;
    if(((v>>bit)&1)!=(unsigned)carrier) {
     pulses[n-1]+=carrier?-500:500;
     pulses[n++]=carrier?500:-500;
    } else {
     pulses[n++]=carrier?500:-500;
     pulses[n++]=carrier?-500:500;
    }
   }
   unsigned char raw[80];
   for(unsigned i=0;i<n;i++) { raw[2*i]=pulses[i]&255;raw[2*i+1]=(pulses[i]>>8)&255; }
   expected_key=key;expected_phase=phase;
   for(unsigned split=2;split<2*n;split+=2) {
    struct ortek_decoder d={0};unsigned before=synthetic_count;
    ortek_feed(&d,raw,split,1,synthetic,NULL);
    ortek_feed(&d,raw+split,2*n-split,1.01,synthetic,NULL);
    assert(synthetic_count-before==(corrupt?0:1));
   }
  }
 }
 puts("All 64 commands, phases, packet splits and corrupt checksums passed");
}
int main(int argc,char **argv) {
 synthetic_tests();
 assert(argc==2); FILE *f=fopen(argv[1],"r"); assert(f);
 struct ortek_decoder d={0}; char line[1024];
 while(fgets(line,sizeof(line),f)) {
  double now; unsigned len; int offset;
  if(sscanf(line,"%lf type=1 bytes=%u%n",&now,&len,&offset)!=2) continue;
  unsigned char bytes[256]; assert(len<=sizeof(bytes));
  char *p=line+offset;
  for(unsigned i=0;i<len;i++) { unsigned b; int n; assert(sscanf(p," %x%n",&b,&n)==1); bytes[i]=b; p+=n; }
  ortek_feed(&d,bytes,len,now,received,NULL);
 }
 fclose(f);
 for(unsigned k=0;k<64;k++) if(counts[k][0]) printf("key %u: down=%u hold=%u up=%u\n",k,counts[k][0],counts[k][1],counts[k][2]);
 assert(counts[21][0]==5 && counts[21][2]==5);
 assert(counts[32][0]==5 && counts[32][2]==5);
 assert(counts[34][0]==5 && counts[34][2]==5);
 struct ortek_repeat r={0};
 assert(ortek_action(&r,21,0,1,1));
 assert(!ortek_action(&r,21,1,1.07,1));
 assert(!ortek_action(&r,21,1,1.14,1));
 assert(!ortek_action(&r,21,1,1.21,1));
 assert(!ortek_action(&r,21,1,1.28,1));
 assert(!ortek_action(&r,21,1,1.35,1));
 assert(ortek_action(&r,21,1,1.42,1));
 assert(!ortek_action(&r,21,2,1.45,1));
 assert(ortek_action(&r,21,0,1.5,1));
 assert(!ortek_action(&r,21,1,1.55,0));
 assert(!ortek_action(&r,21,2,1.6,0));
 puts("Ortek replay and repeat tests passed");
}
