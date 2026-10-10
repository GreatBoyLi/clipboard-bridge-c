#include "protocol.h"

#include <string.h>

/*
 * 将一个 32 位无符号整数写入 4 字节缓冲区。
 *
 * 使用大端序：最高有效字节放在最前面。
 * p 必须指向至少 4 字节的可写区域。
 */
static void put32(unsigned char *p, uint32_t n)
{
    p[0] = (unsigned char)(n >> 24);
    p[1] = (unsigned char)(n >> 16);
    p[2] = (unsigned char)(n >> 8);
    p[3] = (unsigned char)n;
}

/*
 * 从 4 字节大端序数据中读取一个 32 位无符号整数。
 *
 * 先将每个字节转换为 uint32_t，再移位，
 * 避免普通整数提升和有符号移位带来的问题。
 */
static uint32_t get32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)
         | (uint32_t)p[3];
}

/*
 * 编码消息头。
 *
 * 逐字段写入，避免直接复制结构体时产生的
 * 内存对齐和平台字节序差异。
 */
void cb_encode_header(
    unsigned char out[CB_HEADER_SIZE],
    const cb_header *header
)
{
    /* 协议标识兼版本标识；不包含字符串结束符。 */
    memcpy(out, "CB01", 4);

    out[4] = header->type;

    /* 保留字段统一清零，留作未来协议扩展。 */
    out[5] = 0;
    out[6] = 0;
    out[7] = 0;

    put32(out + 8, header->sequence);
    put32(out + 12, header->length);
}

/*
 * 解码消息头，并在读取消息体之前检查长度等约束。
 *
 * 网络层应先调用本函数，确认长度合法后，
 * 再按 length 分配缓冲区和读取消息体。
 */
int cb_decode_header(
    const unsigned char in[CB_HEADER_SIZE],
    cb_header *header
)
{
    /* 拒绝其他协议或不支持的协议版本。 */
    if (memcmp(in, "CB01", 4) != 0) {
        return -1;
    }

    /* 当前版本不使用保留字段，非零表示不兼容。 */
    if (in[5] != 0 || in[6] != 0 || in[7] != 0) {
        return -1;
    }

    header->type = in[4];
    header->sequence = get32(in + 8);
    header->length = get32(in + 12);

    /* 不同消息类型使用不同的消息体和序号约束。 */
    switch (header->type) {
    case CB_AUTH:
        /*
         * 认证消息固定携带 64 字节密钥数据。
         * 序号为 0，区别于正常文本消息。
         */
        return header->length == CB_KEY_SIZE
            && header->sequence == 0 ? 0 : -1;

    case CB_TEXT:
        /*
         * 文本允许为空，但不能超过最大长度。
         * 非零序号用于后续 ACK 确认。
         */
        return header->length <= CB_MAX_TEXT
            && header->sequence != 0 ? 0 : -1;

    case CB_IMAGE_BEGIN:
        return header->length == 12 && header->sequence != 0 ? 0 : -1;
    case CB_IMAGE_CHUNK:
        return header->length > 0 && header->length <= 16384 && header->sequence != 0 ? 0 : -1;

    case CB_IMAGE_CREDIT:
    case CB_ACK:
        /*
         * ACK 不包含消息体。
         * 序号用于指明确认的是哪一条文本消息。
         */
        return header->length == 0
            && header->sequence != 0 ? 0 : -1;

    case CB_SCROLL_CAPS:
        return header->length == 0 && header->sequence == 0 ? 0 : -1;
    case CB_SCROLL_LINES:
        return header->length == 1 && header->sequence == 0 ? 0 : -1;

    case CB_PING:
    case CB_PONG:
        /* 心跳消息不携带消息体。 */
        return header->length == 0 ? 0 : -1;

    default:
        /* 拒绝未知类型，避免错误解释消息体。 */
        return -1;
    }
}

/*
 * 比较固定长度的密钥数据。
 *
 * 将所有字节差异合并到 difference。
 * 只有全部字节相同时，最终结果才为 0。
 *
 * 不使用 strcmp，因为密钥按固定长度比较，
 * 也不应在发现第一个差异时提前结束。
 */
int cb_key_equal(const char *a, const char *b, size_t length)
{
    unsigned char difference = 0;

    for (size_t i = 0; i < length; ++i) {
        difference |= (unsigned char)(a[i] ^ b[i]);
    }

    return difference == 0;
}

/*
 * UTF-8 校验。
 *
 * 合法字符可能占 1、2、3 或 4 字节。
 * 多字节字符由一个首字节和若干 10xxxxxx 形式的后续字节组成。
 *
 * 除检查字节形式外，还要排除：
 *   1. 不必要地使用更多字节的“过长编码”；
 *   2. UTF-16 代理项范围；
 *   3. 超过 Unicode 最大码点的值；
 *   4. 只传输了部分字节的字符；
 *   5. 本工具不支持的 NUL 字节。
 */
int cb_valid_utf8(const unsigned char *text, size_t length)
{
    size_t i = 0;

    while (i < length) {
        unsigned char c = text[i++];

        /* NUL 会使部分系统字符串接口提前结束读取。 */
        if (c == 0) {
            return 0;
        }

        /* ASCII 字符使用单字节编码，包括换行和制表符。 */
        if (c < 0x80) {
            continue;
        }

        unsigned remaining; /* 还需读取的后续字节数量。 */
        uint32_t value;     /* 正在拼接的 Unicode 码点。 */
        uint32_t minimum;   /* 当前编码长度允许的最小码点。 */

        if (c >= 0xc2 && c <= 0xdf) {
            /* 双字节字符：110xxxxx 10xxxxxx。 */
            remaining = 1;
            value = c & 0x1f;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            /* 三字节字符：1110xxxx 10xxxxxx 10xxxxxx。 */
            remaining = 2;
            value = c & 0x0f;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            /* 四字节字符：11110xxx 后接三个后续字节。 */
            remaining = 3;
            value = c & 0x07;
            minimum = 0x10000;
        } else {
            /* 非法首字节，或后续字节出现在字符起始位置。 */
            return 0;
        }

        /* 先检查剩余长度，避免读取到缓冲区之外。 */
        if (length - i < remaining) {
            return 0;
        }

        while (remaining != 0) {
            unsigned char next = text[i++];

            /* 后续字节的最高两位必须是二进制 10。 */
            if ((next & 0xc0) != 0x80) {
                return 0;
            }

            /* 每个后续字节贡献低 6 位有效数据。 */
            value = (value << 6) | (next & 0x3f);
            --remaining;
        }

        /*
         * 排除过长编码，以及超过 U+10FFFF 的码点。
         * 例如 ASCII 字符不能用双字节形式重新编码。
         */
        if (value < minimum || value > 0x10ffff) {
            return 0;
        }

        /* U+D800～U+DFFF 是 UTF-16 代理项，不是合法 UTF-8 码点。 */
        if (value >= 0xd800 && value <= 0xdfff) {
            return 0;
        }
    }

    /* 遍历完成，包括 length 为 0 的空文本。 */
    return 1;
}
