#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include "keyboard.h"
#include "keyboard_map.h"
#include "protocol.h"
#include "scroll_lines.h"
#include "multitouch.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 所有映射状态只由事件线程访问；主线程只通过互斥锁传递连接/粘贴状态。 */
static pthread_t worker;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t started = PTHREAD_COND_INITIALIZER;
static int running, startup_status, connected, redo_mode, stopping;
/* 0=无请求，1=等待取快照，2=等待文字队列清空，3=可执行，4=取消。 */
static int paste_phase;
static pid_t paste_pid;
/* 主线程只交接 swipe 请求，所有实际按键仍由既有事件线程串行发送。 */
static int three_finger_enabled, scroll_lines_enabled;
static unsigned short navigation_key;
static pid_t navigation_pid;
static CFAbsoluteTime navigation_time;
static CFRunLoopRef worker_loop;
static CFMachPortRef tap;
static CGEventSourceRef event_source;
static cb_key_state state;
static unsigned forwarded_mods;
static int context_active, wait_neutral;
static pid_t target_pid;
/* 可选诊断只报告前台状态、修饰键和专用组合键，不记录正文或普通输入。 */
static int debug_enabled;
static unsigned debug_events;
static pid_t debug_front_pid = -1;
static int debug_connection = -1;

static unsigned physical_mods(CGEventFlags flags)
{
    unsigned result = 0;
    if (flags & kCGEventFlagMaskControl) result |= CB_MOD_CONTROL;
    if (flags & kCGEventFlagMaskAlternate) result |= CB_MOD_OPTION;
    if (flags & kCGEventFlagMaskCommand) result |= CB_MOD_COMMAND;
    if (flags & kCGEventFlagMaskShift) result |= CB_MOD_SHIFT;
    return result;
}
static CGEventFlags mapped_flags(unsigned mods)
{
    CGEventFlags flags = 0;
    if (mods & CB_MOD_CONTROL) flags |= kCGEventFlagMaskControl;
    if (mods & CB_MOD_OPTION) flags |= kCGEventFlagMaskAlternate;
    if (mods & CB_MOD_COMMAND) flags |= kCGEventFlagMaskCommand;
    if (mods & CB_MOD_SHIFT) flags |= kCGEventFlagMaskShift;
    return flags;
}

/* 回调内使用同一事件流，定时器内则只向指定 Moonlight 进程投递。 */
static void post_event(CGEventTapProxy proxy, CGEventRef event)
{
    if (proxy) CGEventTapPostEvent(proxy, event);
    else CGEventPostToPid(target_pid, event);
}
static void post_key(CGEventTapProxy proxy, unsigned short key, int down, unsigned mods)
{
    CGEventRef event = CGEventCreateKeyboardEvent(event_source, key, down);
    if (!event) return;
    CGEventSetFlags(event, mapped_flags(mods));
    post_event(proxy, event);
    CFRelease(event);
}

/* 先释放多余修饰键，再按下需要的键，避免生成 Alt+Ctrl+Insert 等错误组合。 */
static void sync_mods(CGEventTapProxy proxy, unsigned wanted, CGEventFlags extras)
{
    static const unsigned bits[] = {CB_MOD_CONTROL, CB_MOD_OPTION, CB_MOD_COMMAND, CB_MOD_SHIFT};
    static const CGKeyCode codes[] = {59, 58, 55, 56};
    for (int press = 0; press < 2; ++press) {
        for (unsigned i = 0; i < 4; ++i) {
            int has = (forwarded_mods & bits[i]) != 0;
            int needs = (wanted & bits[i]) != 0;
            if (has == needs || needs != press) continue;
            CGEventRef event = CGEventCreateKeyboardEvent(event_source, codes[i], needs);
            if (!event) continue;
            if (needs) forwarded_mods |= bits[i];
            else forwarded_mods &= ~bits[i];
            CGEventSetType(event, kCGEventFlagsChanged);
            CGEventSetFlags(event, mapped_flags(forwarded_mods) | extras);
            post_event(proxy, event);
            CFRelease(event);
        }
    }
}

