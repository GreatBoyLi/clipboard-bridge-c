#ifndef CB_CAPS_HOLD_H
#define CB_CAPS_HOLD_H
#include <stdint.h>
#include <string.h>
#include "caps_action.h"
/* 只记录物理按住，不使用 Caps Lock 的锁定标志判断按下/松开。 */
typedef struct {
    uintptr_t device;
    int64_t pid;
    uint64_t pressed_at;
    int down, fired;
} cb_caps_hold;
static inline void cb_caps_cancel(cb_caps_hold *s) {memset(s,0,sizeof(*s));}
static inline void cb_caps_press(cb_caps_hold *s,uintptr_t device,int64_t pid,uint64_t now) {
    if(s->down || !pid) return;
    s->device=device;s->pid=pid;s->pressed_at=now;s->down=1;s->fired=0;
}
static inline unsigned cb_caps_tick(cb_caps_hold *s,int64_t pid,uint64_t now) {
    if(!s->down) return 0;
    if(pid!=s->pid || now<s->pressed_at) {cb_caps_cancel(s);return 0;}
    if(!s->fired && now-s->pressed_at>=1000) {s->fired=1;return CB_CAPS_LONG;}
    return 0;
}
static inline unsigned cb_caps_release(cb_caps_hold *s,uintptr_t device,int64_t pid,uint64_t now) {
    if(!s->down || s->device!=device) return 0;
    unsigned action=cb_caps_tick(s,pid,now);
    if(s->down && !s->fired) action=CB_CAPS_SHORT;
    cb_caps_cancel(s);return action;
}
#endif
