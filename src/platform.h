#ifndef CB_PLATFORM_H
#define CB_PLATFORM_H

#include <stddef.h>
#include "content.h"

/*
 * 初始化系统剪贴板接口。
 *
 * 返回值：
 *   0：成功。
 *  -1：失败。
 *
 * Mac 端要求所有剪贴板接口都在主线程调用。
 */
int cb_platform_init(void);

/* 释放平台相关资源。 */
void cb_platform_close(void);

/*
 * 将当前剪贴板版本记录为基线，不读取或发送已有内容。
 *
 * 后续每次网络连接建立时调用，避免重连后把旧内容
 * 自动覆盖到另一台设备。
 */
void cb_platform_reset(void);

/*
 * 检查剪贴板是否出现新的文本。
 *
 * 返回值：
 *   1：获得新的文本。
 *   0：没有变化，或新内容不是文本。
 *  -1：读取失败，或内容不符合文本限制。
 *
 * 返回 1 时：
 *   *text 指向 malloc 分配的 UTF-8 缓冲区。
 *   *length 是文本字节数，不包括末尾的 '\0'。
 *   调用方必须使用 free(*text) 释放内存。
 *
 * 空文本也属于合法文本，长度为 0。
 * text 和 length 两个输出参数均不能为空。
 */
int cb_platform_poll(char **text, size_t *length);

/*
 * 将收到的 UTF-8 文本写入本机剪贴板。
 *
 * text 至少包含 length 字节，不要求以 '\0' 结尾。
 * 不允许嵌入 NUL 字节，最大长度为 CB_MAX_TEXT。
 *
 * 返回值：
 *   0：成功。
 *  -1：失败。
 *
 * 成功后更新本地版本基线，避免再次把同一内容发送回对端。
 * 本函数不取得输入缓冲区的所有权。
 */
int cb_platform_set(const char *text, size_t length);

/*
 * 使用系统随机源生成随机字节，供后续生成配对密钥。
 *
 * out 由调用方提供，至少包含 length 字节。
 * 成功返回 0，失败返回 -1。
 */
int cb_platform_random(unsigned char *out, size_t length);

#endif /* CB_PLATFORM_H */
