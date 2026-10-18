/*
 * Changes by bmcleodcd: 2026-10-18.
 * Modification/licence notice added by bmcleodcd: 2026-10-18.
 * This file is part of picoLCD and is distributed under the GNU General
 * Public License, version 2. See COPYING for the terms and warranty disclaimer.
 */
/* Linux uinput keyboard/mouse backend; no display connection is required. */
#include "input.h"
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
struct named_key { const char *name; unsigned short code; };
static const struct named_key names[]={
 {"ctrl",KEY_LEFTCTRL},{"shift",KEY_LEFTSHIFT},{"alt",KEY_LEFTALT},{"super",KEY_LEFTMETA},
 {"Escape",KEY_ESC},{"BackSpace",KEY_BACKSPACE},{"Tab",KEY_TAB},{"Return",KEY_ENTER},
 {"space",KEY_SPACE},{"Left",KEY_LEFT},{"Right",KEY_RIGHT},{"Up",KEY_UP},{"Down",KEY_DOWN},
 {"Prior",KEY_PAGEUP},{"Next",KEY_PAGEDOWN},{"Home",KEY_HOME},{"End",KEY_END},
 {"Insert",KEY_INSERT},{"Delete",KEY_DELETE},{"Pause",KEY_PAUSE},
 {"XF86HomePage",KEY_HOMEPAGE},{"XF86AudioRaiseVolume",KEY_VOLUMEUP},
 {"XF86AudioLowerVolume",KEY_VOLUMEDOWN},{"XF86AudioMute",KEY_MUTE},
 {"XF86AudioPlay",KEY_PLAYPAUSE},{"XF86AudioStop",KEY_STOPCD},
 {"XF86AudioNext",KEY_NEXTSONG},{"XF86AudioPrev",KEY_PREVIOUSSONG},
 {"XF86AudioForward",KEY_FASTFORWARD},{"XF86AudioRewind",KEY_REWIND},
 {"KP_Multiply",KEY_KPASTERISK},{"KP_Divide",KEY_KPSLASH},
 {"KP_Add",KEY_KPPLUS},{"KP_Subtract",KEY_KPMINUS},{"KP_Enter",KEY_KPENTER},
 {"KP_Decimal",KEY_KPDOT},
 {"F1",KEY_F1},{"F2",KEY_F2},{"F3",KEY_F3},{"F4",KEY_F4},{"F5",KEY_F5},
 {"F6",KEY_F6},{"F7",KEY_F7},{"F8",KEY_F8},{"F9",KEY_F9},{"F10",KEY_F10},
 {"F11",KEY_F11},{"F12",KEY_F12}
};
/* Native keycodes follow the OS keyboard layout. UK # is the unshifted
 * backslash-position key, whereas US # is Shift+3. No X connection is needed. */