/* 离开 Moonlight、断线或退出时，只向原进程释放已转换的按键。 */
static void release_state(void)
{
    if (target_pid > 0) {
        for (unsigned i = 0; i < CB_KEY_COUNT; ++i) {
            cb_held_key *held = &state.held[i];
            if (held->down && !held->suppress)
                post_key(NULL, held->output, 0, forwarded_mods);
        }
        sync_mods(NULL, 0, 0);
    }
    cb_key_reset(&state, redo_mode);
    forwarded_mods = 0;
    pthread_mutex_lock(&mutex);
    paste_phase = 0;
    paste_pid = 0;
    navigation_key = 0;
    pthread_mutex_unlock(&mutex);
}

/* 两个线程各自直接查询前台进程，不能跨线程读取映射状态。 */
static pid_t foreground_moonlight(void)
{
    @autoreleasepool {
        pid_t focused_pid = 0;
        ProcessSerialNumber psn = {0, 0};
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        OSStatus focus_error = GetFrontProcess(&psn);
        if (focus_error == noErr) focus_error = GetProcessPID(&psn, &focused_pid);
#pragma clang diagnostic pop
        if (focus_error != noErr || focused_pid <= 0) return 0;
        NSRunningApplication *app = focused_pid > 0 ?
            [NSRunningApplication runningApplicationWithProcessIdentifier:focused_pid] : nil;
        return [[app bundleIdentifier] isEqualToString:@"com.moonlight-stream.Moonlight"] ? focused_pid : 0;
    }
}

/* 底层触点回调只读系统上下文，不访问事件线程独占的映射状态。 */
static pid_t touch_context(void)
{
    pthread_mutex_lock(&mutex);
    int ready = connected;
    pthread_mutex_unlock(&mutex);
    if (!ready || physical_mods(CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState))) return 0;
    return foreground_moonlight();
}
static void queue_touch_navigation(unsigned short key, pid_t pid)
{
    pthread_mutex_lock(&mutex);
    if (three_finger_enabled && connected && !navigation_key) {
        navigation_key = key;
        navigation_pid = pid;
        navigation_time = CFAbsoluteTimeGetCurrent();
    }
    pthread_mutex_unlock(&mutex);
}

static int check_context(void)
{
    @autoreleasepool {
        pthread_mutex_lock(&mutex);
        int is_connected = connected;
        pthread_mutex_unlock(&mutex);
        /* 使用进程管理器直接取得前台 PID：不依赖 NSWorkspace 主循环缓存，
         * 也不依赖可能失败的 AXFocusedApplication 查询。
         * 这两个公开兼容接口虽已标为 deprecated，仍支持当前 macOS；
         * 抑制范围仅限此处，不能隐藏其他模块的弃用警告。
         */
        pid_t focused_pid = 0;
        ProcessSerialNumber psn = {0, 0};
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        OSStatus focus_error = GetFrontProcess(&psn);
        if (focus_error == noErr) focus_error = GetProcessPID(&psn, &focused_pid);
#pragma clang diagnostic pop
        if (focus_error != noErr || focused_pid <= 0) {
            focused_pid = 0;
            static OSStatus last_focus_error = noErr;
            if (debug_enabled && focus_error != last_focus_error)
                fprintf(stderr, "[keyboard] foreground process query failed: %d\n", (int)focus_error);
            last_focus_error = focus_error;
        }
        NSRunningApplication *app = focused_pid > 0 ?
            [NSRunningApplication runningApplicationWithProcessIdentifier:focused_pid] : nil;
        int active = is_connected &&
            [[app bundleIdentifier] isEqualToString:@"com.moonlight-stream.Moonlight"];
        pid_t front_pid = [app processIdentifier];
        if (debug_enabled && (front_pid != debug_front_pid || is_connected != debug_connection)) {
            fprintf(stderr, "[keyboard] foreground=%s pid=%d connected=%d active=%d\n",
                [[app bundleIdentifier] UTF8String] ?: "unknown", (int)front_pid,
                is_connected, active);
            debug_front_pid = front_pid;
            debug_connection = is_connected;
        }
        pid_t pid = active ? front_pid : 0;
        if (active != context_active || pid != target_pid) {
            release_state();
            context_active = active;
            target_pid = pid;
            /* 不接管进入前已经按住的修饰键，避免收到没有对应按下的松开事件。 */
            wait_neutral = active && physical_mods(
                CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState)) != 0;
        }
        return context_active;
    }
}

