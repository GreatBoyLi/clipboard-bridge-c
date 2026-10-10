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
static int fake_speed_available;
static double fake_speed;
#define cb_touch_speed test_touch_speed
#include "../src/keyboard_macos.m"
#undef cb_touch_speed
int test_touch_speed(pid_t pid,double *speed) {
    if(pid!=200 || !fake_speed_available) return 0;
    *speed=fake_speed;return 1;
}
int main(void) { @autoreleasepool {
    focus_pid=200;cb_keyboard_connected(1);
    event_source=CGEventSourceCreate(kCGEventSourceStatePrivate);assert(event_source);
    caps_enabled=1;assert(check_context());
    caps_changed(1,7,cb_caps_hid_now());caps_changed(0,7,cb_caps_hid_now());
    assert(cb_keyboard_caps_take()==0); // Windows 未就绪，不能请求。
    cb_keyboard_caps_ready(1);
    caps_changed(1,7,cb_caps_hid_now());caps_changed(0,7,cb_caps_hid_now());
    assert(cb_keyboard_caps_take()==CB_CAPS_SHORT);
    assert(cb_keyboard_caps_take()==0);
    caps_changed(1,7,cb_caps_hid_now());
    caps_hold.pressed_at-=1000;
    timer_callback(NULL,NULL);assert(cb_keyboard_caps_take()==CB_CAPS_LONG);
    timer_callback(NULL,NULL);assert(cb_keyboard_caps_take()==0);
    caps_changed(0,7,cb_caps_hid_now());assert(cb_keyboard_caps_take()==0);
    CGEventRef event=CGEventCreateKeyboardEvent(event_source,57,true);
    CGEventSetType(event,kCGEventFlagsChanged);CGEventSetFlags(event,kCGEventFlagMaskAlphaShift);
    emitted=0;assert(key_callback(NULL,kCGEventFlagsChanged,event,NULL)==NULL && emitted==0);
    CGEventSetIntegerValueField(event,kCGKeyboardEventKeycode,CB_MAC_A);
    CGEventSetType(event,kCGEventKeyDown);
    assert(key_callback(NULL,kCGEventKeyDown,event,NULL)==NULL && emitted==1);
    assert(!(emitted_flags[0]&kCGEventFlagMaskAlphaShift));
    wait_neutral=1;
    CGEventSetFlags(event,kCGEventFlagMaskAlphaShift|kCGEventFlagMaskShift);
    assert(key_callback(NULL,kCGEventKeyDown,event,NULL)==event);
    assert(!(CGEventGetFlags(event)&kCGEventFlagMaskAlphaShift));
    CGEventSetFlags(event,kCGEventFlagMaskAlphaShift);
    CGEventSetType(event,kCGEventFlagsChanged);
    assert(key_callback(NULL,kCGEventFlagsChanged,event,NULL)==event);
    assert(!(CGEventGetFlags(event)&kCGEventFlagMaskAlphaShift));
    release_state();
    caps_changed(1,7,cb_caps_hid_now());
    focus_pid=100;timer_callback(NULL,NULL);
    focus_pid=200;assert(check_context());caps_changed(0,7,cb_caps_hid_now());
    assert(cb_keyboard_caps_take()==0); // 前台变化取消按住。
    caps_changed(1,7,cb_caps_hid_now());caps_changed(0,7,cb_caps_hid_now());
    cb_keyboard_connected(0);assert(cb_keyboard_caps_take()==0);
    cb_keyboard_connected(1);cb_keyboard_caps_ready(1);assert(check_context());
    caps_changed(1,7,cb_caps_hid_now());caps_changed(-1,7,cb_caps_hid_now());
    caps_changed(0,7,cb_caps_hid_now());assert(cb_keyboard_caps_take()==0);
    CFRelease(event);release_state();CFRelease(event_source);event_source=NULL;
    puts("Caps relay, native suppression, readiness and cancellation tests passed");
} }
