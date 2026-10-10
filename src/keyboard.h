#ifndef CB_KEYBOARD_H
#define CB_KEYBOARD_H
#include <stddef.h>
#ifdef __APPLE__
/* 可选功能：不启用时，不安装事件监听、不要求额外系统权限。 */
int cb_keyboard_init(int redo_shift_z);
void cb_keyboard_close(void);
void cb_keyboard_connected(int connected);
/* 主线程取得粘贴请求；只返回请求，不读取或重发 Mac 剪贴板旧内容。 */
int cb_keyboard_take_paste(void);
/* 主线程取两指速度对应的行数；0 表示恢复。不会修改滚动事件。 */
unsigned cb_keyboard_scroll_lines(void);
/* 等所有待发送文字均已确认后才执行；无待同步文字时直接使用 Windows 剪贴板。 */
void cb_keyboard_finish_paste(int success);
#else
/* Windows 沿用 Moonlight 的键盘输入，不需要新增注入模块。 */
static inline int cb_keyboard_init(int redo_shift_z) { (void)redo_shift_z; return -1; }
static inline void cb_keyboard_close(void) { }
static inline void cb_keyboard_connected(int connected) { (void)connected; }
static inline unsigned cb_keyboard_scroll_lines(void) { return 0; }
static inline int cb_keyboard_take_paste(void) { return 0; }
static inline void cb_keyboard_finish_paste(int success) { (void)success; }
#endif
#endif
