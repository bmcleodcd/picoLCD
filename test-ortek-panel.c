#include <assert.h>
#define ORTEK_MAP_PATH "picolcd-ortek.map"
#include "src/panel.c"
static unsigned taps,clicks,downs,ups,moves;
static int move_x,move_y;
int panel_input_button(struct panel_input *in,int button,int down)
{(void)in;assert(button==1||button==3);if(down)downs++;else ups++;return 0;}
int panel_input_move(struct panel_input *in,int dx,int dy)
{(void)in;moves++;move_x=dx;move_y=dy;return 0;}
int panel_input_tap(struct panel_input *in,const char *seq)
{(void)in;if(!strncmp(seq,"mouse:",6))clicks++;else taps++;return 0;}
int main(void) {
 ortek_load_map();
 assert(strcmp(ir_bindings[3].sequence,"s")==0 && !ir_bindings[3].repeat);
 assert(strcmp(ir_bindings[21].sequence,"Up")==0);
 assert(strcmp(ir_bindings[32].sequence,"Down")==0);
 assert(strcmp(ir_bindings[34].sequence,"Return")==0);
 assert(strcmp(ir_bindings[40].sequence,"numbersign")==0);
 struct ir_context ctx={0};struct panel_input input={0};ctx.input=&input;ctx.dongle=1;
 unsigned mappings=0;
 for(unsigned k=0;k<64;k++) if(ir_bindings[k].sequence[0]) {
  mappings++;ortek_received(&ctx,k,0);ortek_received(&ctx,k,2);
 }
 assert(mappings==46 && !taps && !clicks);
 ctx.dongle=0;
 for(unsigned k=0;k<64;k++) if(ir_bindings[k].sequence[0]) {
  ortek_received(&ctx,k,0);ortek_received(&ctx,k,2);
 }
 assert(taps==45 && clicks==1);
 ctx.dongle=1;
 ortek_mouse_received(&ctx,28,1);ortek_mouse_output(&ctx,monotonic_seconds());
 assert(!downs && !ups && !moves);
 ctx.dongle=0;
 ortek_mouse_received(&ctx,28,1);double now=ctx.mouse.seen;
 ortek_mouse_output(&ctx,now);assert(downs==1 && moves==1 && move_x==-2 && !move_y);
 ortek_mouse_received(&ctx,28,1);ortek_mouse_output(&ctx,now+.025);assert(downs==1 && moves==2);
 ortek_mouse_output(&ctx,ctx.mouse.seen+.21);assert(ups==1 && moves==2 && !ctx.mouse_sent);
 ortek_mouse_received(&ctx,0,2);ortek_mouse_output(&ctx,ctx.mouse.seen);assert(downs==2);
 ctx.dongle=1;ortek_mouse_output(&ctx,ctx.mouse.seen+.01);assert(ups==2 && !ctx.mouse_sent);
 ctx.dongle=0;ortek_mouse_received(&ctx,0,3);ortek_mouse_output(&ctx,ctx.mouse.seen);assert(downs==4);
 ortek_mouse_release(&ctx);assert(ups==4 && !ctx.mouse_sent);
 printf("Mouse movement, held buttons, dongle suppression and cleanup passed\n");
 printf("46 mappings dispatch once each; dongle presence suppresses every duplicate\n");
}
