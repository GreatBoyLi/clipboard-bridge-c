/* 私有偏好域测试保存与配对状态，不启动界面或连接真实主机。 */
#include <assert.h>
#define main cb_menu_main
#include "../src/desktop_macos.m"
#undef main
@interface TestMenu : BridgeMenu
@property NSString *testKey;
@property unsigned connects;
@property unsigned alerts;
@end
@implementation TestMenu
- (NSString *)keyPath {return self.testKey;}
- (void)rebuildMenu {}
- (void)connect:(id)sender {(void)sender;self.connects++;}
- (void)alert:(NSString *)title message:(NSString *)message {(void)title;(void)message;self.alerts++;}
@end
int main(void) { @autoreleasepool {
    NSString *suite=[@"net.greatboyli.clipboardbridge.test." stringByAppendingString:NSUUID.UUID.UUIDString];
    TestMenu *menu=[TestMenu new];menu.settings=[[NSUserDefaults alloc] initWithSuiteName:suite];
    char base[]="/tmp/cb-settings-test-XXXXXX";assert(mkdtemp(base));
    menu.testKey=[[NSString stringWithUTF8String:base] stringByAppendingPathComponent:@"pairing.key"];
    [menu saveHost:@"100.87.202.121" port:@"47321"];
    assert(menu.connects==0 && menu.alerts==0);
    assert([menu.status isEqualToString:@"地址已保存，待生成配对密钥"]);
    NSUserDefaults *saved=[[NSUserDefaults alloc] initWithSuiteName:suite];
    assert([[saved stringForKey:@"host"] isEqualToString:@"100.87.202.121"]);
    assert([[saved stringForKey:@"port"] isEqualToString:@"47321"]);
    assert([@"fixture" writeToFile:menu.testKey atomically:NO encoding:NSUTF8StringEncoding error:NULL]);
    [menu saveHost:@"100.87.202.121" port:@"47321"];
    assert(menu.connects==1 && menu.alerts==0);
    assert(unlink(menu.testKey.fileSystemRepresentation)==0 && rmdir(base)==0);
    [saved removePersistentDomainForName:suite];assert([saved synchronize]);
    puts("Saved address and unpaired menu state tests passed");
}return 0;}
