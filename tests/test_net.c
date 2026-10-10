#include "net.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

int main(void)
{
    assert(cb_net_init() == 0);

    /* 不允许程序监听所有网络接口。 */
    assert(cb_net_listen("0.0.0.0", "0") == CB_INVALID);
    assert(cb_net_listen("::", "0") == CB_INVALID);

    /*
     * 端口 0 表示让系统分配一个空闲端口，
     * 避免测试与其他程序产生端口冲突。
     */
    cb_socket listener = cb_net_listen("127.0.0.1", "0");
    assert(listener != CB_INVALID);

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));

#ifdef _WIN32
    int address_length = sizeof(address);
#else
    socklen_t address_length = sizeof(address);
#endif

    /* 获取系统实际分配的端口。 */
    assert(getsockname(
        listener,
        (struct sockaddr *)&address,
        &address_length
    ) == 0);

    char port[16];
    snprintf(port, sizeof(port), "%u", (unsigned)ntohs(address.sin_port));

    cb_socket client = cb_net_connect("127.0.0.1", port, 1000);
    assert(client != CB_INVALID);

    assert(cb_net_wait(listener, 0, 1000) == 1);

    cb_socket server = cb_net_accept(listener);
    assert(server != CB_INVALID);

    /*
     * 分两次发送，接收端必须按要求收齐全部 6 字节，
     * 不依赖发送次数或 TCP 数据包边界。
     */
    assert(cb_net_write(client, "abc", 3, 1000) == 0);
    assert(cb_net_write(client, "def", 3, 1000) == 0);

    char received[6];
    assert(cb_net_read(server, received, sizeof(received), 1000) == 0);
    assert(memcmp(received, "abcdef", sizeof(received)) == 0);

    /* 检查反方向的数据传输。 */
    assert(cb_net_write(server, "reply", 5, 1000) == 0);

    char reply[5];
    assert(cb_net_read(client, reply, sizeof(reply), 1000) == 0);
    assert(memcmp(reply, "reply", sizeof(reply)) == 0);

    /* 空数据不需要访问缓冲区，也不需要实际收发。 */
    assert(cb_net_write(client, NULL, 0, 1000) == 0);
    assert(cb_net_read(server, NULL, 0, 1000) == 0);

    /* 当前没有数据，等待应超时。 */
    assert(cb_net_wait(server, 0, 50) == 0);

    /* 完整读取在规定时间内没有收到数据时，应报告失败。 */
    char missing;
    assert(cb_net_read(server, &missing, 1, 50) == -1);

    /*
     * 根据接口约定，完整收发失败后关闭连接。
     * 对端读取应能识别连接结束。
     */
    cb_net_close(server);
    assert(cb_net_read(client, &missing, 1, 1000) == -1);

    cb_net_close(client);
    cb_net_close(listener);
    cb_net_cleanup();

    puts("Network tests passed");
    return 0;
}
