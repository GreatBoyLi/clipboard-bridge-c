/* 必须放入独立 .app 测试：普通命令行测试没有应用标识，无法复现此问题。 */
#include <assert.h>
#define main cb_menu_main
#include "../src/desktop_macos.m"
#undef main
int main(void) { @autoreleasepool {
    NSString *domain=NSBundle.mainBundle.bundleIdentifier;
    assert([domain hasPrefix:@"net.greatboyli.clipboardbridge.test.bundle."]);
    NSUserDefaults *settings=menu_preferences();
    [settings setObject:@"100.87.202.121" forKey:@"host"];
    [settings setObject:@"47321" forKey:@"port"];
    assert([settings synchronize]);
    NSDictionary *saved=[settings persistentDomainForName:domain];
    assert([saved[@"host"] isEqualToString:@"100.87.202.121"]);
    assert([saved[@"port"] isEqualToString:@"47321"]);
    [settings removePersistentDomainForName:domain];assert([settings synchronize]);
    puts("Bundled Mac application settings save passed");
}return 0;}
