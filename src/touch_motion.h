#ifndef CB_TOUCH_MOTION_H
#define CB_TOUCH_MOTION_H
#include <math.h>
#include <string.h>
/* 归一化纵向距离 / 秒，数值 1 表示每秒移动触控板高度一次。 */
typedef struct {
 int tracking, valid, first_id, second_id;
 unsigned pid;
 double y, time, speed;
} cb_motion_state;
static inline int cb_motion_step(cb_motion_state*s,int a,int b,double y,double t,unsigned pid)
{
 if(a>b){int swap=a;a=b;b=swap;}
 if(!pid || a==b || !isfinite(y) || y<0 || y>1 || !isfinite(t)) {memset(s,0,sizeof(*s));return 0;}
 double dt=t-s->time;
 if(!s->tracking || s->first_id!=a || s->second_id!=b || s->pid!=pid || dt<=0 || dt>.15) {
  *s=(cb_motion_state){.tracking=1,.first_id=a,.second_id=b,.pid=pid,.y=y,.time=t};return 0;
 }
 double instant=fabs(y-s->y)/dt;
 /* 25ms 时间常数，按实际间隔平滑速度估计；不延迟或缓冲滚动事件。 */
 double alpha=dt/(.025+dt);
 s->speed=s->valid ? s->speed+alpha*(instant-s->speed) : instant;
 s->y=y;s->time=t;s->valid=1;return 1;
}
#endif
