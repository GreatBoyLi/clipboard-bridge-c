/* 无 Windows SDK 的主机也可验证动作、钩子和部分提交清理逻辑。 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CB_CAPS_TEST
#define CALLBACK
#define HC_ACTION 0
#define WM_KEYDOWN 0x100u
#define WM_KEYUP 0x101u
#define WM_SYSKEYDOWN 0x104u
#define WM_SYSKEYUP 0x105u
#define VK_CAPITAL 0x14u
#define VK_SPACE 0x20u
#define VK_LWIN 0x5bu
#define VK_SHIFT 0x10u
#define VK_CONTROL 0x11u
#define VK_MENU 0x12u
#define VK_LSHIFT 0xa0u
#define KEYEVENTF_EXTENDEDKEY 1u
#define KEYEVENTF_KEYUP 2u
#define INPUT_KEYBOARD 1u
typedef uintptr_t ULONG_PTR;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef unsigned UINT;
typedef struct { DWORD vkCode; DWORD scanCode; DWORD flags; DWORD time; ULONG_PTR dwExtraInfo; } KBDLLHOOKSTRUCT;
typedef struct { WORD wVk; WORD wScan; DWORD dwFlags; DWORD time; ULONG_PTR dwExtraInfo; } KEYBDINPUT;
typedef struct { DWORD type; KEYBDINPUT ki; } INPUT;
static LRESULT next_result;
static unsigned next_calls;
static unsigned send_calls;
static UINT send_limits[4];
static unsigned pause_calls;
static unsigned fail_pause;
static int caps_input_pause(void) { return ++pause_calls != fail_pause; }
static unsigned char async_down[256];
static INPUT submitted[16];
static UINT submitted_count;
static LRESULT CallNextHookEx(void *hook, int code, WPARAM message, LPARAM parameter)
{ (void)hook; (void)code; (void)message; (void)parameter; ++next_calls; return next_result; }
static short GetAsyncKeyState(int key)
{ return (short)(async_down[key] ? 0x8000u : 0u); }
static UINT SendInput(UINT count, INPUT *input, int size)
{
    UINT limit = send_limits[send_calls++];
    UINT accepted = limit < count ? limit : count;
    assert(size == (int)sizeof(INPUT));
    assert(submitted_count + count <= 16);
    memcpy(&submitted[submitted_count], input, count * sizeof(INPUT));
    submitted_count += count;
    return accepted;
}
#include "../src/caps_windows.c"
static void reset(int on)
{
    caps_on = on; caps_down = 0; caps_healthy = 1;
    memset(external_down, 0, sizeof(external_down));
    memset(async_down, 0, sizeof(async_down));
    memset(submitted, 0, sizeof(submitted));
    next_result = 0; next_calls = 0; send_calls = 0;
    for (unsigned i=0;i<4;++i) send_limits[i]=UINT32_MAX;
    pause_calls=fail_pause=0; submitted_count = 0;
}
static void hook_key(DWORD key, WPARAM message, ULONG_PTR tag)
{
    KBDLLHOOKSTRUCT event = {0};
    event.vkCode = key; event.dwExtraInfo = tag;
    assert(caps_hook(HC_ACTION, message, (LPARAM)&event) == next_result);
}
int main(void)
{
    reset(0);
    async_down[VK_SHIFT] = async_down[VK_LSHIFT] = 1;
    caps_seed_keys();
    assert(!external_down[VK_SHIFT] && external_down[VK_LSHIFT]);
    async_down[VK_SHIFT] = async_down[VK_LSHIFT] = 0;
    hook_key(VK_LSHIFT, WM_KEYUP, 0);
    assert(!caps_busy());
    reset(0);
    assert(caps_execute(CB_CAPS_SHORT) == 1 && submitted_count == 4);
    assert(submitted[0].ki.wVk == VK_LWIN && submitted[1].ki.wVk == VK_SPACE);
    assert(send_calls == 3 && pause_calls == 2);
    assert(submitted[2].ki.dwFlags == KEYEVENTF_KEYUP);
    assert(submitted[3].ki.dwFlags == (KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY));
    reset(1);
    assert(caps_execute(CB_CAPS_SHORT) == 1 && submitted_count == 2);
    assert(submitted[0].ki.wVk == VK_CAPITAL);
    reset(0);
    assert(caps_execute(CB_CAPS_LONG) == 1 && submitted_count == 2);
    assert(submitted[0].ki.wVk == VK_CAPITAL);
    reset(1);
    assert(caps_execute(CB_CAPS_LONG) == 1 && submitted_count == 2);
    reset(0);
    assert(!caps_execute(0) && !caps_execute(99) && send_calls == 0);
    async_down[VK_SHIFT] = 1;
    assert(!caps_execute(CB_CAPS_SHORT) && send_calls == 0);
    async_down[VK_SHIFT] = 0;
    external_down['A'] = 1;
    assert(!caps_execute(CB_CAPS_LONG) && send_calls == 0);
    reset(0);
    hook_key(VK_CAPITAL, WM_KEYDOWN, 0);
    assert(caps_on && caps_down && !caps_execute(CB_CAPS_SHORT));
    hook_key(VK_CAPITAL, WM_KEYDOWN, 0);
    assert(caps_on);
    hook_key(VK_CAPITAL, WM_KEYUP, 0);
    assert(!caps_down && !external_down[VK_CAPITAL]);
    hook_key(VK_CAPITAL, WM_KEYDOWN, CB_CAPS_TAG);
    assert(!caps_on && !external_down[VK_CAPITAL]);
    hook_key(VK_CAPITAL, WM_KEYUP, CB_CAPS_TAG);
    assert(next_calls == 5);
    reset(0);
    next_result = 1;
    hook_key(VK_CAPITAL, WM_KEYDOWN, 0);
    assert(!caps_on && !caps_down && next_calls == 1);
    reset(0);
    send_limits[2] = 0;
    assert(!caps_execute(CB_CAPS_SHORT) && send_calls == 4 && submitted_count == 6);
    assert(submitted[4].ki.wVk == VK_SPACE && submitted[4].ki.dwFlags == KEYEVENTF_KEYUP);
    assert(submitted[5].ki.wVk == VK_LWIN && (submitted[5].ki.dwFlags & KEYEVENTF_KEYUP));
    assert(!caps_execute(CB_CAPS_LONG) && send_calls == 4);
    reset(0);
    send_limits[0] = 0;
    assert(!caps_execute(CB_CAPS_SHORT) && send_calls == 1);
    reset(1);
    send_limits[0] = 1;
    assert(!caps_execute(CB_CAPS_LONG) && send_calls == 2 && submitted_count == 3);
    assert(submitted[2].ki.wVk == VK_CAPITAL && submitted[2].ki.dwFlags == KEYEVENTF_KEYUP);
    reset(0);
    fail_pause=1;
    assert(!caps_execute(CB_CAPS_SHORT) && send_calls==2);
    assert(submitted_count==2 && submitted[1].ki.wVk==VK_LWIN);
    assert(submitted[1].ki.dwFlags & KEYEVENTF_KEYUP);
    puts("caps_windows decision / hook / SendInput tests passed");
    return 0;
}