static int primary_modifier(unsigned short key)
{
    return key == 54 || key == 55 || key == 56 || key == 58 ||
           key == 59 || key == 60 || key == 61 || key == 62;
}
static CGEventRef key_callback(CGEventTapProxy proxy, CGEventType type,
                               CGEventRef event, void *user)
{
    (void)user;
    @autoreleasepool {
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            release_state();
            wait_neutral = 1;
            if (debug_enabled) fprintf(stderr, "[keyboard] tap disabled; enabling again\n");
            CGEventTapEnable(tap, true);
            return event;
        }
        if (type != kCGEventScrollWheel && debug_enabled && debug_events == 0) {
            fprintf(stderr, "[keyboard] event tap received its first keyboard event\n");
            ++debug_events;
        }
        /* 滚动全部透传，既不缩放，也不量化/补发。 */
        if (type == kCGEventScrollWheel) return event;
        if (!check_context()) return event;
        CGEventFlags flags = CGEventGetFlags(event);
        unsigned physical = physical_mods(flags);
        if (wait_neutral) {
            if (physical == 0) {
                wait_neutral = 0;
                if (debug_enabled) fprintf(stderr, "[keyboard] modifiers released; mapping ready\n");
            }
            return event;
        }
        unsigned short key = (unsigned short)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
        int kind = type == kCGEventFlagsChanged ? CB_KEY_FLAGS :
                   type == kCGEventKeyDown ? CB_KEY_DOWN : CB_KEY_UP;
        cb_key_result result = cb_key_process(&state, kind, key, physical);
        if (debug_enabled && debug_events < 80 &&
            (type == kCGEventFlagsChanged || result.paste_request ||
             (physical & CB_MOD_COMMAND &&
              (key == CB_MAC_A || key == CB_MAC_C || key == CB_MAC_Z ||
                    key == CB_MAC_S || key == CB_MAC_F || key == CB_MAC_X ||
                    key == CB_MAC_T || key == CB_MAC_R)))) {
            ++debug_events;
            fprintf(stderr, "[keyboard] type=%d key=%u mods=%u output=%u before=%u after=%u paste=%d\n",
                kind, key, physical, result.key, result.before, result.after, result.paste_request);
        }
        /* 丢弃设备相关左右位，只保留 Caps Lock、Fn 和数字键盘属性。 */
        CGEventFlags extras = flags & (kCGEventFlagMaskAlphaShift |
            kCGEventFlagMaskSecondaryFn | kCGEventFlagMaskNumericPad);
        sync_mods(proxy, result.before, extras);
        if (result.paste_request) {
            pthread_mutex_lock(&mutex);
            if (paste_phase == 0) {
                paste_phase = 1;
                paste_pid = target_pid;
            }
            pthread_mutex_unlock(&mutex);
        }
        if ((!result.suppress && type != kCGEventFlagsChanged) ||
            (type == kCGEventFlagsChanged && !primary_modifier(key))) {
            CGEventRef output = CGEventCreateCopy(event);
            if (output) {
                CGEventSetFlags(output, mapped_flags(result.before) | extras);
                if (result.key != key) {
                    CGEventSetIntegerValueField(output, kCGKeyboardEventKeycode, result.key);
                    CGEventKeyboardSetUnicodeString(output, 0, NULL);
                }
                post_event(proxy, output);
                CFRelease(output);
            }
        }
        sync_mods(proxy, result.after, extras);
        if (result.alt_tap) {
            /* 只有未参与组合的独立 Command 才补发 Alt 点击。 */
            sync_mods(proxy, result.after | CB_MOD_OPTION, extras);
            sync_mods(proxy, result.after, extras);
        }
        /* 原始事件已转换并按顺序投递，不能再让 Moonlight 收到第二份。 */
        return NULL;
    }
}

