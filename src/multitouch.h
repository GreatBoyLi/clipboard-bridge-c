#ifndef CB_MULTITOUCH_H
#define CB_MULTITOUCH_H
#include <sys/types.h>
/* context 返回当前允许操作的 Moonlight PID；返回 0 时取消当前手势。
 * 回调可能来自系统线程；调用方必须对共享状态加锁。
 */
int cb_touch_start(pid_t (*context)(void), void (*navigate)(unsigned short, pid_t), int debug);
void cb_touch_stop(void);
/* 仅返回同一目标进程最近 100ms 的两指速度快照，不返回触点坐标。 */
int cb_touch_speed(pid_t pid, double *speed);
#endif
