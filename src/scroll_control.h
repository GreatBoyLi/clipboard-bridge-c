#ifndef CB_SCROLL_CONTROL_H
#define CB_SCROLL_CONTROL_H
/* Windows 显式启用的可选系统行数控制。Mac 不会修改本机设置。 */
#ifdef _WIN32
int cb_scroll_guard_entry(int argc,char **argv); /* -1: 普通启动 */
int cb_scroll_init(void);
int cb_scroll_set(unsigned lines); /* 0 恢复，1~5 临时行数 */
void cb_scroll_close(void);
/* 桌面切换必须等守护进程恢复完成；失败阻止下一设备自动启动。 */
int cb_scroll_close_wait(void);
#else
static inline int cb_scroll_guard_entry(int argc,char **argv) {(void)argc;(void)argv;return -1;}
static inline int cb_scroll_init(void) {return -1;}
static inline int cb_scroll_set(unsigned lines) {(void)lines;return -1;}
static inline void cb_scroll_close(void) {}
static inline int cb_scroll_close_wait(void) {return 0;}
#endif
#endif
