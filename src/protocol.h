#ifndef CB_PROTOCOL_H
#define CB_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/*
 * Clipboard Bridge 公共通信协议。
 *
 * Mac 和 Windows 共用这份协议定义。
 * 本模块只负责消息头编解码及数据校验，不访问网络或系统剪贴板。
 *
 * 消息由两部分组成：
 *
 *   [固定 16 字节消息头] + [length 字节消息体]
 *
 * 消息头布局：
 *
 *   字节位置    长度    含义
 *   0～3        4       协议标识 "CB01"
 *   4           1       消息类型
 *   5～7        3       保留字段，当前必须为 0
 *   8～11       4       消息序号，无符号整数，大端序
 *   12～15      4       消息体长度，无符号整数，大端序
 *
 * “大端序”指高位字节在前，确保不同平台使用相同的传输格式。
 */

/* 每条消息的固定消息头长度。 */
#define CB_HEADER_SIZE 16

/*
 * 单条文本的最大长度：1 MiB。
 * 限制的是 UTF-8 编码后的字节数，不是字符数。
 */
#define CB_MAX_TEXT (1024u * 1024u)

/*
 * 配对认证消息的消息体长度。
 * 约定传输 64 个字符，例如 32 字节随机密钥的十六进制表示。
 *
 * 此处只定义长度；密钥生成和文件读取由后续模块负责。
 */
#define CB_KEY_SIZE 64

/*
 * 消息类型。
 *
 * AUTH：建立连接时进行配对认证。
 * TEXT：传输 UTF-8 文本，允许空文本。
 * ACK ：确认成功写入一次文本或图片快照。
 * PING/PONG：后续用于连接心跳检测。
 *
 * IMAGE_BEGIN/CHUNK/CREDIT：分块图片快照，不复用 TEXT 类型。
 * CREDIT 仅确认一块已接收，ACK 才确认整张图片成功写入。
 * 文件传输暂不实现。
 */
enum {
    CB_AUTH = 1,
    CB_TEXT = 2,
    CB_ACK  = 3,
    CB_PING = 4,
    CB_PONG = 5,
    CB_SCROLL_CAPS = 6, /* Windows 声明已启用行数控制，正文为空 */
    CB_SCROLL_LINES = 7, /* 正文一字节：0 恢复，1~5 临时行数 */
    CB_IMAGE_BEGIN = 8, /* 12 字节格式及长度；非零剪贴板序号 */
    CB_IMAGE_CHUNK = 9, /* 1~16384 字节；沿用 BEGIN 序号 */
    CB_IMAGE_CREDIT = 10, /* 非末块的接收确认，允许继续发送一块 */
    CB_CAPS_ENABLE = 11, /* seq=0，空正文，请求启动 Windows 按键线程 */
    CB_CAPS_READY = 12, /* seq=0，一字节：1 就绪，0 不可用 */
    CB_CAPS_ACTION = 13, /* 独立递增 seq，一字节短按=1/长按=2 */
    CB_CAPS_RESULT = 14 /* 原动作 seq，一字节：1 已提交，0 失败 */
};

/*
 * 程序内部使用的消息头。
 *
 * 不可直接把这个结构体作为网络数据发送：
 * 不同编译器可能添加对齐填充，整数的字节序也可能不同。
 * 必须通过下面的编解码函数转换。
 */
typedef struct {
    uint8_t type;       /* 消息类型。 */
    uint32_t sequence;  /* 消息序号；文本和 ACK 的序号不能为 0。 */
    uint32_t length;    /* 消息体长度，单位为字节。 */
} cb_header;

/*
 * 将内部消息头编码为固定 16 字节的网络消息头。
 *
 * out：
 *   调用方提供的输出缓冲区，至少包含 CB_HEADER_SIZE 字节。
 *
 * header：
 *   要编码的消息头，不能为空指针。
 *
 * 本函数不分配内存，也不校验输入字段是否满足协议约束。
 */
void cb_encode_header(
    unsigned char out[CB_HEADER_SIZE],
    const cb_header *header
);

/*
 * 解码并校验固定 16 字节的网络消息头。
 *
 * in：
 *   调用方已经完整读取的 CB_HEADER_SIZE 字节数据。
 *
 * header：
 *   调用方提供的输出结构体。
 *
 * 返回值：
 *    0：消息头符合当前协议。
 *   -1：标识、保留字段、类型、序号或长度不合法。
 *
 * 输入和输出指针均不能为空。
 * 失败时不要使用输出结构体中的内容。
 * 本函数只检查消息头，消息体仍需单独读取和校验。
 */
int cb_decode_header(
    const unsigned char in[CB_HEADER_SIZE],
    cb_header *header
);

/*
 * 检查数据是否为本工具支持的合法 UTF-8 文本。
 *
 * text：
 *   至少包含 length 字节的缓冲区，不要求以 '\0' 结尾。
 *
 * length：
 *   要检查的字节数；0 表示空文本。
 *
 * 返回值：
 *   1：合法。
 *   0：存在非法编码、截断字符或嵌入的 NUL 字节。
 *
 * 本工具不支持文本中间的 NUL 字节，避免原生剪贴板接口
 * 将其误认为字符串结束位置。
 *
 * 本函数不分配或释放内存。
 */
int cb_valid_utf8(const unsigned char *text, size_t length);

/*
 * 比较两个固定长度的配对密钥。
 *
 * a、b：
 *   两个缓冲区，各自至少包含 length 字节。
 *
 * length：
 *   比较的字节数，不依赖字符串结束符。
 *
 * 返回值：
 *   1：全部相同。
 *   0：存在差异。
 *
 * 实现会遍历全部指定字节，不在发现首个差异时提前返回。
 * 本函数只比较数据，不提供加密，也不验证密钥字符格式。
 */
int cb_key_equal(const char *a, const char *b, size_t length);

#endif /* CB_PROTOCOL_H */
