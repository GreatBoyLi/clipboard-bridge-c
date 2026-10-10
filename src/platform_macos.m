#import <AppKit/AppKit.h>

#include "platform.h"
#include "protocol.h"
#include "content.h"
#include "files.h"
#import <ImageIO/ImageIO.h>

#include <stdlib.h>
#include <string.h>

/*
 * 使用 ARC 管理 Objective-C 对象的生命周期。
 * 编译时必须添加 -fobjc-arc。
 *
 * UTF-8 输出缓冲区仍使用 malloc/free，
 * 不由 ARC 管理。
 */
static NSPasteboard *pasteboard = nil;

/* 最近一次已处理的剪贴板版本。 */
static NSInteger last_change_count = 0;

/*
 * 检查调用线程。
 * 剪贴板操作统一在主线程执行，避免多个线程交叉读取和写入。
 */
static int ready_on_main_thread(void)
{
    return pasteboard != nil && [NSThread isMainThread];
}

int cb_platform_init(void)
{
    @autoreleasepool {
        if (![NSThread isMainThread]) {
            return -1;
        }

        /* generalPasteboard 是系统通用剪贴板。 */
        pasteboard = [NSPasteboard generalPasteboard];

        if (pasteboard == nil) {
            return -1;
        }

        /*
         * 只记录版本，不同步启动前已经存在的内容。
         * 用户下一次复制文本时才会产生新的待同步内容。
         */
        last_change_count = [pasteboard changeCount];
        return 0;
    }
}

void cb_platform_close(void)
{
    @autoreleasepool {
        pasteboard = nil;
        last_change_count = 0;
    }
}

void cb_platform_reset(void)
{
    @autoreleasepool {
        if (ready_on_main_thread()) {
            last_change_count = [pasteboard changeCount];
        }
    }
}

