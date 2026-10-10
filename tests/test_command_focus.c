#include "keyboard_map.h"
#include <assert.h>
#include <stdio.h>
/* 回归：专用组合不能在任何阶段产生 Alt，以免 Windows 菜单抢走输入焦点。 */
int main(void) {
    cb_key_state s; cb_key_result r;
    cb_key_reset(&s, 0);
    r = cb_key_process(&s, CB_KEY_FLAGS, 55, CB_MOD_COMMAND);
    assert(r.before == 0 && r.after == 0);
    r = cb_key_process(&s, CB_KEY_DOWN, CB_MAC_C, CB_MOD_COMMAND);
    assert(r.before == CB_MOD_CONTROL && r.key == CB_MAC_INSERT);
    r = cb_key_process(&s, CB_KEY_UP, CB_MAC_C, CB_MOD_COMMAND);
    assert(r.before == CB_MOD_CONTROL && r.after == 0);
    r = cb_key_process(&s, CB_KEY_FLAGS, 55, 0);
    assert(r.before == 0 && r.after == 0);
    cb_key_reset(&s, 0);
    cb_key_process(&s, CB_KEY_FLAGS, 55, CB_MOD_COMMAND);
    r = cb_key_process(&s, CB_KEY_DOWN, CB_MAC_V, CB_MOD_COMMAND);
    assert(r.paste_request && r.suppress && r.before == 0 && r.after == 0);
    r = cb_key_process(&s, CB_KEY_UP, CB_MAC_V, CB_MOD_COMMAND);
    assert(r.suppress && r.after == 0);
    puts("Command shortcut focus tests passed");
}
