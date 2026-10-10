#ifndef CB_NET_H
#define CB_NET_H

#include <stddef.h>
#include <stdint.h>

/*
 * 平台之间的套接字类型不同：
 * Windows 使用 SOCKET，Mac 使用整数文件描述符。
 */
#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET cb_socket;
#define CB_INVALID INVALID_SOCKET
#else
typedef int cb_socket;
#define CB_INVALID (-1)
#endif

/*
 * 初始化网络环境。
 * Windows 需要初始化 Winsock；Mac 无需额外初始化。
 * 成功返回 0，失败返回 -1。
 */
int cb_net_init(void);

/* 释放网络环境；应在全部连接关闭后调用。 */
void cb_net_cleanup(void);

/* 关闭套接字；传入 CB_INVALID 时不执行任何操作。 */
void cb_net_close(cb_socket socket);

/*
 * 在指定 IP 和端口上监听连接。
 *
 * address 必须是明确的数字 IP，不接受域名或通配地址。
 * 正式运行时填写本机 Tailscale IP，避免监听所有网络接口。
 * 测试时使用 127.0.0.1。
 *
 * port 是端口字符串，例如 "47321"。
 * 成功返回监听套接字，失败返回 CB_INVALID。
 */
cb_socket cb_net_listen(const char *address, const char *port);

/*
 * 接收一个连接。
 * 监听套接字为非阻塞模式；当前无连接时返回 CB_INVALID。
 * 调用方可以先使用 cb_net_wait 等待监听套接字可读。
 */
cb_socket cb_net_accept(cb_socket listener);

/*
 * 连接远端设备。
 *
 * address 可以是 IP 或 Tailscale 设备域名。
 * 每个候选地址的 TCP 连接最多等待 timeout_ms 毫秒。
 * 系统域名解析时间不包含在该超时中。
 *
 * 成功返回连接套接字，失败返回 CB_INVALID。
 */
cb_socket cb_net_connect(
    const char *address,
    const char *port,
    unsigned timeout_ms
);

/*
 * 等待套接字就绪。
 *
 * write_ready 为 0：等待可读，包括对端关闭连接。
 * write_ready 非 0：等待可写。
 *
 * 返回值：
 *   1：套接字就绪。
 *   0：等待超时。
 *  -1：发生错误。
 */
int cb_net_wait(
    cb_socket socket,
    int write_ready,
    unsigned timeout_ms
);

/*
 * 在总超时内完整读取或发送指定数量的字节。
 *
 * TCP 没有消息边界，一次收发可能只完成部分数据，
 * 因此函数内部会持续处理，直到完成全部字节。
 *
 * 返回值：
 *   0：全部完成。
 *  -1：超时、连接关闭或其他错误。
 *
 * 失败前可能已经处理部分数据。
 * 发生失败后，调用方必须关闭连接，不能继续复用它。
 *
 * buffer 至少包含 length 字节；length 为 0 时不访问 buffer。
 * 不分配内存，缓冲区由调用方管理。
 */
int cb_net_read(
    cb_socket socket,
    void *buffer,
    size_t length,
    unsigned timeout_ms
);

int cb_net_write(
    cb_socket socket,
    const void *buffer,
    size_t length,
    unsigned timeout_ms
);

/* 返回单调时钟的毫秒数，用于超时计算，不受修改系统时间影响。 */
uint64_t cb_net_now(void);

/* 暂停指定毫秒数，用于重连间隔等场景。 */
void cb_net_sleep(unsigned milliseconds);

#endif /* CB_NET_H */
