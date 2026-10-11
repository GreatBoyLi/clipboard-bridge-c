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
    NSString *generated=[directory stringByAppendingPathComponent:@"generated.key"];
    NSData *key=ensure_pairing(generated);assert(key.length==65);
    assert([ensure_pairing(generated) isEqualToData:key]); // 重复导出绝不换密钥。
    NSString *other=[directory stringByAppendingPathComponent:@"other.key"];
    NSData *otherKey=ensure_pairing(other);assert(otherKey.length==65 && ![otherKey isEqualToData:key]);
    NSString *text=[[NSString alloc] initWithData:key encoding:NSUTF8StringEncoding];
    NSCharacterSet *invalid=[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet];
    assert([[text substringToIndex:64] rangeOfCharacterFromSet:invalid].location==NSNotFound && [text hasSuffix:@"\n"]);
    assert(stat(generated.fileSystemRepresentation,&info)==0 && (info.st_mode&0777)==0600);
    assert(ensure_pairing(path)==nil); // 已有损坏文件不能被悄悄替换。
    assert([[NSData dataWithContentsOfFile:path] isEqualToData:second]);
    NSString *alias=[directory stringByAppendingPathComponent:@"alias.key"];
    assert(symlink(generated.fileSystemRepresentation,alias.fileSystemRepresentation)==0);
    assert(ensure_pairing(alias)==nil);
    NSString *fifo=[directory stringByAppendingPathComponent:@"fifo.key"];
    assert(mkfifo(fifo.fileSystemRepresentation,0600)==0 && ensure_pairing(fifo)==nil);
    assert(unlink(fifo.fileSystemRepresentation)==0);
    assert(unlink(alias.fileSystemRepresentation)==0 && unlink(generated.fileSystemRepresentation)==0 && unlink(other.fileSystemRepresentation)==0);
    assert(unlink(path.fileSystemRepresentation)==0 && unlink(stale.fileSystemRepresentation)==0 && rmdir(base)==0);
    puts("Mac private pairing replacement and recovery tests passed");
}return 0;}