static int layout_is_gb(void)
{
 const char *override=getenv("PICOLCD_KEYBOARD_LAYOUT");
 if(override)return !strcmp(override,"gb");
 static int cached=-1;
 if(cached<0) {
  cached=0;FILE *f=fopen("/etc/default/keyboard","r");char line[256];
  if(f) {
   while(fgets(line,sizeof(line),f))if(!strncmp(line,"XKBLAYOUT=",10)) {
    char *v=line+10;while(*v==' ' || *v=='\"' || *v=='\'')v++;
    cached=!strncmp(v,"gb",2) && (v[2]=='\"' || v[2]=='\'' || v[2]==',' || v[2]=='\n' || !v[2]);
   }
   fclose(f);
  }
 }
 return cached;
}
int panel_input_parse(const char *sequence,unsigned short *out,size_t *count)
{
 static const unsigned short letters[]={KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
 static const unsigned short digits[]={KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9};
 static const unsigned short keypad[]={KEY_KP0,KEY_KP1,KEY_KP2,KEY_KP3,KEY_KP4,KEY_KP5,KEY_KP6,KEY_KP7,KEY_KP8,KEY_KP9};
 char buffer[96];size_t used=0;
 if(!sequence || !out || !count || !sequence[0] || strlen(sequence)>=sizeof(buffer))return -1;
 strcpy(buffer,sequence);char *p=buffer;
 while(p) {
  char *next=strchr(p,'+');if(next)*next++=0;
  unsigned short code=0;
  if(strlen(p)==1 && *p>='a' && *p<='z')code=letters[*p-'a'];
  else if(strlen(p)==1 && *p>='0' && *p<='9')code=digits[*p-'0'];
  else if(strlen(p)==4 && !strncmp(p,"KP_",3) && p[3]>='0' && p[3]<='9')code=keypad[p[3]-'0'];
  else if(!strcmp(p,"numbersign")) {
   if(layout_is_gb())code=KEY_BACKSLASH;
   else {
    if(used+2>*count)return -1;
    out[used++]=KEY_LEFTSHIFT;code=KEY_3;
   }
  } else for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++)if(!strcmp(p,names[i].name)){code=names[i].code;break;}
  if(!code || used>=*count)return -1;
  for(size_t i=0;i<used;i++)if(out[i]==code)return -1;
  out[used++]=code;p=next;
 }
 *count=used;return 0;
}
static int emit(struct panel_input *in,int fd,unsigned short type,unsigned short code,int value)
{
 struct input_event e={0};e.type=type;e.code=code;e.value=value;
 if(fd<0 || in->failed)return -1;
 ssize_t n;do{n=write(fd,&e,sizeof(e));}while(n<0 && errno==EINTR);
 if(n!=(ssize_t)sizeof(e)){in->failed=1;return -1;}return 0;
}
static int sync_input(struct panel_input *in,int fd){return emit(in,fd,EV_SYN,SYN_REPORT,0);}
static int create_device(int mouse)
{
 int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC);if(fd<0)return -1;
 if(ioctl(fd,UI_SET_EVBIT,EV_KEY)<0)goto fail;
 if(mouse) {
  if(ioctl(fd,UI_SET_KEYBIT,BTN_LEFT)<0 || ioctl(fd,UI_SET_KEYBIT,BTN_RIGHT)<0 ||
     ioctl(fd,UI_SET_EVBIT,EV_REL)<0 || ioctl(fd,UI_SET_RELBIT,REL_X)<0 || ioctl(fd,UI_SET_RELBIT,REL_Y)<0)goto fail;
 } else {
  /* Advertise only keys accepted by the configuration parser. */
  for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++)
   if(ioctl(fd,UI_SET_KEYBIT,names[i].code)<0)goto fail;
  const char *chars="abcdefghijklmnopqrstuvwxyz0123456789";
  for(const char *p=chars;*p;p++) {
   char sequence[2]={*p,0};unsigned short code[8];size_t count=8;
   if(panel_input_parse(sequence,code,&count)<0 || ioctl(fd,UI_SET_KEYBIT,code[0])<0)goto fail;
  }
  for(int digit=0;digit<10;digit++) {
   char sequence[8];unsigned short code[8];size_t count=8;
   snprintf(sequence,sizeof(sequence),"KP_%d",digit);
   if(panel_input_parse(sequence,code,&count)<0 || ioctl(fd,UI_SET_KEYBIT,code[0])<0)goto fail;
  }
  if(ioctl(fd,UI_SET_KEYBIT,KEY_BACKSLASH)<0 || ioctl(fd,UI_SET_EVBIT,EV_REP)<0)goto fail;
 }
 struct uinput_setup setup={0};setup.id.bustype=BUS_VIRTUAL;
 setup.id.vendor=0x04d8;setup.id.product=0x0002;setup.id.version=1;
 snprintf(setup.name,sizeof(setup.name),"picoLCD Virtual %s",mouse?"Mouse":"Keyboard");
 if(ioctl(fd,UI_SET_PHYS,mouse?"picolcd/input1":"picolcd/input0")<0 ||
    ioctl(fd,UI_DEV_SETUP,&setup)<0 || ioctl(fd,UI_DEV_CREATE)<0)goto fail;
 return fd;
fail: {int saved=errno;close(fd);errno=saved;return -1;}
}
int panel_input_open(struct panel_input *in)
{
 memset(in,0,sizeof(*in));in->keyboard=in->mouse=-1;
 in->keyboard=create_device(0);if(in->keyboard<0)return -1;
 in->mouse=create_device(1);
 if(in->mouse<0){int saved=errno;panel_input_close(in);errno=saved;return -1;}
 return 0;
}
void panel_input_close(struct panel_input *in)
{
 if(in->keyboard>=0){ioctl(in->keyboard,UI_DEV_DESTROY);close(in->keyboard);}
 if(in->mouse>=0){ioctl(in->mouse,UI_DEV_DESTROY);close(in->mouse);}
 memset(in,0,sizeof(*in));in->keyboard=in->mouse=-1;
}
int panel_input_hold(struct panel_input *in,const char *sequence,int down)
{
 unsigned short codes[8];size_t n=8;
 if(panel_input_parse(sequence,codes,&n)<0)return -1;
 for(size_t i=0;i<n;i++) {
  unsigned short code=codes[down?i:n-1-i];
  if(down){if(!in->refs[code]++ && emit(in,in->keyboard,EV_KEY,code,1)<0)return -1;}
  else if(in->refs[code] && !--in->refs[code] && emit(in,in->keyboard,EV_KEY,code,0)<0)return -1;
 }
 return sync_input(in,in->keyboard);
}
int panel_input_tap(struct panel_input *in,const char *sequence)
{
 if(!strcmp(sequence,"mouse:1") || !strcmp(sequence,"mouse:3")) {
  int button=sequence[6]-'0';
  if(panel_input_button(in,button,1)<0)return -1;
  return panel_input_button(in,button,0);
 }
 if(panel_input_hold(in,sequence,1)<0)return -1;
 return panel_input_hold(in,sequence,0);
}
int panel_input_button(struct panel_input *in,int button,int down)
{
 if(button!=1 && button!=3)return -1;
 unsigned short *refs=&in->mouse_refs[button==1?0:1];
 if(down) {
  if(!(*refs)++ && emit(in,in->mouse,EV_KEY,button==1?BTN_LEFT:BTN_RIGHT,1)<0)return -1;
 } else if(*refs && !--(*refs) && emit(in,in->mouse,EV_KEY,button==1?BTN_LEFT:BTN_RIGHT,0)<0)return -1;
 return sync_input(in,in->mouse);
}
int panel_input_move(struct panel_input *in,int dx,int dy)
{
 if((dx && emit(in,in->mouse,EV_REL,REL_X,dx)<0) ||
    (dy && emit(in,in->mouse,EV_REL,REL_Y,dy)<0))return -1;
 return sync_input(in,in->mouse);
}
