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
#include "../src/keyboard_macos.m"
int main(void) {
    @autoreleasepool {
        cb_keyboard_connected(1);
        assert(check_context() == 0); // 终端
        focus_pid = 200;
        assert(check_context() == 1 && target_pid == 200); // 切到 Moonlight
        focus_pid = 100;
        assert(check_context() == 0); // 切回 Mac 应用
        focus_pid = 200;
        assert(check_context() == 1);
        query_fail = 1;
        assert(check_context() == 0); // 查询失败不能继续使用旧前台结果
        query_fail = 0;
        cb_keyboard_connected(0);
        assert(check_context() == 0); // 断线即停用
        puts("Foreground switching tests passed");
    }
}
