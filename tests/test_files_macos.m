#import <AppKit/AppKit.h>
#include <assert.h>
#include <unistd.h>
#include "../src/platform_macos.m"
int main(void) { @autoreleasepool {
    pasteboard=[NSPasteboard pasteboardWithUniqueName];last_change_count=pasteboard.changeCount;
    char *paths[]={"/tmp/中文🙂.txt","/tmp/second.txt"};
    cb_content files={0},out={0};files.files=paths;files.file_count=2;
    assert(cb_platform_set_content(&files)==0);
    assert(cb_platform_poll_content(&out)==0); // 自写抑制。
    last_change_count--;
    assert(cb_platform_poll_content(&out)==1 && out.file_count==2 && !out.text && !out.png);
    assert(!strcmp(out.files[0],paths[0]) && !strcmp(out.files[1],paths[1]));
    cb_content_free(&out);
    [pasteboard clearContents];[pasteboard setString:@"new text" forType:NSPasteboardTypeString];
    assert(cb_platform_poll_content(&out)==1 && out.text && !out.files);
    cb_content_free(&out);
    /* 独立剪贴板引用真实缓存子文件，不能被过期清理。 */
    char pattern[]="/tmp/cb-cache-clipboard-XXXXXX";char *root=mkdtemp(pattern);assert(root);
    char path[256];snprintf(path,sizeof(path),"%s/file.txt",root);
    FILE *file=fopen(path,"wb");assert(file);fclose(file);
    char *actual[]={path};files.files=actual;files.file_count=1;
    assert(cb_platform_set_content(&files)==0);
    NSInteger baseline=last_change_count;
    assert(cache_in_use(root)==1 && last_change_count==baseline);
    assert(cb_platform_set("text",4)==0);
    baseline=last_change_count;
    assert(cache_in_use(root)==0 && last_change_count==baseline);
    assert(unlink(path)==0 && rmdir(root)==0);
    [pasteboard releaseGlobally];cb_platform_close();
    puts("Mac file URLs, self-write suppression and text replacement tests passed");
} }
