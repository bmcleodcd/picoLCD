#ifndef PICOLCD_INPUT_H
#define PICOLCD_INPUT_H
#include <linux/input.h>
#include <stddef.h>
struct panel_input { int keyboard,mouse,failed; unsigned short refs[KEY_CNT],mouse_refs[2]; };
int panel_input_parse(const char *,unsigned short *,size_t *);
int panel_input_open(struct panel_input *);
void panel_input_close(struct panel_input *);
int panel_input_hold(struct panel_input *,const char *,int);
int panel_input_tap(struct panel_input *,const char *);
int panel_input_button(struct panel_input *,int,int);
int panel_input_move(struct panel_input *,int,int);
#endif
