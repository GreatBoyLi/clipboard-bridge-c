#import <AppKit/AppKit.h>

#include "platform.h"
#include "protocol.h"

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

int cb_platform_poll(char **text, size_t *length)
{
    if (text == NULL || length == NULL) {
        return -1;
    }

    /* 所有返回路径都提供明确的默认输出。 */
    *text = NULL;
    *length = 0;

    @autoreleasepool {
        if (!ready_on_main_thread()) {
            return -1;
        }

        NSInteger current = [pasteboard changeCount];

        /*
         * 内容没有变化时，不读取字符串，
         * 避免重复转换和分配内存。
         */
        if (current == last_change_count) {
            return 0;
        }

        NSString *value =
            [pasteboard stringForType:NSPasteboardTypeString];

        /*
         * 读取期间可能有其他应用再次修改剪贴板。
         * 不接受不稳定的快照，也不更新版本基线，
         * 让下一次轮询重新读取。
         */
        if ([pasteboard changeCount] != current) {
            return -1;
        }

        if (value == nil) {
            /*
             * 图片、文件等内容暂不处理。
             * 记录该版本，避免每次轮询重复读取。
             */
            last_change_count = current;
            return 0;
        }

        /* 禁止有损转换，确保中文和 Emoji 完整保留。 */
        NSData *data = [value
            dataUsingEncoding:NSUTF8StringEncoding
            allowLossyConversion:NO];

        if (data == nil) {
            last_change_count = current;
            return -1;
        }

        NSUInteger byte_count = [data length];

        /*
         * 不支持的内容只处理一次，
         * 避免每次轮询都重复报告同一个错误。
         */
        if (byte_count > CB_MAX_TEXT ||
            !cb_valid_utf8(
                (const unsigned char *)[data bytes],
                (size_t)byte_count)) {
            last_change_count = current;
            return -1;
        }

        /*
         * 多分配 1 字节保存字符串结束符。
         * 协议中的长度不包含这个结束符。
         */
        char *buffer = malloc((size_t)byte_count + 1);

        if (buffer == NULL) {
            /* 内存分配失败时不更新版本，允许稍后重试。 */
            return -1;
        }

        if (byte_count != 0) {
            memcpy(buffer, [data bytes], (size_t)byte_count);
        }

        buffer[byte_count] = '\0';

        *text = buffer;
        *length = (size_t)byte_count;
        last_change_count = current;

        return 1;
    }
}

int cb_platform_set(const char *text, size_t length)
{
    if (text == NULL ||
        length > CB_MAX_TEXT ||
        !cb_valid_utf8((const unsigned char *)text, length)) {
        return -1;
    }

    @autoreleasepool {
        if (!ready_on_main_thread()) {
            return -1;
        }

        /*
         * 先完成转换，再清理剪贴板。
         * 非法输入不会导致原来的剪贴板内容被清除。
         */
        NSString *value = [[NSString alloc]
            initWithBytes:text
            length:length
            encoding:NSUTF8StringEncoding];

        if (value == nil) {
            return -1;
        }

        /*
         * 系统剪贴板可同时包含多个格式。
         * 接收新的纯文本时，清除原来的图片、文件等格式。
         */
        [pasteboard clearContents];

        if (![pasteboard
                setString:value
                forType:NSPasteboardTypeString]) {
            return -1;
        }

        /*
         * 写入后记录新版本，下一次轮询不会再次读取它。
         * 这是避免“Mac → Windows → Mac → Windows”循环的关键。
         */
        last_change_count = [pasteboard changeCount];

        return 0;
    }
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