static void timer_callback(CFRunLoopTimerRef timer, void *user)
{
    (void)timer; (void)user;
    if (!check_context()) return;
    pthread_mutex_lock(&mutex);
    unsigned short nav = navigation_key;
    pid_t nav_pid = navigation_pid;
    CFAbsoluteTime nav_time = navigation_time;
    navigation_key = 0;
    pthread_mutex_unlock(&mutex);
    int held = 0;
    for (unsigned i = 0; i < CB_KEY_COUNT; ++i) held |= state.held[i].down;
    /* 不把过期手势、切换窗口前的手势或带有其他按键的手势延迟注入。 */
    if (nav && nav_pid == target_pid && CFAbsoluteTimeGetCurrent() - nav_time <= 0.25 &&
        !wait_neutral && !held && !state.raw_mods &&
        !physical_mods(CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState))) {
        sync_mods(NULL, CB_MOD_OPTION, 0);
        post_key(NULL, nav, 1, CB_MOD_OPTION);
        post_key(NULL, nav, 0, CB_MOD_OPTION);
        sync_mods(NULL, 0, 0);
        if (debug_enabled) fprintf(stderr, "[trackpad] navigation posted to Moonlight\n");
    }
    pthread_mutex_lock(&mutex);
    int phase = paste_phase;
    int valid = connected && paste_pid == target_pid;
    if (phase == 3 || phase == 4) paste_phase = 0;
    pthread_mutex_unlock(&mutex);
    if (phase != 3 || !valid || wait_neutral) return;
    if (debug_enabled) fprintf(stderr, "[keyboard] paste ready; posting Shift+Insert using Windows clipboard\n");
    /* 无待同步文字，或所有待同步文字均已确认：使用 Windows 当前剪贴板。 */
    sync_mods(NULL, CB_MOD_SHIFT, 0);
    post_key(NULL, CB_MAC_INSERT, 1, CB_MOD_SHIFT);
    post_key(NULL, CB_MAC_INSERT, 0, CB_MOD_SHIFT);
    sync_mods(NULL, cb_key_desired_mods(&state), 0);
}

