/* Windows Caps 映射：独立消息线程避免网络、编码工作拖延键盘钩子。 */
#ifndef CB_CAPS_TEST
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <string.h>
#include <stdio.h>
#include "caps_remote.h"

#define CB_CAPS_TAG ((ULONG_PTR)0x43424341u)
static int caps_on;
static unsigned char caps_down;
static unsigned char external_down[256];
static int caps_healthy;
/* 只统计本模块的控制按键，不记录用户输入内容。钩子内不输出日志。 */
static unsigned caps_seen, caps_blocked;
static int caps_win_observed, caps_space_observed;

#ifndef CB_CAPS_TEST
static int caps_input_pause(void);
#endif

/* 钩子只跟踪 Caps 和外部按住的键，不记录字符、不吞掉输入。 */
static LRESULT CALLBACK caps_hook(int code, WPARAM message, LPARAM parameter)
{
    LRESULT next = CallNextHookEx(NULL, code, message, parameter);
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT *key = (const KBDLLHOOKSTRUCT *)parameter;
        if (key->dwExtraInfo == CB_CAPS_TAG) {
            unsigned bit = key->vkCode == VK_LWIN ? 1u :
                           key->vkCode == VK_SPACE ? 2u :
                           key->vkCode == VK_CAPITAL ? 4u : 0u;
            if (message == WM_KEYUP || message == WM_SYSKEYUP) bit <<= 3;
            caps_seen |= bit;
            if (next != 0) caps_blocked |= bit;
        }
    }
    if (code == HC_ACTION && next == 0) {
        const KBDLLHOOKSTRUCT *key = (const KBDLLHOOKSTRUCT *)parameter;
        int down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        int up = message == WM_KEYUP || message == WM_SYSKEYUP;
        if (key->vkCode < 256u && (down || up)) {
            if (key->dwExtraInfo != CB_CAPS_TAG)
                external_down[key->vkCode] = (unsigned char)down;
            if (key->vkCode == VK_CAPITAL) {
                if (down && !caps_down) caps_on = !caps_on;
                caps_down = (unsigned char)down;
            }
        }
    }
    return next;
}

static void caps_key(INPUT *input, WORD key, DWORD flags)
{
    memset(input, 0, sizeof(*input));
    input->type = INPUT_KEYBOARD;
    input->ki.wVk = key;
    input->ki.dwFlags = flags;
    input->ki.dwExtraInfo = CB_CAPS_TAG;
}

/* 不释放用户按住的修饰键；任何键按住时保守拒绝整个动作。 */
static int caps_busy(void)
{
    int key;
    for (key = 8; key < 256; ++key) {
        if (external_down[key] || (GetAsyncKeyState(key) & 0x8000)) return 1;
    }
    return 0;
}

/* 只缓存钩子会报告的左右修饰键；聚合键的释放不会单独回调。 */
static void caps_seed_keys(void)
{
    int key;
    memset(external_down, 0, sizeof(external_down));
    for (key = 8; key < 256; ++key) {
        if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU) continue;
        external_down[key] = (unsigned char)((GetAsyncKeyState(key) & 0x8000) != 0);
    }
    caps_down = external_down[VK_CAPITAL];
}

