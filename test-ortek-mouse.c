#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ortek.h"
static unsigned counts[32][4],hits,expected_direction,expected_buttons;
static void received(void *ctx,unsigned dir,unsigned buttons) { (void)ctx;assert(dir<32 && buttons<4);counts[dir][buttons]++;hits++; }
static void checked(void *ctx,unsigned dir,unsigned buttons) { (void)ctx;assert(dir==expected_direction && buttons==expected_buttons);hits++; }
static void synthetic_tests(void) {
 for(unsigned dir=0;dir<32;dir++) {
  if(dir>0 && dir<16)continue;
  for(unsigned buttons=0;buttons<4;buttons++) for(unsigned corrupt=0;corrupt<2;corrupt++) {
   unsigned word=20|(buttons<<5)|(dir<<7),sum=3;
   for(unsigned v=word;v;v>>=1)sum+=v&1;
   word|=sum<<12;word^=corrupt<<12;
   int widths[40]={-2000,500};unsigned n=2;
   for(unsigned bit=0;bit<16;bit++) for(unsigned half=0;half<2;half++) {
    int val=(((word>>bit)&1)^half)?500:-500;
    if((val<0)==(widths[n-1]<0))widths[n-1]+=val;
    else widths[n++]=val;
   }
   if(widths[n-1]>0)n--; /* Idle space not reported by the panel. */
   unsigned char raw[80];
   for(unsigned i=0;i<n;i++) {raw[2*i]=widths[i]&255;raw[2*i+1]=(widths[i]>>8)&255;}
   expected_direction=dir;expected_buttons=buttons;
   for(unsigned split=2;split<n*2;split+=2) {
    struct ortek_decoder d={0};unsigned before=hits;
    ortek_mouse_feed(&d,raw,split,1,checked,NULL);
    ortek_mouse_feed(&d,raw+split,2*n-split,1.01,checked,NULL);
    assert(hits-before==(corrupt?0:1));
   }
  }
 }
 puts("All mouse directions/buttons, packet splits and corrupt checksums passed");
}
static void replay(const char *path) {
 FILE *f=fopen(path,"r");assert(f);char line[1024];struct ortek_decoder d={0};
 while(fgets(line,sizeof(line),f)) {
  double now;unsigned len;int offset;
  if(sscanf(line,"%lf type=1 bytes=%u%n",&now,&len,&offset)!=2)continue;
  unsigned char raw[256];assert(len<=sizeof(raw));char *p=line+offset;
  for(unsigned i=0;i<len;i++) {unsigned b;int n;assert(sscanf(p," %x%n",&b,&n)==1);raw[i]=b;p+=n;}
  ortek_mouse_feed(&d,raw,len,now,received,NULL);
 }
 fclose(f);
}
int main(void) {
 synthetic_tests();hits=0;
 replay("test-data/ortek-capture.log");assert(hits==0); /* Keyboard never becomes motion. */
 replay("test-data/ortek-mouse-capture.log");
 printf("Mouse replay: %u frames, left %u, right %u, up %u, down %u, stops %u\n",hits,counts[28][0],counts[20][0],counts[16][0],counts[24][0],counts[0][0]);
 assert(counts[28][0]==21 && counts[20][0]==1 && counts[16][0]==12 && counts[24][0]==18);
 assert(counts[0][0]==10 && counts[0][1]==5 && counts[0][2]==5);
 assert(counts[30][0]==12 && counts[18][0]==9 && counts[22][0]==5 && counts[26][0]==5);
 struct ortek_mouse_state s={0};int dx,dy;unsigned buttons;
 ortek_mouse_update(&s,28,1,1);ortek_mouse_step(&s,1,&dx,&dy,&buttons);assert(dx==-2 && dy==0 && buttons==1);
 ortek_mouse_step(&s,1.01,&dx,&dy,&buttons);assert(!dx && !dy && buttons==1);
 for(int i=1;i<=14;i++)ortek_mouse_update(&s,28,1,1+i*.1);
 ortek_mouse_step(&s,2.4,&dx,&dy,&buttons);assert(dx==-8 && dy==0 && buttons==1);
 ortek_mouse_step(&s,2.61,&dx,&dy,&buttons);assert(!dx && !dy && buttons==0);
 ortek_mouse_update(&s,18,3,3);ortek_mouse_step(&s,3,&dx,&dy,&buttons);assert(dx==2 && dy==-2 && buttons==3);
 ortek_mouse_update(&s,0,0,3.1);ortek_mouse_step(&s,3.1,&dx,&dy,&buttons);assert(!dx && !dy && !buttons);
 puts("Mouse acceleration, diagonal, pacing, buttons, stop and lost-release timeout passed");
}
