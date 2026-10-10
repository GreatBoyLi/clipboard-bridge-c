#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <assert.h>
/* 模拟主线程不运行事件循环时的旧缓存，以及直接前台进程查询。 */
@interface TestApp : NSObject
@property pid_t pid;
+ (instancetype)runningApplicationWithProcessIdentifier:(pid_t)pid;
- (NSString *)bundleIdentifier;
- (pid_t)processIdentifier;
@end
@implementation TestApp
+ (instancetype)runningApplicationWithProcessIdentifier:(pid_t)pid {
    TestApp *app = [TestApp new]; app.pid = pid; return app;
}
- (NSString *)bundleIdentifier { return self.pid == 200 ? @"com.moonlight-stream.Moonlight" : @"com.apple.Terminal"; }
- (pid_t)processIdentifier { return self.pid; }
@end
@interface TestWorkspace : NSObject
+ (instancetype)sharedWorkspace;
- (TestApp *)frontmostApplication;
@end
@implementation TestWorkspace
+ (instancetype)sharedWorkspace { return [TestWorkspace new]; }
- (TestApp *)frontmostApplication { return [TestApp runningApplicationWithProcessIdentifier:100]; }
@end
static int focus_pid = 100, query_fail;
static CGEventFlags test_flags(CGEventSourceStateID state) { (void)state; return 0; }
#define CGEventSourceFlagsState test_flags
/* 模拟直接进程查询，前台 PID 会变化，不受旧工作区缓存影响。 */
static OSErr test_front(ProcessSerialNumber *psn) {
    if (query_fail) return procNotFound;
    psn->highLongOfPSN = 0;
    psn->lowLongOfPSN = (UInt32)focus_pid;
    return noErr;
}
static OSStatus test_process_pid(const ProcessSerialNumber *psn, pid_t *pid) {
    *pid = (pid_t)psn->lowLongOfPSN; return noErr;
}
#define GetFrontProcess test_front
#define GetProcessPID test_process_pid
#define NSRunningApplication TestApp
#define NSWorkspace TestWorkspace
/* 截获输出，不把测试按键发给任何真实应用。 */
static int emitted, reject_alt = 1;
static unsigned short emitted_keys[32];
static CGEventFlags emitted_flags[32];
static CGEventType emitted_types[32];
static void record_event(CGEventRef event) {
    assert(emitted < 32);
    CGEventFlags flags = CGEventGetFlags(event);
    if (reject_alt) assert(!(flags & kCGEventFlagMaskAlternate));
    emitted_keys[emitted] = (unsigned short)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
    emitted_flags[emitted] = flags;
    emitted_types[emitted++] = CGEventGetType(event);
}
static void test_post(CGEventTapProxy proxy, CGEventRef event) { (void)proxy; record_event(event); }
static void test_post_pid(pid_t pid, CGEventRef event) { assert(pid == 200); record_event(event); }
#define CGEventTapPostEvent test_post
#define CGEventPostToPid test_post_pid
#include "../src/keyboard_macos.m"
static void input(CGEventType type, CGKeyCode key, CGEventFlags flags) {
    CGEventRef event = CGEventCreateKeyboardEvent(event_source, key, type != kCGEventKeyUp);
    assert(event);
    CGEventSetType(event, type); CGEventSetFlags(event, flags);
    assert(key_callback((CGEventTapProxy)(uintptr_t)1, type, event, NULL) == NULL);
    CFRelease(event);
}
int main(void) {
    @autoreleasepool {
        focus_pid = 200; cb_keyboard_connected(1);
        event_source = CGEventSourceCreate(kCGEventSourceStatePrivate);
        assert(event_source);
        input(kCGEventFlagsChanged, 55, kCGEventFlagMaskCommand);
        assert(emitted == 0); // Command 尚未决定用途，不能先发 Alt。
        input(kCGEventKeyDown, CB_MAC_C, kCGEventFlagMaskCommand);
        input(kCGEventKeyUp, CB_MAC_C, kCGEventFlagMaskCommand);
        input(kCGEventFlagsChanged, 55, 0);
        assert(emitted == 4);
        assert(emitted_keys[0] == 59 && (emitted_flags[0] & kCGEventFlagMaskControl));
        assert(emitted_keys[1] == CB_MAC_INSERT && emitted_types[1] == kCGEventKeyDown);
        assert(emitted_keys[2] == CB_MAC_INSERT && emitted_types[2] == kCGEventKeyUp);
        assert(emitted_keys[3] == 59 && emitted_flags[3] == 0);
        emitted = 0;
        input(kCGEventFlagsChanged, 55, kCGEventFlagMaskCommand);
        input(kCGEventKeyDown, CB_MAC_V, kCGEventFlagMaskCommand);
        input(kCGEventKeyUp, CB_MAC_V, kCGEventFlagMaskCommand);
        assert(emitted == 0 && paste_phase == 1);
        /* 请求只交接状态，不读取 Mac 的旧文字；无待同步文字也能原生粘贴。 */
        assert(cb_keyboard_take_paste() == 1 && paste_phase == 2);
        assert(cb_keyboard_take_paste() == 0);
        cb_keyboard_finish_paste(1);
        timer_callback(NULL, NULL);
        assert(emitted == 4 && emitted_keys[0] == 56);
        assert(emitted_keys[1] == CB_MAC_INSERT && emitted_types[1] == kCGEventKeyDown);
        assert(emitted_keys[2] == CB_MAC_INSERT && emitted_types[2] == kCGEventKeyUp);
        input(kCGEventFlagsChanged, 55, 0);
        assert(emitted == 4); // 松开 Command 不再发送迟到的 Alt。
        emitted = 0;
        input(kCGEventFlagsChanged, 55, kCGEventFlagMaskCommand);
        input(kCGEventKeyDown, CB_MAC_A, kCGEventFlagMaskCommand);
        input(kCGEventKeyUp, CB_MAC_A, kCGEventFlagMaskCommand);
        input(kCGEventFlagsChanged, 55, 0);
        assert(emitted == 4);
        assert(emitted_keys[0] == 59 && (emitted_flags[0] & kCGEventFlagMaskControl));
        assert(emitted_keys[1] == CB_MAC_A && emitted_types[1] == kCGEventKeyDown);
        assert(emitted_keys[2] == CB_MAC_A && emitted_types[2] == kCGEventKeyUp);
        assert(emitted_keys[3] == 59 && emitted_flags[3] == 0);
        /* 新组合全部生成 Ctrl 按下 → 原键按下/松开 → Ctrl 松开，不包含 Alt。 */
        const CGKeyCode common[] = {CB_MAC_S, CB_MAC_F, CB_MAC_X, CB_MAC_T, CB_MAC_R};
        for (unsigned i = 0; i < sizeof(common) / sizeof(common[0]); ++i) {
            emitted = 0;
            input(kCGEventFlagsChanged, 55, kCGEventFlagMaskCommand);
            input(kCGEventKeyDown, common[i], kCGEventFlagMaskCommand);
            input(kCGEventKeyUp, common[i], kCGEventFlagMaskCommand);
            input(kCGEventFlagsChanged, 55, 0);
            assert(emitted == 4);
            assert(emitted_keys[0] == 59 && (emitted_flags[0] & kCGEventFlagMaskControl));
            assert(emitted_keys[1] == common[i] && emitted_types[1] == kCGEventKeyDown);
            assert(emitted_keys[2] == common[i] && emitted_types[2] == kCGEventKeyUp);
            assert(emitted_keys[3] == 59 && emitted_flags[3] == 0);
        }
        emitted = 0; reject_alt = 0;
        input(kCGEventFlagsChanged, 55, kCGEventFlagMaskCommand);
        input(kCGEventFlagsChanged, 55, 0);
        assert(emitted == 2 && emitted_keys[0] == 58 && emitted_keys[1] == 58);
        assert(emitted_flags[0] & kCGEventFlagMaskAlternate);
        assert(emitted_flags[1] == 0);
        CFRelease(event_source); event_source = NULL;
        puts("Mac command event relay tests passed");
    }
}