/* 元数据先检查尺寸，禁止先完整解码再判断是否过大。 */
static CGImageRef decode_image(NSData *data)
{
    if (!data || data.length > CB_MAX_PIXELS * 4u + 1024u * 1024u) return NULL;
    CGImageSourceRef source=CGImageSourceCreateWithData((__bridge CFDataRef)data,NULL);
    if (!source) return NULL;
    CGImageRef image=NULL;
    CFDictionaryRef properties=CGImageSourceCopyPropertiesAtIndex(source,0,NULL);
    if(properties) {
        NSDictionary *meta=(__bridge NSDictionary *)properties;
        uint64_t w=[meta[(__bridge NSString *)kCGImagePropertyPixelWidth] unsignedLongLongValue];
        uint64_t h=[meta[(__bridge NSString *)kCGImagePropertyPixelHeight] unsignedLongLongValue];
        if(w<=UINT32_MAX && h<=UINT32_MAX && cb_image_dimensions((uint32_t)w,(uint32_t)h))
            image=CGImageSourceCreateImageAtIndex(source,0,NULL);
        CFRelease(properties);
    }
    CFRelease(source);return image;
}
int cb_platform_poll_content(cb_content *out)
{
    if(!out) return -1;
    memset(out,0,sizeof(*out));
    @autoreleasepool {
        if(!ready_on_main_thread()) return -1;
        NSInteger version=pasteboard.changeCount;
        if(version==last_change_count) return 0;
        /* 文件复制可能同时带路径文本/图标，优先读取真正的文件 URL。 */
        NSArray<NSURL *> *urls=[pasteboard readObjectsForClasses:@[[NSURL class]]
            options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
        if(urls.count) {
            int bad=urls.count>CB_FILE_COUNT;
            if(!bad) {
                out->files=calloc(urls.count,sizeof(char *));
                if(!out->files) return -1;
                out->file_count=(unsigned)urls.count;
                for(NSUInteger i=0;i<urls.count;i++) {
                    const char *path=urls[i].path.UTF8String;
                    if(!path) {bad=1;break;}
                    out->files[i]=strdup(path);if(!out->files[i]) {bad=1;break;}
                }
            }
            if(pasteboard.changeCount!=version) bad=1;
            last_change_count=version;
            if(bad) {cb_content_free(out);return -1;}
            return 1;
        }
        NSString *text=[pasteboard stringForType:NSPasteboardTypeString];
        NSData *png=[pasteboard dataForType:NSPasteboardTypePNG];
        NSData *tiff=png?nil:[pasteboard dataForType:NSPasteboardTypeTIFF];
        int bad=0;
        if(tiff) {
            CGImageRef image=decode_image(tiff);
            if(image) {
                NSBitmapImageRep *rep=[[NSBitmapImageRep alloc] initWithCGImage:image];
                png=[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                CGImageRelease(image);
                if(!png) bad=1;
            } else bad=1;
        }
        if(png) {
            if(!cb_png_valid(png.bytes,png.length)) bad=1;
            else {
                CGImageRef image=decode_image(png);
                if(!image) bad=1;else CGImageRelease(image);
            }
        }
        NSData *utf8=text?[text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO]:nil;
        if(text && (!utf8 || utf8.length>CB_MAX_TEXT || !cb_valid_utf8(utf8.bytes,utf8.length))) bad=1;
        /* 图片与文字必须来自同一稳定版本，不能拼接两次复制的内容。 */
        if(pasteboard.changeCount!=version) return -1;
        last_change_count=version;
        if(bad) return -1; // 超限图片不退化为仅文本，避免悄悄丢失格式。
        if(!text && !png) return 0;
        if(text) {
            out->text=malloc(utf8.length+1);if(!out->text) return -1;
            memcpy(out->text,utf8.bytes,utf8.length);out->text[utf8.length]=0;
            out->text_length=utf8.length;
        }
        if(png) {
            out->png=malloc(png.length);
            if(!out->png) {cb_content_free(out);return -1;}
            memcpy(out->png,png.bytes,png.length);out->png_length=png.length;
        }
        return 1;
    }
}
int cb_platform_set_content(const cb_content *c)
{
    if(c && c->files) {
        if(!c->file_count || c->file_count>CB_FILE_COUNT || c->text || c->png) return -1;
        @autoreleasepool {
            if(!ready_on_main_thread()) return -1;
            NSMutableArray<NSURL *> *urls=[NSMutableArray array];
            for(unsigned i=0;i<c->file_count;i++) {
                if(!c->files[i]) return -1;
                NSString *path=[[NSString alloc] initWithUTF8String:c->files[i]];
                if(!path || !path.isAbsolutePath) return -1;
                [urls addObject:[NSURL fileURLWithPath:path]];
            }
            [pasteboard clearContents];BOOL ok=[pasteboard writeObjects:urls];
            last_change_count=pasteboard.changeCount;return ok?0:-1;
        }
    }
    if(!c || (!c->text && !c->png) || c->text_length>CB_MAX_TEXT ||
       (!c->text && c->text_length) || (!c->png && c->png_length) ||
       !cb_valid_utf8((const unsigned char *)c->text,c->text_length)) return -1;
    @autoreleasepool {
        if(!ready_on_main_thread()) return -1;
        NSPasteboardItem *item=[[NSPasteboardItem alloc] init];
        if(c->text) {
            NSString *text=[[NSString alloc] initWithBytes:c->text length:c->text_length encoding:NSUTF8StringEncoding];
            if(!text || ![item setString:text forType:NSPasteboardTypeString]) return -1;
        }
        if(c->png) {
            if(!cb_png_valid(c->png,c->png_length)) return -1;
            NSData *png=[NSData dataWithBytes:c->png length:c->png_length];
            CGImageRef image=decode_image(png);if(!image) return -1;
            NSBitmapImageRep *rep=[[NSBitmapImageRep alloc] initWithCGImage:image];
            CGImageRelease(image);
            NSData *tiff=[rep representationUsingType:NSBitmapImageFileTypeTIFF properties:@{}];
            if(!tiff || ![item setData:png forType:NSPasteboardTypePNG] ||
               ![item setData:tiff forType:NSPasteboardTypeTIFF]) return -1;
        }
        /* 转换全部成功后才清空；一个 item 同时保留文本与两种图片格式。 */
        [pasteboard clearContents];
        BOOL ok=[pasteboard writeObjects:@[item]];
        last_change_count=pasteboard.changeCount;
        return ok?0:-1;
    }
}
/* 旧文本接口保留，便于现有手动测试和仅文本调用方使用。 */
int cb_platform_poll(char **text,size_t *length)
{
    if(!text || !length) return -1;
    *text=NULL;*length=0;cb_content c={0};int result=cb_platform_poll_content(&c);
    if(result==1 && c.text) {*text=c.text;*length=c.text_length;c.text=NULL;}
    else if(result==1) result=0;
    cb_content_free(&c);return result;
}
int cb_platform_set(const char *text,size_t length)
{
    cb_content c={(char *)text,length,NULL,0,NULL,0};return cb_platform_set_content(&c);
}

int cb_platform_random(unsigned char *out, size_t length)
{
    if (out == NULL && length != 0) {
        return -1;
    }

    if (length != 0) {
        /* 使用系统提供的随机源，不使用时间戳或 rand() 生成密钥。 */
        arc4random_buf(out, length);
    }

    return 0;
}

/* 不修改剪贴板基线；读取失败或读取期间变化时保守保留缓存。 */
static int cache_in_use(const char *directory) {
    @autoreleasepool {
        if(!ready_on_main_thread()) return 1;
        NSInteger version=pasteboard.changeCount;
        NSArray<NSURL *> *urls=[pasteboard readObjectsForClasses:@[[NSURL class]]
            options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
        if(!urls && [pasteboard availableTypeFromArray:@[NSPasteboardTypeFileURL,@"NSFilenamesPboardType"]]) return 1;
        for(NSURL *url in urls) {
            const char *path=url.path.UTF8String;
            if(!path || cb_file_cache_contains(directory,path)) return 1;
        }
        return pasteboard.changeCount!=version;
    }
}
void cb_platform_cleanup_cache(void) {
    unsigned removed=cb_file_cache_clean(cache_in_use);
    if(removed) printf("Removed %u expired file cache directories.\n",removed);
}
