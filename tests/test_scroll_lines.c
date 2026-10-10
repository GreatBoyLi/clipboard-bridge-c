#include "scroll_lines.h"
#include "scroll_restore.h"
#include <limits.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
static unsigned actual;
static int calls,fail;
static int set_mock(unsigned value,void *ctx) {
 (void)ctx;++calls;if(fail) return -1;actual=value;return 0;
}
int main(void) {
 assert(cb_lines_linear(0)==1);
 assert(cb_lines_linear(.2)==1);
 assert(cb_lines_linear(.3)==1);
 assert(cb_lines_linear(.4)==1);
 assert(cb_lines_linear(.5)==1);
 assert(cb_lines_linear(.537)==1);
 assert(cb_lines_linear(.54)==2);
 assert(cb_lines_linear(.575)==2);
 assert(cb_lines_linear(.65)==3);
 assert(cb_lines_linear(.725)==4);
 assert(cb_lines_linear(.8)==5);
 assert(cb_lines_linear(10)==5);
 assert(cb_lines_linear(NAN)==0);
 assert(cb_lines_linear(-1)==0);
 for(int i=1;i<=800;++i) {
  double speed=(double)i/1000;
  assert(cb_lines_linear(speed)>=cb_lines_linear(speed-.001));
  assert(cb_lines_linear(speed)>=1 && cb_lines_linear(speed)<=5);
 }
 cb_lines_filter f={0};
 assert(cb_lines_update(&f,.08,100)==1);
 assert(cb_lines_update(&f,.8,150)==1); /* 最多每 200ms 切换一次 */
 assert(cb_lines_update(&f,.8,300)==5);
 f=(cb_lines_filter){0};assert(cb_lines_update(&f,.56,100)==2);
 assert(cb_lines_update(&f,.54,400)==2); /* 阈值附近不切回 1 */
 assert(cb_lines_update(&f,.08,700)==1);
 /* 实机常见慢滑速度先保持 1 行，超过扩大的范围才升档。 */
 f=(cb_lines_filter){0};
 assert(cb_lines_update(&f,.039,100)==1);
 assert(cb_lines_update(&f,.209,400)==1);
 assert(cb_lines_update(&f,.299,700)==1);
 assert(cb_lines_update(&f,.32,1000)==1);
 assert(cb_lines_update(&f,.5,1300)==1);
 assert(cb_lines_update(&f,.54,1600)==1);
 assert(cb_lines_update(&f,.55,1900)==2);
 assert(cb_lines_update(&f,.8,2200)==5);
 cb_lines_lease lease={0};
 cb_lines_renew(&lease,5,100);
 assert(cb_lines_desired(&lease,100)==5);
 assert(cb_lines_desired(&lease,1299)==5);
 assert(cb_lines_desired(&lease,1300)==0); /* 卡死/断网独立超时 */
 cb_lines_renew(&lease,0,400);
 assert(cb_lines_desired(&lease,400)==0);
 /* 同一目标不重复广播；租约结束、退出、断线均走同一个恢复入口。 */
 cb_scroll_owner owner={3,3};actual=3;calls=0;
 assert(cb_scroll_apply(&owner,1,set_mock,NULL)==0 && actual==1);
 assert(cb_scroll_apply(&owner,1,set_mock,NULL)==0 && calls==1);
 assert(cb_scroll_apply(&owner,5,set_mock,NULL)==0 && actual==5);
 assert(cb_scroll_apply(&owner,0,set_mock,NULL)==0 && actual==3);
 owner=(cb_scroll_owner){UINT_MAX,UINT_MAX};actual=UINT_MAX;
 assert(cb_scroll_apply(&owner,1,set_mock,NULL)==0);
 fail=1;assert(cb_scroll_apply(&owner,0,set_mock,NULL)<0 && owner.applied==1);
 fail=0;assert(cb_scroll_apply(&owner,0,set_mock,NULL)==0 && actual==UINT_MAX);
 owner=(cb_scroll_owner){0,0};
 assert(cb_scroll_apply(&owner,5,set_mock,NULL)==0);
 assert(cb_scroll_apply(&owner,0,set_mock,NULL)==0 && actual==0);
 assert(cb_scroll_apply(&owner,6,set_mock,NULL)<0);
 puts("System scroll line mapping, lease and restoration tests passed");
}