static void *event_thread(void *unused)
{
    (void)unused;
    @autoreleasepool {
        cb_key_reset(&state, redo_mode);
        event_source = CGEventSourceCreate(kCGEventSourceStatePrivate);
        tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
            kCGEventTapOptionDefault,
            CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) |
            CGEventMaskBit(kCGEventFlagsChanged), key_callback, NULL);
        CFRunLoopSourceRef source = tap ? CFMachPortCreateRunLoopSource(NULL, tap, 0) : NULL;
        CFRunLoopTimerRef timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent(),
            0.05, 0, 0, timer_callback, NULL);
        int ok = event_source && tap && source && timer;
        if (ok) {
            CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
            CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopCommonModes);
        }
        pthread_mutex_lock(&mutex);
        worker_loop = ok ? (CFRunLoopRef)CFRetain(CFRunLoopGetCurrent()) : NULL;
        startup_status = ok ? 1 : -1;
        pthread_cond_signal(&started);
        pthread_mutex_unlock(&mutex);
        /* 分段运行并检查退出标记，避免初始化刚完成时 Stop 先于 Run 的竞态。 */
        while (ok) {
            pthread_mutex_lock(&mutex);
            int done = stopping;
            pthread_mutex_unlock(&mutex);
            if (done) break;
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
        }
        release_state();
        if (tap) { CGEventTapEnable(tap, false); CFMachPortInvalidate(tap); }
        if (timer) { CFRunLoopTimerInvalidate(timer); CFRelease(timer); }
        if (source) CFRelease(source);
        if (tap) { CFRelease(tap); tap = NULL; }
        if (event_source) { CFRelease(event_source); event_source = NULL; }
        return NULL;
    }
}
int cb_keyboard_init(int redo_shift_z)
{
    if (running) return 0;
    if (!AXIsProcessTrusted()) {
        fprintf(stderr, "Keyboard mapping needs macOS Accessibility permission.\n");
        return -1;
    }
    const char *debug = getenv("CB_KEYBOARD_DEBUG");
    debug_enabled = debug && strcmp(debug, "1") == 0;
    if (debug_enabled) fprintf(stderr, "[keyboard] diagnostics enabled; no clipboard text is logged\n");
    const char *swipe_option = getenv("CB_THREE_FINGER");
    const char *scroll_option = getenv("CB_SCROLL_LINES");
    three_finger_enabled = swipe_option && strcmp(swipe_option, "1") == 0;
    scroll_lines_enabled = scroll_option && strcmp(scroll_option, "1") == 0;
    redo_mode = redo_shift_z;
    startup_status = 0;
    stopping = 0;
    if (pthread_create(&worker, NULL, event_thread, NULL) != 0) return -1;
    pthread_mutex_lock(&mutex);
    while (startup_status == 0) pthread_cond_wait(&started, &mutex);
    int ok = startup_status == 1;
    pthread_mutex_unlock(&mutex);
    running = 1;
    if (!ok) { cb_keyboard_close(); return -1; }
    if (three_finger_enabled || scroll_lines_enabled) {
        if (cb_touch_start(touch_context, queue_touch_navigation, debug_enabled) != 0) {
            cb_keyboard_close(); return -1;
        }
        if(three_finger_enabled) puts("Raw three-finger navigation enabled (right=back, left=forward).");
    }
    if (scroll_lines_enabled) puts("Windows scroll line control enabled (linear 1-5; native scroll unchanged).");
    puts("Keyboard mapping enabled for foreground Moonlight while connected.");
    return 0;
}
void cb_keyboard_close(void)
{
    if (!running) return;
    if (three_finger_enabled || scroll_lines_enabled) cb_touch_stop();
    pthread_mutex_lock(&mutex);
    connected = 0;
    stopping = 1;
    if (worker_loop) { CFRunLoopStop(worker_loop); CFRunLoopWakeUp(worker_loop); }
    pthread_mutex_unlock(&mutex);
    pthread_join(worker, NULL);
    if (worker_loop) { CFRelease(worker_loop); worker_loop = NULL; }
    running = 0;
}
void cb_keyboard_connected(int value)
{
    pthread_mutex_lock(&mutex);
    connected = value;
    if (!value) { paste_phase = 0; navigation_key = 0; }
    pthread_mutex_unlock(&mutex);
}
int cb_keyboard_take_paste(void)
{
    /* 部分系统的底层输入源需要主 CFRunLoop；无需创建 NSApplication 或抢焦点。 */
    if (three_finger_enabled || scroll_lines_enabled) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.001, false);
    /* 这里只交接请求。文字同步沿用平台的版本检测，不能每次粘贴都重发旧文字：
     * Windows 文件剪贴板不会同步到 Mac，重发旧文字会把文件覆盖掉。
     */
    pthread_mutex_lock(&mutex);
    int take = paste_phase == 1;
    if (take) paste_phase = 2;
    pthread_mutex_unlock(&mutex);
    return take;
}
void cb_keyboard_finish_paste(int success)
{
    pthread_mutex_lock(&mutex);
    if (paste_phase == 2) paste_phase = success ? 3 : 4;
    pthread_mutex_unlock(&mutex);
}

/* 由网络主线程调用；前台查询不使用事件线程独占状态。
 * 短暂丢帧/离手后的惯性保留最后行数 500ms，离开 Moonlight 则立即恢复。
 */
unsigned cb_keyboard_scroll_lines(void)
{
    static cb_lines_filter filter;
    static pid_t previous_pid;
    static CFAbsoluteTime sampled;
    pid_t pid=scroll_lines_enabled ? touch_context() : 0;
    CFAbsoluteTime now=CFAbsoluteTimeGetCurrent();
    if(!pid || pid!=previous_pid) {memset(&filter,0,sizeof(filter));sampled=0;}
    previous_pid=pid;
    if(!pid) return 0;
    double speed;
    if(cb_touch_speed(pid,&speed)) {
        sampled=now;
        unsigned before=filter.lines;
        unsigned lines=cb_lines_update(&filter,speed,(uint64_t)(now*1000));
        if(debug_enabled && lines!=before)
            fprintf(stderr,"[trackpad] speed=%.3f height/s -> Windows %u lines (native scroll unchanged)\n",speed,lines);
        return lines;
    }
    if(sampled && now-sampled<.5) return filter.lines;
    memset(&filter,0,sizeof(filter));return 0;
}
