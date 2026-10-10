#include "caps_hold.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    cb_caps_hold s={0};
    cb_caps_press(&s,7,10,100);
    assert(cb_caps_tick(&s,10,1099)==0);
    assert(cb_caps_release(&s,7,10,1099)==CB_CAPS_SHORT);
    assert(cb_caps_release(&s,7,10,1100)==0);
    cb_caps_press(&s,7,10,2000);
    assert(cb_caps_tick(&s,10,3000)==CB_CAPS_LONG);
    assert(cb_caps_tick(&s,10,5000)==0);
    assert(cb_caps_release(&s,7,10,5001)==0);
    cb_caps_press(&s,7,10,6000);
    assert(cb_caps_release(&s,7,10,7000)==CB_CAPS_LONG);
    cb_caps_press(&s,7,10,8000);
    cb_caps_press(&s,7,10,8900); // 重复 down 不能重置计时。
    assert(cb_caps_tick(&s,10,9000)==CB_CAPS_LONG);
    cb_caps_cancel(&s);assert(cb_caps_release(&s,7,10,9001)==0);
    cb_caps_press(&s,7,10,10000);
    assert(cb_caps_release(&s,8,10,10100)==0); // 另一块键盘不能释放本次按住。
    assert(cb_caps_release(&s,7,10,10101)==CB_CAPS_SHORT);
    cb_caps_press(&s,7,10,11000);
    assert(cb_caps_tick(&s,11,12000)==0); // 换前台进程取消。
    assert(cb_caps_release(&s,7,10,12001)==0);
    cb_caps_press(&s,7,10,13000);
    assert(cb_caps_release(&s,7,10,12999)==0); // 拒绝倒序时间。
    puts("Caps short, long, repeat, device and context tests passed");
}
