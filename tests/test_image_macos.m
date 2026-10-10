#import <AppKit/AppKit.h>
#include <assert.h>
#include "image_fixture.h"
static int fail_png_encoding;
@interface CBTestBitmap : NSBitmapImageRep
@end
@implementation CBTestBitmap
- (NSData *)representationUsingType:(NSBitmapImageFileType)type properties:(NSDictionary *)properties {
    if(fail_png_encoding && type==NSBitmapImageFileTypePNG) return nil;
    return [super representationUsingType:type properties:properties];
}
@end
#define NSBitmapImageRep CBTestBitmap
#include "../src/platform_macos.m"
int main(void) { @autoreleasepool {
    /* 独立测试剪贴板，绝不清空用户的通用剪贴板。 */
    pasteboard=[NSPasteboard pasteboardWithUniqueName];
    last_change_count=pasteboard.changeCount;
    cb_content input={"图片🙂",10,(unsigned char *)cb_test_png,sizeof(cb_test_png),NULL,0}, output={0};
    assert(cb_platform_set_content(&input)==0);
    assert([pasteboard stringForType:NSPasteboardTypeString]!=nil);
    assert([pasteboard dataForType:NSPasteboardTypePNG]!=nil);
    assert([pasteboard dataForType:NSPasteboardTypeTIFF]!=nil);
    assert(cb_platform_poll_content(&output)==0); // 自写不能回传。
    last_change_count--;
    assert(cb_platform_poll_content(&output)==1);
    assert(output.text_length==10 && output.png_length==sizeof(cb_test_png));
    cb_content_free(&output);
    input.png_length=20;assert(cb_platform_set_content(&input)==-1);
    assert([pasteboard dataForType:NSPasteboardTypePNG].length==sizeof(cb_test_png));
    NSData *tiff=[pasteboard dataForType:NSPasteboardTypeTIFF];
    NSPasteboardItem *item=[[NSPasteboardItem alloc] init];
    [item setData:tiff forType:NSPasteboardTypeTIFF];
    [item setString:@"must not silently drop image" forType:NSPasteboardTypeString];
    [pasteboard clearContents];[pasteboard writeObjects:@[item]];
    fail_png_encoding=1;
    assert(cb_platform_poll_content(&output)==-1 && !output.text && !output.png);
    fail_png_encoding=0;
    [pasteboard clearContents];
    [pasteboard setString:@"text only" forType:NSPasteboardTypeString];
    assert(cb_platform_poll_content(&output)==1 && output.text && !output.png);
    cb_content_free(&output);
    [pasteboard releaseGlobally]; cb_platform_close();
    puts("Mac image clipboard snapshot tests passed");
} }
