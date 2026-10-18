#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include "src/input.h"
static struct input_event events[512];
static size_t read_events(int fd) {
 ssize_t n=read(fd,events,sizeof(events));assert(n>=0 && n%sizeof(events[0])==0);return n/sizeof(events[0]);
}
static void expect(size_t n,unsigned type,unsigned code,int value) {
 assert(events[n].type==type && events[n].code==code && events[n].value==value);
}
int main(void) {
 unsigned short codes[8];size_t count=8;
 assert(setenv("PICOLCD_KEYBOARD_LAYOUT","gb",1)==0);
 assert(panel_input_parse("numbersign",codes,&count)==0 && count==1 && codes[0]==KEY_BACKSLASH);
 assert(setenv("PICOLCD_KEYBOARD_LAYOUT","us",1)==0);count=8;
 assert(panel_input_parse("ctrl+shift+t",codes,&count)==0 && count==3 && codes[0]==KEY_LEFTCTRL && codes[1]==KEY_LEFTSHIFT && codes[2]==KEY_T);
 count=8;assert(panel_input_parse("numbersign",codes,&count)==0 && count==2 && codes[0]==KEY_LEFTSHIFT && codes[1]==KEY_3);
 const char *bad[]={"","ctrl+","+s","ctrl+ctrl+s","$(touch /tmp/no)","unknown", "ctrl+shift+alt+super+a+b+c+d+e"};
 for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++){count=8;assert(panel_input_parse(bad[i],codes,&count)<0);}
 FILE *f=fopen("picolcd-ortek.map","r");assert(f);char line[256],seq[96];unsigned key;int repeat,maps=0;
 while(fgets(line,sizeof(line),f))if(sscanf(line,"%u %d %95s",&key,&repeat,seq)==3) {
  if(strncmp(seq,"mouse:",6)){count=8;assert(panel_input_parse(seq,codes,&count)==0);}maps++;
 }
 fclose(f);assert(maps==46);
 int keyboard[2],mouse[2];assert(pipe(keyboard)==0 && pipe(mouse)==0);
 struct panel_input in={.keyboard=keyboard[1],.mouse=mouse[1]};
 assert(panel_input_tap(&in,"ctrl+shift+t")==0);assert(read_events(keyboard[0])==8);
 expect(0,EV_KEY,KEY_LEFTCTRL,1);expect(1,EV_KEY,KEY_LEFTSHIFT,1);expect(2,EV_KEY,KEY_T,1);
 expect(3,EV_SYN,SYN_REPORT,0);expect(4,EV_KEY,KEY_T,0);expect(5,EV_KEY,KEY_LEFTSHIFT,0);expect(6,EV_KEY,KEY_LEFTCTRL,0);
 assert(panel_input_hold(&in,"ctrl",1)==0);read_events(keyboard[0]);
 assert(panel_input_tap(&in,"ctrl+s")==0);assert(read_events(keyboard[0])==4);
 expect(0,EV_KEY,KEY_S,1);expect(2,EV_KEY,KEY_S,0);assert(in.refs[KEY_LEFTCTRL]==1);
 assert(panel_input_hold(&in,"ctrl",0)==0);assert(read_events(keyboard[0])==2);expect(0,EV_KEY,KEY_LEFTCTRL,0);
 assert(panel_input_tap(&in,"numbersign")==0);assert(read_events(keyboard[0])==6);
 expect(0,EV_KEY,KEY_LEFTSHIFT,1);expect(1,EV_KEY,KEY_3,1);expect(3,EV_KEY,KEY_3,0);expect(4,EV_KEY,KEY_LEFTSHIFT,0);
 assert(panel_input_button(&in,1,1)==0);assert(panel_input_move(&in,-2,1)==0);assert(panel_input_button(&in,1,0)==0);
 assert(read_events(mouse[0])==7);expect(0,EV_KEY,BTN_LEFT,1);expect(2,EV_REL,REL_X,-2);expect(3,EV_REL,REL_Y,1);expect(5,EV_KEY,BTN_LEFT,0);
 assert(panel_input_button(&in,3,1)==0);read_events(mouse[0]);
 assert(panel_input_tap(&in,"mouse:3")==0);assert(read_events(mouse[0])==2);
 expect(0,EV_SYN,SYN_REPORT,0);assert(in.mouse_refs[1]==1);
 assert(panel_input_button(&in,3,0)==0);assert(read_events(mouse[0])==2);expect(0,EV_KEY,BTN_RIGHT,0);
 close(keyboard[1]);in.keyboard=-1;assert(panel_input_tap(&in,"s")<0);
 in.keyboard=open("/dev/null",O_RDONLY);in.failed=0;assert(panel_input_tap(&in,"s")<0 && in.failed);
 panel_input_close(&in);close(keyboard[0]);close(mouse[0]);
 puts("uinput: 46 mappings, malformed chords, event order, shared modifiers, mouse drag and write-failure cleanup passed");
}
