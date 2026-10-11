/* 仅测试私有目录内的配对原子保存，不启动界面或读取用户剪贴板。 */
#include <assert.h>
#define main cb_menu_main
#include "../src/desktop_macos.m"
#undef main
int main(void) { @autoreleasepool {
    char base[]="/tmp/cb-menu-key-XXXXXX";assert(mkdtemp(base));
    NSString *directory=[NSString stringWithUTF8String:base];
    NSString *path=[directory stringByAppendingPathComponent:@"pairing.key"];
    NSString *stale=[path stringByAppendingString:@".new"];
    assert([@"stale" writeToFile:stale atomically:NO encoding:NSUTF8StringEncoding error:NULL]);
    NSData *first=[@"first" dataUsingEncoding:NSUTF8StringEncoding];
    NSData *second=[@"second" dataUsingEncoding:NSUTF8StringEncoding];
    assert(store_pairing(path,first));assert(store_pairing(path,second));
    assert([[NSData dataWithContentsOfFile:path] isEqualToData:second]);
    struct stat info;assert(stat(path.fileSystemRepresentation,&info)==0 && (info.st_mode&0777)==0600);
    assert(!store_pairing([directory stringByAppendingPathComponent:@"missing/key"],first));
    assert([[NSData dataWithContentsOfFile:path] isEqualToData:second]);
    assert([@"stale" isEqualToString:[NSString stringWithContentsOfFile:stale encoding:NSUTF8StringEncoding error:NULL]]);
    assert(unlink(path.fileSystemRepresentation)==0 && unlink(stale.fileSystemRepresentation)==0 && rmdir(base)==0);
    puts("Mac private pairing replacement and recovery tests passed");
}return 0;}
