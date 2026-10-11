/* 使用真实网络/生命周期与模拟剪贴板，不读写用户剪贴板或系统键盘。 */
#include <assert.h>
#define main cb_worker_main
#include "../src/main.c"
#undef main
int cb_platform_init(void) {return 0;}
void cb_platform_close(void) {puts("TEST_PLATFORM_CLOSED");}
void cb_platform_reset(void) {puts("TEST_BASELINE_RESET");}
void cb_platform_cleanup_cache(void) {}
int cb_platform_poll_content(cb_content *content) {memset(content,0,sizeof(*content));return 0;}
int cb_platform_set_content(const cb_content *content) {(void)content;assert(0);return -1;}
int cb_platform_poll(char **text,size_t *length) {*text=NULL;*length=0;return 0;}
int cb_platform_set(const char *text,size_t length) {(void)text;(void)length;assert(0);return -1;}
int cb_platform_random(unsigned char *data,size_t length) {memset(data,0,length);return 0;}
#ifdef __APPLE__
int cb_keyboard_init(int redo) {(void)redo;return 0;}
void cb_keyboard_close(void) {puts("TEST_KEYBOARD_CLOSED");}
void cb_keyboard_connected(int active) {printf("TEST_KEYBOARD_CONNECTED %d\n",active);}
int cb_keyboard_caps_enabled(void) {return 0;}
void cb_keyboard_caps_ready(int ready) {(void)ready;}
unsigned cb_keyboard_caps_take(void) {return 0;}
int cb_keyboard_take_paste(void) {return 0;}
unsigned cb_keyboard_scroll_lines(void) {return 0;}
void cb_keyboard_finish_paste(int success) {(void)success;assert(0);}
#endif
int main(int argc,char **argv) {return cb_worker_main(argc,argv);}
