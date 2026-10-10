#ifndef CB_CONTENT_H
#define CB_CONTENT_H
#include <stddef.h>
#include <stdint.h>
#define CB_MAX_PNG (20u * 1024u * 1024u)
#define CB_MAX_PIXELS (32u * 1024u * 1024u)
#define CB_CONTENT_META 12u
#define CB_CONTENT_CHUNK_SIZE 16384u
/* 一次复制的快照：text 非 NULL 表示文本存在，包括空字符串。 */
typedef struct {
    char *text;
    size_t text_length;
    unsigned char *png;
    size_t png_length;
} cb_content;
/* 释放由 poll/unpack 分配的快照；调用后全部字段清零。 */
void cb_content_free(cb_content *content);
int cb_image_dimensions(uint32_t width, uint32_t height);
int cb_png_valid(const unsigned char *png, size_t length);
int cb_content_pack(const cb_content *content, unsigned char **data, size_t *length);
int cb_content_unpack(const unsigned char *data, size_t length, cb_content *content);
/* 平台接口返回同一版本的图片及文本，远端写入只改变一次剪贴板。 */
/* out 必须是空快照；1 新内容，0 未变/不支持，-1 读取或限制错误。
 * 成功的 text/png 使用 malloc 分配，调用方负责 cb_content_free。 */
int cb_platform_poll_content(cb_content *content);
/* 0 成功，-1 失败；不接管输入内存，准备完成后才清空系统剪贴板。 */
int cb_platform_set_content(const cb_content *content);
#endif
