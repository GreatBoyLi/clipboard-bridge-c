#include "protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/*
 * 公共协议的回归测试。
 *
 * 不访问网络，也不读取或修改系统剪贴板。
 * assert 条件不成立时，程序会立即退出并报告失败位置。
 *
 * 编译测试时不要定义 NDEBUG，否则 assert 检查会被关闭。
 */
int main(void)
{
    cb_header original = {CB_TEXT, 7, 6};
    cb_header decoded;
    unsigned char wire[CB_HEADER_SIZE];

    /* 1. 正常文本消息头应能完成编码和解码。 */
    cb_encode_header(wire, &original);

    assert(cb_decode_header(wire, &decoded) == 0);
    assert(decoded.type == CB_TEXT);
    assert(decoded.sequence == 7);
    assert(decoded.length == 6);

    /*
     * 2. 检查固定标识和大端序布局。
     * 序号 7 的最低有效字节位于 11；
     * 长度 6 的最低有效字节位于 15。
     */
    assert(memcmp(wire, "CB01", 4) == 0);
    assert(wire[11] == 7);
    assert(wire[15] == 6);

    /* 3. 错误协议标识必须被拒绝。 */
    wire[0] = 'X';
    assert(cb_decode_header(wire, &decoded) < 0);

    /* 4. 当前版本不允许使用保留字段。 */
    cb_encode_header(wire, &original);
    wire[5] = 1;
    assert(cb_decode_header(wire, &decoded) < 0);

    /* 5. 超过 1 MiB 的文本长度必须被拒绝。 */
    original.length = CB_MAX_TEXT + 1;
    cb_encode_header(wire, &original);
    assert(cb_decode_header(wire, &decoded) < 0);

    /* 6. 认证消息需要序号为 0、长度为 64。 */
    original.type = CB_AUTH;
    original.sequence = 0;
    original.length = CB_KEY_SIZE;
    cb_encode_header(wire, &original);
    assert(cb_decode_header(wire, &decoded) == 0);

    /* 7. 长度不足的认证消息必须被拒绝。 */
    original.length = CB_KEY_SIZE - 1;
    cb_encode_header(wire, &original);
    assert(cb_decode_header(wire, &decoded) < 0);

    /* 8. ACK 消息不能包含消息体。 */
    original.type = CB_ACK;
    original.sequence = 1;
    original.length = 1;
    cb_encode_header(wire, &original);
    assert(cb_decode_header(wire, &decoded) < 0);

    /* 9. 未定义的消息类型必须被拒绝。 */
    original.type = 99;
    original.length = 0;
    cb_encode_header(wire, &original);
    assert(cb_decode_header(wire, &decoded) < 0);

    /* 10. 空文本是合法的文本消息。 */
    original.type = CB_TEXT;
    original.sequence = 1;
    original.length = 0;
    cb_encode_header(wire, &original);
    assert(cb_decode_header(wire, &decoded) == 0);

    /* 11. 文本消息不能使用序号 0。 */
    original.sequence = 0;
    cb_encode_header(wire, &original);
    assert(cb_decode_header(wire, &decoded) < 0);

    /*
     * 12. 检查中文、换行和 Emoji。
     *
     * 显式使用 UTF-8 字节，避免不同编译器的源文件编码
     * 设置影响测试数据。
     */
    const unsigned char unicode[] = {
        0xe4, 0xb8, 0xad,       /* 中 */
        0xe6, 0x96, 0x87,       /* 文 */
        0x0a,                   /* 换行 */
        0xf0, 0x9f, 0x99, 0x82  /* 🙂 */
    };

    assert(cb_valid_utf8(unicode, sizeof(unicode)));

    /* 13. 空文本和包含 CRLF 的 ASCII 文本均合法。 */
    assert(cb_valid_utf8((const unsigned char *)"", 0));
    assert(cb_valid_utf8((const unsigned char *)"hello\r\n", 7));

    /* 14. 拒绝过长编码。 */
    assert(!cb_valid_utf8((const unsigned char *)"\xc0\x80", 2));

    /* 15. 拒绝 UTF-16 代理项的编码。 */
    assert(!cb_valid_utf8((const unsigned char *)"\xed\xa0\x80", 3));

    /* 16. 拒绝超过 Unicode 最大码点的编码。 */
    assert(!cb_valid_utf8((const unsigned char *)"\xf4\x90\x80\x80", 4));

    /* 17. 三字节字符只收到两个字节时，必须拒绝。 */
    assert(!cb_valid_utf8((const unsigned char *)"\xe4\xb8", 2));

    /* 18. 文本中间包含 NUL 时，必须拒绝。 */
    assert(!cb_valid_utf8((const unsigned char *)"a\0b", 3));

    /* 19. 固定长度密钥比较：相同返回 1，不同返回 0。 */
    assert(cb_key_equal("abcd", "abcd", 4));
    assert(!cb_key_equal("abcd", "abce", 4));

    original=(cb_header){CB_SCROLL_CAPS,0,0};
    cb_encode_header(wire,&original);assert(cb_decode_header(wire,&decoded)==0);
    original=(cb_header){CB_SCROLL_LINES,0,1};
    cb_encode_header(wire,&original);assert(cb_decode_header(wire,&decoded)==0);
    original.length=2;
    cb_encode_header(wire,&original);assert(cb_decode_header(wire,&decoded)<0);
    original.length=1;original.sequence=1;
    cb_encode_header(wire,&original);assert(cb_decode_header(wire,&decoded)<0);
    /* 新文件消息边界：拒绝空块、过大清单、零序号和损坏校验长度。 */
    original=(cb_header){CB_FILES_BEGIN,1,4};
    cb_encode_header(wire,&original);assert(cb_decode_header(wire,&decoded)==0);
    original.length=32769;cb_encode_header(wire,&original);assert(cb_decode_header(wire,&decoded)<0);
    original=(cb_header){CB_FILES_CHUNK,1,16384};
    cb_encode_header(wire,&original);assert(cb_decode_header(wire,&decoded)==0);
    original.length=0;cb_encode_header(wire,&original);assert(cb_decode_header(wire,&decoded)<0);
    original=(cb_header){CB_FILES_CREDIT,0,0};
    cb_encode_header(wire,&original);assert(cb_decode_header(wire,&decoded)<0);
    original=(cb_header){CB_FILES_END,1,256};
    cb_encode_header(wire,&original);assert(cb_decode_header(wire,&decoded)==0);
    original.length=5;cb_encode_header(wire,&original);assert(cb_decode_header(wire,&decoded)<0);
    puts("Protocol tests passed");
    return 0;
}
