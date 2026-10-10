#ifndef CB_KEYBOARD_MAP_H
#define CB_KEYBOARD_MAP_H
#include <stdint.h>
/* 纯 C 状态机使用平台无关的修饰键位，不依赖 CoreGraphics。 */
enum { CB_MOD_CONTROL = 1, CB_MOD_OPTION = 2, CB_MOD_COMMAND = 4, CB_MOD_SHIFT = 8 };
enum { CB_KEY_FLAGS, CB_KEY_DOWN, CB_KEY_UP };
/* macOS 虚拟键码，与当前键盘布局显示的字符无关。 */
enum {
    CB_MAC_A = 0, CB_MAC_S = 1, CB_MAC_F = 3, CB_MAC_Z = 6,
    CB_MAC_X = 7, CB_MAC_C = 8, CB_MAC_V = 9, CB_MAC_R = 15,
    CB_MAC_Y = 16, CB_MAC_T = 17, CB_MAC_INSERT = 114
};
#define CB_KEY_COUNT 128
typedef struct {
    unsigned char down, special, suppress;
    uint16_t output;
    unsigned forced;
} cb_held_key;
typedef struct {
    cb_held_key held[CB_KEY_COUNT];
    unsigned raw_mods;
    int redo_shift_z;
    /* Command 先暂存；普通组合确认后才发送 Alt，专用组合不发送。 */
    int command_used, command_alt;
} cb_key_state;
typedef struct {
    uint16_t key;
    unsigned before, after;
    int suppress, paste_request, alt_tap;
} cb_key_result;
void cb_key_reset(cb_key_state *state, int redo_shift_z);
unsigned cb_key_base_mods(unsigned physical_mods);
unsigned cb_key_desired_mods(const cb_key_state *state);
/* 同一键的抬起使用按下时保存的目标，不随修饰键松开顺序变化。 */
cb_key_result cb_key_process(cb_key_state *state, int type, uint16_t key, unsigned physical_mods);
#endif
