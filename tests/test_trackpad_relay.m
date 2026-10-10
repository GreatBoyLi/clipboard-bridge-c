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
int main(void) {
    @autoreleasepool {
        focus_pid = 200; cb_keyboard_connected(1);
        event_source = CGEventSourceCreate(kCGEventSourceStatePrivate);
        assert(event_source); assert(check_context());
        reject_alt = 0;
        assert(touch_context() == 200);
        three_finger_enabled=1; queue_touch_navigation(123, 200);
        assert(navigation_key == 123 && navigation_pid == 200);
        navigation_key = 0;
        focus_pid = 100; assert(touch_context() == 0);
        focus_pid = 200;
        navigation_key = 123; navigation_pid = 200;
        navigation_time = CFAbsoluteTimeGetCurrent();
        timer_callback(NULL, NULL);
        assert(emitted == 4);
        assert(emitted_keys[0] == 58 && emitted_types[0] == kCGEventFlagsChanged);
        assert(emitted_flags[0] & kCGEventFlagMaskAlternate);
        assert(emitted_keys[1] == 123 && emitted_types[1] == kCGEventKeyDown);
        assert(emitted_keys[2] == 123 && emitted_types[2] == kCGEventKeyUp);
        assert(emitted_keys[3] == 58 && emitted_flags[3] == 0);
        emitted = 0;
        navigation_key = 124; navigation_pid = 200;
        navigation_time = CFAbsoluteTimeGetCurrent() - 1;
        timer_callback(NULL, NULL); assert(emitted == 0); /* 不执行过期手势 */
        navigation_key = 124; navigation_pid = 201;
        navigation_time = CFAbsoluteTimeGetCurrent();
        timer_callback(NULL, NULL); assert(emitted == 0); /* 不发送给另一会话 */
        state.raw_mods = CB_MOD_CONTROL;
        navigation_key = 124; navigation_pid = 200;
        navigation_time = CFAbsoluteTimeGetCurrent();
        timer_callback(NULL, NULL); assert(emitted == 0); /* 防止组合成 Alt+Ctrl */
        cb_key_reset(&state, 0);
        /* 旧的滚动改写已删除；各种滚动事件都必须原样返回。 */
        for (int mode = 0; mode < 4; ++mode) {
            CGEventRef event=CGEventCreateScrollWheelEvent(event_source,
                mode==0 ? kCGScrollEventUnitLine : kCGScrollEventUnitPixel,2,4,mode==3 ? 1 : 0);
            if(mode==1) CGEventSetFlags(event,kCGEventFlagMaskControl);
            if(mode==2) focus_pid=100;
            CFDataRef before=CGEventCreateData(NULL,event);
            assert(key_callback(NULL,kCGEventScrollWheel,event,NULL)==event);
            CFDataRef after=CGEventCreateData(NULL,event);
            assert(CFEqual(before,after));assert(emitted==0);
            CFRelease(before);CFRelease(after);CFRelease(event);focus_pid=200;
        }
        scroll_lines_enabled=1;fake_speed_available=1;fake_speed=.525;
        assert(cb_keyboard_scroll_lines()==3);
        focus_pid=100;assert(cb_keyboard_scroll_lines()==0); /* 切走即恢复 */
        focus_pid=200;fake_speed=.8;assert(cb_keyboard_scroll_lines()==5);
        cb_keyboard_connected(0);assert(cb_keyboard_scroll_lines()==0); /* 断线恢复 */
        cb_keyboard_connected(1);scroll_lines_enabled=0;
        assert(cb_keyboard_scroll_lines()==0); /* 未启用不发送控制 */
        three_finger_enabled=0;
        queue_touch_navigation(123,200);assert(navigation_key==0);
        three_finger_enabled=1;
        navigation_key = 124; navigation_pid = 200;
        navigation_time = CFAbsoluteTimeGetCurrent();
        cb_keyboard_connected(0); timer_callback(NULL, NULL);
        assert(emitted == 0);
        CFRelease(event_source); event_source = NULL;
        puts("Trackpad event routing tests passed");
    }
}
