#include "keyboard_map.h"
#include <string.h>
void cb_key_reset(cb_key_state *s, int redo_shift_z)
{
    memset(s, 0, sizeof(*s));
    s->redo_shift_z = redo_shift_z;
}
unsigned cb_key_base_mods(unsigned physical)
{
    unsigned result = physical & (CB_MOD_CONTROL | CB_MOD_SHIFT);
    if (physical & CB_MOD_OPTION) result |= CB_MOD_COMMAND;
    if (physical & CB_MOD_COMMAND) result |= CB_MOD_OPTION;
    return result;
}
unsigned cb_key_desired_mods(const cb_key_state *s)
{
    /* 特殊组合按住期间维持其目标修饰键，不提前恢复 Alt。 */
    for (unsigned i = 0; i < CB_KEY_COUNT; ++i)
        if (s->held[i].down && s->held[i].special) return s->held[i].forced;
    unsigned mods = cb_key_base_mods(s->raw_mods);
    if (!s->command_alt) mods &= ~CB_MOD_OPTION;
    return mods;
}
cb_key_result cb_key_process(cb_key_state *s, int type, uint16_t key, unsigned physical)
{
    unsigned previous = s->raw_mods;
    cb_key_result r = {key, 0, 0, 0, 0, 0};
    if ((physical & CB_MOD_COMMAND) && !(previous & CB_MOD_COMMAND)) {
        s->command_used = 0;
        s->command_alt = 0;
    }
    if (!(physical & CB_MOD_COMMAND) && (previous & CB_MOD_COMMAND)) {
        /* 单独按 Command 的 Alt 行为在松开时执行；用过专用组合则不补发。 */
        r.alt_tap = !s->command_used && previous == CB_MOD_COMMAND && physical == 0;
        s->command_alt = 0;
    }
    s->raw_mods = physical;
    if (type == CB_KEY_FLAGS || key >= CB_KEY_COUNT) {
        r.before = r.after = cb_key_desired_mods(s);
        r.suppress = type == CB_KEY_FLAGS;
        return r;
    }
    cb_held_key *held = &s->held[key];
    if (type == CB_KEY_DOWN && !held->down) {
        held->down = 1;
        held->output = key;
        if (physical == CB_MOD_COMMAND && key == CB_MAC_V) {
            held->suppress = 1;
            r.paste_request = 1;
        } else if (physical == CB_MOD_COMMAND &&
                   (key == CB_MAC_A || key == CB_MAC_C || key == CB_MAC_Z ||
                    key == CB_MAC_S || key == CB_MAC_F || key == CB_MAC_X ||
                    key == CB_MAC_T || key == CB_MAC_R)) {
            held->special = 1;
            held->forced = CB_MOD_CONTROL;
            held->output = key == CB_MAC_C ? CB_MAC_INSERT : key;
        } else if (physical == (CB_MOD_COMMAND | CB_MOD_SHIFT) && key == CB_MAC_Z) {
            held->special = 1;
            held->forced = CB_MOD_CONTROL | (s->redo_shift_z ? CB_MOD_SHIFT : 0);
            held->output = s->redo_shift_z ? CB_MAC_Z : CB_MAC_Y;
        }
    }
    if (type == CB_KEY_DOWN && (physical & CB_MOD_COMMAND)) {
        s->command_used = 1;
        /* 普通组合（如 Command+Tab）仍使用 Alt，专用组合则屏蔽 Alt。 */
        s->command_alt = !held->special && !held->suppress;
    }
    if (held->down) {
        r.key = held->output;
        r.suppress = held->suppress;
    }
    r.before = cb_key_desired_mods(s);
    if (type == CB_KEY_UP) memset(held, 0, sizeof(*held));
    r.after = cb_key_desired_mods(s);
    return r;
}
