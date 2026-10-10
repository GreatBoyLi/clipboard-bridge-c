#include "touch_swipe.h"
#include <assert.h>
#include <stdio.h>
static cb_touch_result step(cb_touch_state *s, int n, double x, double y, double t, unsigned pid) {
    cb_touch_point p[4] = {{1,x,y},{2,x+0.1,y},{3,x+0.2,y},{4,x+0.3,y}};
    return cb_touch_step(s,p,n,t,pid);
}
int main(void) {
    cb_touch_state s = {0};
    step(&s,3,.2,.3,1,200); step(&s,3,.26,.3,1.03,200);
    cb_touch_result r=step(&s,3,.34,.3,1.06,200);
    assert(r.key==124 && r.pid==200); /* 实机 normalized.x 增长对应左滑＝前进 */
    assert(!step(&s,3,.5,.3,1.09,200).key); /* 一次手势仅一次 */
    step(&s,0,0,0,1.1,200);
    step(&s,3,.5,.3,2,200); step(&s,3,.44,.3,2.03,200);
    assert(step(&s,3,.36,.3,2.06,200).key==123);
    s=(cb_touch_state){0}; step(&s,3,.2,.3,3,200);
    step(&s,4,.24,.3,3.03,200);
    assert(!step(&s,3,.5,.3,3.06,200).key); /* 四指后减少到三指也不触发 */
    step(&s,0,0,0,3.1,200);
    step(&s,3,.2,.3,4,200); step(&s,3,.2,.4,4.03,200);
    assert(!step(&s,3,.4,.5,4.06,200).key); /* 垂直移动先发生则取消 */
    s=(cb_touch_state){0}; step(&s,3,.2,.3,5,200);
    step(&s,3,.26,.3,5.03,201);
    assert(!step(&s,3,.5,.3,5.06,201).key); /* 切换进程必须抬手再开始 */
    s=(cb_touch_state){0}; step(&s,3,.2,.3,6,200); step(&s,3,.26,.3,6.03,0);
    assert(!step(&s,3,.5,.3,6.06,200).key); /* 断线/按其他键期间取消 */
    s=(cb_touch_state){0}; step(&s,2,.2,.3,7,200); step(&s,2,.5,.3,7.1,200);
    assert(!step(&s,2,.8,.3,7.2,200).key);
    puts("Raw three-finger recognition tests passed");
}
