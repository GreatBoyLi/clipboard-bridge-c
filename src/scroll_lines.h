#ifndef CB_SCROLL_LINES_H
#define CB_SCROLL_LINES_H
#include <stdint.h>
#include <math.h>
/* 按触控板高度/秒线性映射：<=0.5 为 1 行，>=0.8 为 5 行。
 * 四舍五入后初始 1 行范围约至 0.538；防抖使升至 2 行约需超过 0.545。
 * Windows 行数是整数，只对线性结果四舍五入，不改写任何滚动事件。
 * 0 专用于“恢复原值”，不能误设成 Windows 的“禁止滚动”。 */
static inline double cb_lines_position(double speed) {
    double t=(speed-.5)/.3;
    return t<0 ? 1 : t>1 ? 5 : 1+4*t;
}
static inline unsigned cb_lines_linear(double speed) {
    if(!isfinite(speed) || speed<0) return 0;
    return (unsigned)(cb_lines_position(speed)+.5);
}
typedef struct { unsigned lines; uint64_t changed; } cb_lines_filter;
static inline unsigned cb_lines_update(cb_lines_filter *f,double speed,uint64_t now) {
    unsigned desired=cb_lines_linear(speed);
    if(!desired) return 0;
    if(!f->lines) {f->lines=desired;f->changed=now;}
    /* 小幅滞回和最短切换间隔避免临界速度使全局设置来回跳。
     * 这只稳定行数设置，不延迟 Moonlight 的输入事件。 */
    else if(now-f->changed>=200 && desired!=f->lines &&
            fabs(cb_lines_position(speed)-(double)f->lines)>.6) {
        f->lines=desired;f->changed=now;
    }
    return f->lines;
}
typedef struct { unsigned lines; uint64_t deadline; } cb_lines_lease;
static inline void cb_lines_renew(cb_lines_lease *s,unsigned lines,uint64_t now) {
    s->lines=lines<=5 ? lines : 0;s->deadline=now+1200;
}
static inline unsigned cb_lines_desired(const cb_lines_lease *s,uint64_t now) {
    return now<s->deadline ? s->lines : 0;
}
#endif
