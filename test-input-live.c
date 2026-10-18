/*
 * Changes by bmcleodcd: 2026-10-18.
 * Modification/licence notice added by bmcleodcd: 2026-10-18.
 * This file is part of picoLCD and is distributed under the GNU General
 * Public License, version 2. See COPYING for the terms and warranty disclaimer.
 */
/* Live kernel test. Grab only the devices this process creates, so generated
 * events never reach Kodi/the desktop. Other input devices are untouched. */
#include <assert.h>
#include <stdlib.h>
#include "src/display-monitor.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <poll.h>
#include "src/input.h"
static int reader(int fd) {
 char name[128],path[256];assert(ioctl(fd,UI_GET_SYSNAME(sizeof(name)),name)>=0);
 for(int attempt=0;attempt<50;attempt++) {
  glob_t g;snprintf(path,sizeof(path),"/sys/devices/virtual/input/%s/event*",name);
  if(glob(path,0,NULL,&g)==0 && g.gl_pathc) {
   char *leaf=strrchr(g.gl_pathv[0],'/');char device[128];snprintf(device,sizeof(device),"/dev/input/%s",leaf+1);
   int r=open(device,O_RDONLY|O_NONBLOCK|O_CLOEXEC);globfree(&g);
   if(r>=0){assert(ioctl(r,EVIOCGRAB,1)==0);return r;}
  } else globfree(&g);
  usleep(20000);
 }
 return -1;
}
static void expect(int fd,unsigned type,unsigned code,int value) {
 struct pollfd p={.fd=fd,.events=POLLIN};struct input_event e;
 for(int i=0;i<20;i++) {
  assert(poll(&p,1,1000)>0);assert(read(fd,&e,sizeof(e))==sizeof(e));
  if(e.type==type && e.code==code && e.value==value)return;
 }
 assert(!"expected event missing");
}
int main(void) {
 struct panel_input in;assert(panel_input_open(&in)==0);
 int k=reader(in.keyboard),m=reader(in.mouse);assert(k>=0 && m>=0);
 assert(panel_input_tap(&in,"ctrl+shift+t")==0);
 expect(k,EV_KEY,KEY_LEFTCTRL,1);expect(k,EV_KEY,KEY_LEFTSHIFT,1);expect(k,EV_KEY,KEY_T,1);
 expect(k,EV_KEY,KEY_T,0);expect(k,EV_KEY,KEY_LEFTSHIFT,0);expect(k,EV_KEY,KEY_LEFTCTRL,0);
 assert(panel_input_move(&in,4,-2)==0);expect(m,EV_REL,REL_X,4);expect(m,EV_REL,REL_Y,-2);
 assert(panel_input_button(&in,1,1)==0);expect(m,EV_KEY,BTN_LEFT,1);
 assert(panel_input_button(&in,1,0)==0);expect(m,EV_KEY,BTN_LEFT,0);
 assert(panel_input_button(&in,3,1)==0);expect(m,EV_KEY,BTN_RIGHT,1);
 assert(panel_input_button(&in,3,0)==0);expect(m,EV_KEY,BTN_RIGHT,0);
 /* No X server is available to this worker; kernel input still works. */
 assert(setenv("DISPLAY",":65530",1)==0);
 struct display_monitor monitor={.fd=-1};struct display_mode mode;
 for(int i=0;i<10;i++) {
  assert(!display_monitor_poll(&monitor,i*.1,&mode));usleep(20000);
 }
 display_monitor_close(&monitor);
 assert(panel_input_tap(&in,"s")==0);expect(k,EV_KEY,KEY_S,1);expect(k,EV_KEY,KEY_S,0);
 close(k);close(m);panel_input_close(&in);
 puts("Live uinput keyboard/mouse and no-X-server isolation passed; test devices destroyed");
}
