/* 真实主程序会话的网络模拟：认证后的能力协商、续租和释放。
 * 不读取剪贴板，不向系统发送输入。 */
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
static int step, capability, writes, body_pending;
static unsigned requests[8];
int test_wait(cb_socket s,int w,unsigned t) {
 (void)s;(void)w;(void)t;++step;
 if(step>5) return -1;
 return step==2 && capability ? 1 : 0;
}
uint64_t test_now(void) {return (uint64_t)step*100;}
int test_read(cb_socket s,void *out,size_t n,unsigned t) {
 (void)s;(void)t;assert(n==CB_HEADER_SIZE);
 cb_header h={CB_SCROLL_CAPS,0,0};cb_encode_header(out,&h);return 0;
}
int test_write(cb_socket s,const void *data,size_t n,unsigned t) {
 (void)s;(void)t;
 if(!body_pending) {
  cb_header h;assert(n==CB_HEADER_SIZE && cb_decode_header(data,&h)==0);
  assert(h.type==CB_SCROLL_LINES && h.sequence==0 && h.length==1);body_pending=1;
 } else {assert(n==1);requests[writes++]=*(const unsigned char*)data;body_pending=0;}
 return 0;
}
int cb_platform_init(void) {return 0;}
void cb_platform_close(void) {}
void cb_platform_reset(void) {}
int cb_platform_poll(char **text,size_t *n) {*text=NULL;*n=0;return 0;}
int cb_platform_set(const char *text,size_t n) {(void)text;(void)n;return 0;}
int cb_platform_random(unsigned char *out,size_t n) {memset(out,0,n);return 0;}
int cb_keyboard_init(int m) {(void)m;return 0;}
void cb_keyboard_close(void) {}
void cb_keyboard_connected(int c) {(void)c;}
int cb_keyboard_take_paste(void) {return 0;}
void cb_keyboard_finish_paste(int s) {(void)s;}
unsigned cb_keyboard_scroll_lines(void) {return step<5 ? 5 : 0;}
int main(void) {
 capability=0;run_session(1);assert(writes==0); /* 未声明能力，不发送扩展消息 */
 step=writes=body_pending=0;capability=1;run_session(1);
 assert(writes==3 && requests[0]==5 && requests[1]==5 && requests[2]==0);
 puts("Scroll capability negotiation, renewal and release tests passed");
}

int cb_platform_poll_content(cb_content *c) {
    memset(c,0,sizeof(*c));return cb_platform_poll(&c->text,&c->text_length);
}
int cb_platform_set_content(const cb_content *c) {return cb_platform_set(c->text,c->text_length);}

int cb_keyboard_caps_enabled(void) {return 0;}
void cb_keyboard_caps_ready(int ready) {(void)ready;}
unsigned cb_keyboard_caps_take(void) {return 0;}
