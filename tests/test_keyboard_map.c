#include "keyboard_map.h"
#include <assert.h>
#include <stdio.h>

/* 纯逻辑测试，不创建全局事件监听，也不修改系统剪贴板。 */
int main(void)
{
    cb_key_state s;
    cb_key_result r;
    cb_key_reset(&s, 0);
    assert(cb_key_base_mods(CB_MOD_OPTION) == CB_MOD_COMMAND);
    assert(cb_key_base_mods(CB_MOD_COMMAND) == CB_MOD_OPTION);
    assert(cb_key_base_mods(CB_MOD_CONTROL | CB_MOD_SHIFT) ==
           (CB_MOD_CONTROL | CB_MOD_SHIFT));

    /* ⌘C 按 Ctrl+Insert，抬起后不发送 Alt，避免菜单抢走焦点。 */
    r = cb_key_process(&s, CB_KEY_DOWN, CB_MAC_C, CB_MOD_COMMAND);
    assert(r.key == CB_MAC_INSERT && r.before == CB_MOD_CONTROL);
    r = cb_key_process(&s, CB_KEY_UP, CB_MAC_C, CB_MOD_COMMAND);
    assert(r.key == CB_MAC_INSERT && r.before == CB_MOD_CONTROL);
    assert(r.after == 0);

    /* 先松开 Command，也不能改变 C 的释放目标或提前松开 Ctrl。 */
    r = cb_key_process(&s, CB_KEY_DOWN, CB_MAC_C, CB_MOD_COMMAND);
    r = cb_key_process(&s, CB_KEY_FLAGS, 55, 0);
    assert(r.before == CB_MOD_CONTROL);
    r = cb_key_process(&s, CB_KEY_UP, CB_MAC_C, 0);
    assert(r.key == CB_MAC_INSERT && r.after == 0);

    /* Control+C 不转换，不影响终端中断。 */
    r = cb_key_process(&s, CB_KEY_DOWN, CB_MAC_C, CB_MOD_CONTROL);
    assert(r.key == CB_MAC_C && r.before == CB_MOD_CONTROL);
    cb_key_process(&s, CB_KEY_UP, CB_MAC_C, 0);

    r = cb_key_process(&s, CB_KEY_DOWN, CB_MAC_Z, CB_MOD_COMMAND);
    assert(r.key == CB_MAC_Z && r.before == CB_MOD_CONTROL);
    cb_key_process(&s, CB_KEY_UP, CB_MAC_Z, 0);
    r = cb_key_process(&s, CB_KEY_DOWN, CB_MAC_Z,
                       CB_MOD_COMMAND | CB_MOD_SHIFT);
    assert(r.key == CB_MAC_Y && r.before == CB_MOD_CONTROL);
    cb_key_process(&s, CB_KEY_UP, CB_MAC_Z, 0);

    /* 粘贴需要 ACK 后执行，不能立刻向 Moonlight 转发 V。 */
    r = cb_key_process(&s, CB_KEY_DOWN, CB_MAC_V, CB_MOD_COMMAND);
    assert(r.suppress && r.paste_request);
    r = cb_key_process(&s, CB_KEY_DOWN, CB_MAC_V, CB_MOD_COMMAND);
    assert(r.suppress && !r.paste_request);
    r = cb_key_process(&s, CB_KEY_UP, CB_MAC_V, 0);
    assert(r.suppress && !r.paste_request && r.after == 0);

    /* 没有专门规则的组合继续使用基础修饰键映射。 */
    r = cb_key_process(&s, CB_KEY_DOWN, 11, CB_MOD_COMMAND);
    assert(r.key == 11 && r.before == CB_MOD_OPTION);
    cb_key_process(&s, CB_KEY_UP, 11, 0);
    r = cb_key_process(&s, CB_KEY_DOWN, CB_MAC_C,
                       CB_MOD_COMMAND | CB_MOD_CONTROL);
    assert(r.key == CB_MAC_C &&
           r.before == (CB_MOD_OPTION | CB_MOD_CONTROL));

    cb_key_reset(&s, 1);
    r = cb_key_process(&s, CB_KEY_DOWN, CB_MAC_Z,
                       CB_MOD_COMMAND | CB_MOD_SHIFT);
    assert(r.key == CB_MAC_Z &&
           r.before == (CB_MOD_CONTROL | CB_MOD_SHIFT));
    /* 普通 Alt 组合保持可用；独立 Command 在释放时才执行 Alt。 */
    cb_key_reset(&s, 0);
    r = cb_key_process(&s, CB_KEY_FLAGS, 55, CB_MOD_COMMAND);
    assert(r.before == 0 && !r.alt_tap);
    r = cb_key_process(&s, CB_KEY_FLAGS, 55, 0);
    assert(r.alt_tap);
    cb_key_process(&s, CB_KEY_FLAGS, 55, CB_MOD_COMMAND);
    r = cb_key_process(&s, CB_KEY_DOWN, 48, CB_MOD_COMMAND);
    assert(r.before == CB_MOD_OPTION); /* Command+Tab -> Alt+Tab */
    r = cb_key_process(&s, CB_KEY_UP, 48, CB_MOD_COMMAND);
    assert(r.after == CB_MOD_OPTION);
    r = cb_key_process(&s, CB_KEY_FLAGS, 55, 0);
    assert(!r.alt_tap && r.after == 0);
    cb_key_reset(&s, 0);
    cb_key_process(&s, CB_KEY_FLAGS, 55, CB_MOD_COMMAND);
    cb_key_process(&s, CB_KEY_DOWN, CB_MAC_C, CB_MOD_COMMAND);
    cb_key_process(&s, CB_KEY_UP, CB_MAC_C, CB_MOD_COMMAND);
    r = cb_key_process(&s, CB_KEY_FLAGS, 55, 0);
    assert(!r.alt_tap && r.after == 0);
    puts("Keyboard mapping tests passed");
    return 0;
}
