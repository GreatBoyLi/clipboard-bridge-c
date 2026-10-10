#include "touch_motion.h"
#include "scroll_lines.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void) {
 cb_motion_state slow={0},fast={0};
 cb_motion_step(&slow,1,2,.3,1,200);
 cb_motion_step(&fast,1,2,.3,1,200);
 assert(cb_motion_step(&slow,1,2,.31,1.1,200));
 assert(cb_motion_step(&fast,1,2,.31,1.01,200));
 assert(fabs(slow.speed-.1)<.0001 && fabs(fast.speed-1)<.0001);
 assert(cb_lines_linear(fast.speed)>cb_lines_linear(slow.speed));
 /* 触点顺序交换不是换手；更换 ID、时钟倒退、过大间隔、焦点改变则重置。 */
 assert(cb_motion_step(&slow,2,1,.32,1.2,200));
 assert(!cb_motion_step(&slow,1,3,.4,1.3,200));
 assert(!cb_motion_step(&slow,1,3,.5,1.2,200));
 assert(!cb_motion_step(&slow,1,3,.6,2,200));
 assert(!cb_motion_step(&slow,1,3,.7,2.01,201));
 puts("Two-finger velocity tests passed");
}
