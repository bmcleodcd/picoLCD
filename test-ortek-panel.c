#include <assert.h>
#define ORTEK_MAP_PATH "tinybox-ortek.map"
#include "src/panel.c"
static unsigned taps,clicks,downs,ups,moves;
static int move_x,move_y;
int xdo_mouse_down(const xdo_t *x,Window w,int b) {(void)x;(void)w;assert(b==1||b==3);downs++;return 0;}
int xdo_mouse_up(const xdo_t *x,Window w,int b) {(void)x;(void)w;assert(b==1||b==3);ups++;return 0;}
int xdo_move_mouse_relative(const xdo_t *x,int dx,int dy) {(void)x;moves++;move_x=dx;move_y=dy;return 0;}
int xdo_send_keysequence_window(const xdo_t *x, Window w,const char *seq,useconds_t delay)
{ (void)x;(void)w;assert(seq[0] && delay==12000);taps++;return 0; }
int xdo_click_window(const xdo_t *x, Window w,int button)
{ (void)x;(void)w;assert(button==1 || button==3);clicks++;return 0; }
int main(void) {
 ortek_load_map();
 assert(strcmp(ir_bindings[3].sequence,"s")==0 && !ir_bindings[3].repeat);
 assert(strcmp(ir_bindings[21].sequence,"Up")==0);
 assert(strcmp(ir_bindings[32].sequence,"Down")==0);
 assert(strcmp(ir_bindings[34].sequence,"Return")==0);
 assert(strcmp(ir_bindings[40].sequence,"numbersign")==0);
 struct ir_context ctx={0};ctx.x=(xdo_t *)&ctx;ctx.dongle=1;
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
 ortek_mouse_output(&ctx,now);assert(downs==1 && moves==1 && move_x==-6 && !move_y);
 ortek_mouse_received(&ctx,28,1);ortek_mouse_output(&ctx,now+.025);assert(downs==1 && moves==2);
 ortek_mouse_output(&ctx,ctx.mouse.seen+.21);assert(ups==1 && moves==2 && !ctx.mouse_sent);
 ortek_mouse_received(&ctx,0,2);ortek_mouse_output(&ctx,ctx.mouse.seen);assert(downs==2);
 ctx.dongle=1;ortek_mouse_output(&ctx,ctx.mouse.seen+.01);assert(ups==2 && !ctx.mouse_sent);
 ctx.dongle=0;ortek_mouse_received(&ctx,0,3);ortek_mouse_output(&ctx,ctx.mouse.seen);assert(downs==4);
 ortek_mouse_release(&ctx);assert(ups==4 && !ctx.mouse_sent);
 printf("Mouse movement, held buttons, dongle suppression and cleanup passed\n");
 printf("46 mappings dispatch once each; dongle presence suppresses every duplicate\n");
}
