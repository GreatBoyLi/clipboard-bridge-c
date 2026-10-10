#include "keyboard_map.h"
#include <assert.h>
#include <stdio.h>
/* 全选必须发送 Ctrl+A，且整个组合不能发出 Alt。Mac A 的虚拟键码为 0。 */
int main(void) {
    cb_key_state s; cb_key_result r;
    cb_key_reset(&s, 0);
    cb_key_process(&s, CB_KEY_FLAGS, 55, CB_MOD_COMMAND);
    r = cb_key_process(&s, CB_KEY_DOWN, 0, CB_MOD_COMMAND);
    assert(r.key == 0 && r.before == CB_MOD_CONTROL && !r.suppress && !r.paste_request);
    r = cb_key_process(&s, CB_KEY_DOWN, 0, CB_MOD_COMMAND); // 按住产生重复事件
    assert(r.before == CB_MOD_CONTROL);
    r = cb_key_process(&s, CB_KEY_UP, 0, CB_MOD_COMMAND);
    assert(r.before == CB_MOD_CONTROL && r.after == 0);
    r = cb_key_process(&s, CB_KEY_FLAGS, 55, 0);
    assert(!r.alt_tap);
    cb_key_reset(&s, 0);
    cb_key_process(&s, CB_KEY_DOWN, 0, CB_MOD_COMMAND);
    r = cb_key_process(&s, CB_KEY_FLAGS, 55, 0); // 先松 Command，再松 A
    assert(r.before == CB_MOD_CONTROL && !r.alt_tap);
    r = cb_key_process(&s, CB_KEY_UP, 0, 0);
    assert(r.key == 0 && r.before == CB_MOD_CONTROL && r.after == 0);
    r = cb_key_process(&s, CB_KEY_DOWN, 0, CB_MOD_CONTROL);
    assert(r.key == 0 && r.before == CB_MOD_CONTROL);
    puts("Select all shortcut tests passed");
}
