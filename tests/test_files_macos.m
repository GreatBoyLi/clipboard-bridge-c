#import <AppKit/AppKit.h>
#include <assert.h>
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
    cb_content_free(&out);[pasteboard releaseGlobally];cb_platform_close();
    puts("Mac file URLs, self-write suppression and text replacement tests passed");
} }
