#ifndef CB_CAPS_REMOTE_H
#define CB_CAPS_REMOTE_H
#include "caps_action.h"
/* 仅在已认证且已启用 Caps 映射的会话中启动；返回 1 表示成功。 */
int cb_caps_remote_init(void);
/* 断开会话前停止；函数返回后不再执行排队的动作。 */
void cb_caps_remote_close(void);
/* 返回 1 表示 SendInput 已完整提交；冲突、失败或超时返回 0。 */
int cb_caps_remote_action(unsigned action);
#endif