static int caps_execute(unsigned action)
{
    INPUT inputs[4];
    INPUT release[2];
    unsigned char owned[256] = {0};
    UINT count, sent, i, releases = 0;
    caps_seen=caps_blocked=0;caps_win_observed=caps_space_observed=-1;
    if (!caps_healthy || (action != CB_CAPS_SHORT && action != CB_CAPS_LONG)) {
        fprintf(stderr,"[caps] request rejected: worker unavailable or invalid action\n");return 0;
    }
    if (caps_busy() || caps_down) {
        fprintf(stderr,"[caps] request rejected: a key is still held\n");return 0;
    }
    fprintf(stderr,"[caps] action=%s caps=%s target=%s\n",
            action==CB_CAPS_LONG?"long":"short",caps_on?"on":"off",
            (action==CB_CAPS_LONG || caps_on)?"CapsLock":"Win+Space");
    if (action == CB_CAPS_LONG || caps_on) {
        caps_key(&inputs[0], VK_CAPITAL, 0);
        caps_key(&inputs[1], VK_CAPITAL, KEYEVENTF_KEYUP);
        count = 2;
    } else {
        caps_key(&inputs[0], VK_LWIN, KEYEVENTF_EXTENDEDKEY);
        caps_key(&inputs[1], VK_SPACE, 0);
        caps_key(&inputs[2], VK_SPACE, KEYEVENTF_KEYUP);
        caps_key(&inputs[3], VK_LWIN, KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY);
        count = 4;
    }
    if (count == 4) {
        /* Win+空格由系统输入法服务处理。分阶段保留按住状态，
         * 避免四个事件瞬间提交后，处理切换时 Win 已经松开。
         * 等待期间继续处理钩子消息，并响应会话停止。 */
        sent = SendInput(1, inputs, (int)sizeof(INPUT));
        if (sent == 1 && caps_input_pause()) {
            caps_win_observed=(GetAsyncKeyState(VK_LWIN)&0x8000)!=0;
            sent += SendInput(1, &inputs[1], (int)sizeof(INPUT));
            if (sent == 2 && caps_input_pause()) {
                caps_space_observed=(GetAsyncKeyState(VK_SPACE)&0x8000)!=0;
                sent += SendInput(2, &inputs[2], (int)sizeof(INPUT));
            }
        }
    } else {
        sent = SendInput(count, inputs, (int)sizeof(INPUT));
    }
    fprintf(stderr,"[caps] submitted=%u/%u win-down=%d space-down=%d hook-seen=0x%02x hook-blocked=0x%02x\n",
            sent,count,caps_win_observed,caps_space_observed,caps_seen,caps_blocked);
    fflush(stderr);
    if (sent == count) return 1;
    /* 只清理确实已提交但尚未松开的本模块按键；外部按键优先。 */
    if (sent > count) sent = count;
    for (i = 0; i < sent; ++i)
        owned[inputs[i].ki.wVk] = (unsigned char)((inputs[i].ki.dwFlags & KEYEVENTF_KEYUP) == 0);
    for (i = sent; i > 0; --i) {
        WORD key = inputs[i - 1].ki.wVk;
        if (owned[key] && !external_down[key]) {
            caps_key(&release[releases++], key,
                     KEYEVENTF_KEYUP | (key == VK_LWIN ? KEYEVENTF_EXTENDEDKEY : 0));
            owned[key] = 0;
        }
    }
    if (releases != 0) {
        UINT cleaned = SendInput(releases, release, (int)sizeof(INPUT));
        (void)cleaned; /* 清理失败仍向调用方报告失败，不猜测系统状态。 */
    }
    caps_healthy = 0;
    return 0;
}

#ifndef CB_CAPS_TEST
static SRWLOCK caps_api_lock = SRWLOCK_INIT;
static HANDLE caps_thread, caps_ready, caps_stop, caps_request, caps_done;
static volatile LONG caps_enabled;
static unsigned caps_action;
static int caps_result;

/* 不是 Sleep：低级键盘钩子要求安装线程持续处理消息。 */
static int caps_input_pause(void)
{
    ULONGLONG deadline = GetTickCount64() + 40;
    MSG message;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        DWORD wait;
        if (WaitForSingleObject(caps_stop, 0) == WAIT_OBJECT_0) return 0;
        if (now >= deadline) return 1;
        wait = MsgWaitForMultipleObjects(1, &caps_stop, FALSE,
                                        (DWORD)(deadline - now), QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) return 0;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return 0;
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (WaitForSingleObject(caps_stop, 0) == WAIT_OBJECT_0) return 0;
        }
    }
}


