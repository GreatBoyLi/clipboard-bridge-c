#include "keyboard_map.h"
#include <assert.h>
#include <stdio.h>
/* Mac 的 S、F、X、T、R 虚拟键码；测试不发送实际按键。 */
int main(void) {
    const uint16_t keys[] = {1, 3, 7, 17, 15};
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        cb_key_state s; cb_key_result r;
        cb_key_reset(&s, 0);
        r = cb_key_process(&s, CB_KEY_FLAGS, 55, CB_MOD_COMMAND);
        assert(r.before == 0);
        r = cb_key_process(&s, CB_KEY_DOWN, keys[i], CB_MOD_COMMAND);
        assert(r.key == keys[i] && r.before == CB_MOD_CONTROL && !r.suppress && !r.paste_request);
        r = cb_key_process(&s, CB_KEY_DOWN, keys[i], CB_MOD_COMMAND);
        assert(r.before == CB_MOD_CONTROL); // 重复按下仍保持同一目标。
        r = cb_key_process(&s, CB_KEY_UP, keys[i], CB_MOD_COMMAND);
        assert(r.before == CB_MOD_CONTROL && r.after == 0);
        r = cb_key_process(&s, CB_KEY_FLAGS, 55, 0);
        assert(r.after == 0 && !r.alt_tap);
        cb_key_reset(&s, 0);
        cb_key_process(&s, CB_KEY_DOWN, keys[i], CB_MOD_COMMAND);
        r = cb_key_process(&s, CB_KEY_FLAGS, 55, 0);
        assert(r.before == CB_MOD_CONTROL && !r.alt_tap); // 先松 Command
        r = cb_key_process(&s, CB_KEY_UP, keys[i], 0);
        assert(r.key == keys[i] && r.before == CB_MOD_CONTROL && r.after == 0);
        r = cb_key_process(&s, CB_KEY_DOWN, keys[i], CB_MOD_CONTROL);
        assert(r.key == keys[i] && r.before == CB_MOD_CONTROL); // 物理 Control 不改变。
    }
    puts("Common Command shortcut tests passed");
}
