/* 模拟动作握手和确认；不创建 HID 监听，也不注入 Windows 输入。 */
#include <assert.h>
#define main cb_application_main
#define cb_net_wait test_wait
#define cb_net_read test_read
#define cb_net_write test_write
#define cb_net_now test_now
#include "../src/main.c"
#undef main
#undef cb_net_wait
#undef cb_net_read
#undef cb_net_write
#undef cb_net_now
static int iteration,mode,stage,enabled_sent,action_sent,ready_seen,taken;
static cb_header writing;
static uint32_t action_sequence;
int test_wait(cb_socket s,int w,unsigned t) {(void)s;(void)w;(void)t;iteration++;return iteration<=2?1:-1;}
uint64_t test_now(void) {return iteration*100u;}
int test_read(cb_socket s,void *out,size_t n,unsigned t) {
 (void)s;(void)t;
 if(n==CB_HEADER_SIZE) {
  cb_header h=iteration==1?(cb_header){CB_CAPS_READY,0,1}:(cb_header){CB_CAPS_RESULT,mode==1?action_sequence+1:action_sequence,1};
  cb_encode_header(out,&h);return 0;
 }
 assert(n==1);*(unsigned char *)out=iteration==1?1:mode==2?0:1;return 0;
}
int test_write(cb_socket s,const void *p,size_t n,unsigned t) {
 (void)s;(void)t;
 if(!stage) {
  assert(n==CB_HEADER_SIZE && cb_decode_header(p,&writing)==0);
  if(writing.type==CB_CAPS_ENABLE) {enabled_sent++;return 0;}
  assert(writing.type==CB_CAPS_ACTION && writing.sequence==1 && ready_seen && writing.length==1);
  action_sequence=writing.sequence;stage=1;
 } else {assert(n==1 && *(const unsigned char *)p==CB_CAPS_SHORT);action_sent++;stage=0;}
 return 0;
}
int cb_keyboard_caps_enabled(void) {return 1;}
void cb_keyboard_caps_ready(int v) {ready_seen=v;}
unsigned cb_keyboard_caps_take(void) {assert(ready_seen);if(taken++)return 0;return CB_CAPS_SHORT;}
unsigned cb_keyboard_scroll_lines(void) {return 0;}
int cb_keyboard_init(int m) {(void)m;return 0;}
void cb_keyboard_close(void) {}
void cb_keyboard_connected(int v) {(void)v;}
int cb_keyboard_take_paste(void) {return 0;}
void cb_keyboard_finish_paste(int s) {(void)s;assert(0);}
int cb_platform_init(void) {return 0;}
void cb_platform_close(void) {}
void cb_platform_reset(void) {}
int cb_platform_poll(char **p,size_t *n) {*p=NULL;*n=0;return 0;}
int cb_platform_set(const char *p,size_t n) {(void)p;(void)n;return 0;}
int cb_platform_poll_content(cb_content *c) {memset(c,0,sizeof(*c));return 0;}
int cb_platform_set_content(const cb_content *c) {(void)c;return 0;}
int cb_platform_random(unsigned char *p,size_t n) {memset(p,0,n);return 0;}
int main(void) {
 for(mode=0;mode<3;mode++) {
  iteration=stage=enabled_sent=action_sent=ready_seen=taken=0;action_sequence=0;
  run_session(123);assert(enabled_sent==1 && action_sent==1 && !ready_seen);
 }
 puts("Caps session readiness, independent sequence and failure-result tests passed");
}

void cb_platform_cleanup_cache(void) {}