static DWORD WINAPI caps_worker(void *unused)
{
    MSG message;
    HANDLE events[2];
    HHOOK hook;
    (void)unused;
    PeekMessageW(&message, NULL, 0, 0, PM_NOREMOVE);
    memset(external_down, 0, sizeof(external_down));
    /* 新消息队列只取初始快照；之后不从网络线程的旧队列读取开关。 */
    caps_on = (GetKeyState(VK_CAPITAL) & 1) != 0;
    caps_seed_keys();
    hook = SetWindowsHookExW(WH_KEYBOARD_LL, caps_hook, GetModuleHandleW(NULL), 0);
    caps_healthy = hook != NULL;
    SetEvent(caps_ready);
    if (hook == NULL) return 0;
    events[0] = caps_stop;
    events[1] = caps_request;
    for (;;) {
        DWORD wait = MsgWaitForMultipleObjects(2, events, FALSE, INFINITE, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
        /* 先处理输入，使连续的本地 Caps 变化先于远程动作生效。 */
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT ||
                WaitForSingleObject(caps_stop, 0) == WAIT_OBJECT_0) goto stopped;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (wait == WAIT_OBJECT_0 + 1) {
            caps_result = 0;
            if (InterlockedCompareExchange(&caps_enabled, 0, 0) != 0 &&
                WaitForSingleObject(caps_stop, 0) != WAIT_OBJECT_0)
                caps_result = caps_execute(caps_action);
            if (!caps_healthy) InterlockedExchange(&caps_enabled, 0);
            SetEvent(caps_done);
        }
    }
stopped:
    InterlockedExchange(&caps_enabled, 0);
    UnhookWindowsHookEx(hook);
    caps_healthy = 0;
    return 0;
}

static void caps_close_locked(void)
{
    InterlockedExchange(&caps_enabled, 0);
    if (caps_stop != NULL) SetEvent(caps_stop);
    /* 完整等待线程退出，避免断线之后仍执行旧会话的动作。 */
    if (caps_thread != NULL) {
        WaitForSingleObject(caps_thread, INFINITE);
        CloseHandle(caps_thread);
    }
    if (caps_ready != NULL) CloseHandle(caps_ready);
    if (caps_stop != NULL) CloseHandle(caps_stop);
    if (caps_request != NULL) CloseHandle(caps_request);
    if (caps_done != NULL) CloseHandle(caps_done);
    caps_thread = caps_ready = caps_stop = caps_request = caps_done = NULL;
}

int cb_caps_remote_init(void)
{
    int ok = 0;
    AcquireSRWLockExclusive(&caps_api_lock);
    if (caps_thread != NULL) {
        ok = InterlockedCompareExchange(&caps_enabled, 0, 0) != 0;
        goto finished;
    }
    caps_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    caps_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    caps_request = CreateEventW(NULL, FALSE, FALSE, NULL);
    caps_done = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (caps_ready == NULL || caps_stop == NULL || caps_request == NULL || caps_done == NULL) goto failed;
    caps_thread = CreateThread(NULL, 0, caps_worker, NULL, 0, NULL);
    if (caps_thread == NULL) goto failed;
    if (WaitForSingleObject(caps_ready, 3000) != WAIT_OBJECT_0 || !caps_healthy) goto failed;
    InterlockedExchange(&caps_enabled, 1);
    ok = 1;
    goto finished;
failed:
    caps_close_locked();
finished:
    ReleaseSRWLockExclusive(&caps_api_lock);
    return ok;
}

void cb_caps_remote_close(void)
{
    /* 先取消未执行请求，再等待正在提交的动作结束。 */
    InterlockedExchange(&caps_enabled, 0);
    AcquireSRWLockExclusive(&caps_api_lock);
    caps_close_locked();
    ReleaseSRWLockExclusive(&caps_api_lock);
}

int cb_caps_remote_action(unsigned action)
{
    int result = 0;
    if (action != CB_CAPS_SHORT && action != CB_CAPS_LONG) return 0;
    AcquireSRWLockExclusive(&caps_api_lock);
    if (caps_thread == NULL || InterlockedCompareExchange(&caps_enabled, 0, 0) == 0) goto finished;
    caps_action = action;
    caps_result = 0;
    ResetEvent(caps_done);
    if (!SetEvent(caps_request)) goto finished;
    if (WaitForSingleObject(caps_done, 3000) == WAIT_OBJECT_0) {
        result = caps_result;
    } else {
        /* 超时请求不可留到下一次连接：同步停机并清理。 */
        caps_close_locked();
    }
finished:
    ReleaseSRWLockExclusive(&caps_api_lock);
    return result;
}
#endif
