/*
 * 使用系统原生 TCP 接口：
 * Windows 为 Winsock，Mac 为 BSD sockets。
 *
 * 本模块只负责字节传输，不解释协议，也不访问剪贴板。
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "net.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32

#include <ws2tcpip.h>
#include <windows.h>

typedef int cb_socklen;

#else

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef socklen_t cb_socklen;

#endif

/*
 * 读取最近一次套接字操作的错误码。
 * Mac 使用 errno，Windows 使用 WSAGetLastError。
 */
static int socket_error(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

/* 判断是否因信号中断，可以重新尝试。 */
static int interrupted(int error)
{
#ifdef _WIN32
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

/* 判断非阻塞套接字是否暂时无法完成操作。 */
static int would_block(int error)
{
#ifdef _WIN32
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

/*
 * 将套接字切换为非阻塞模式。
 * 实际等待由 select 和统一超时逻辑管理。
 */
static int prepare_socket(cb_socket socket)
{
#ifdef _WIN32
    u_long enabled = 1;

    if (ioctlsocket(socket, FIONBIO, &enabled) != 0) {
        return -1;
    }
#else
    int flags = fcntl(socket, F_GETFL, 0);

    if (flags < 0 ||
        fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }

    /*
     * Mac 向已关闭的连接发送数据可能产生 SIGPIPE。
     * 禁止该信号，让发送失败通过返回值报告。
     */
#ifdef SO_NOSIGPIPE
    int enabled = 1;

    if (setsockopt(
            socket, SOL_SOCKET, SO_NOSIGPIPE,
            &enabled, sizeof(enabled)) < 0) {
        return -1;
    }
#endif
#endif

    return 0;
}

int cb_net_init(void)
{
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

void cb_net_cleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

void cb_net_close(cb_socket socket)
{
    if (socket == CB_INVALID) {
        return;
    }

#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

uint64_t cb_net_now(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec value;

    /*
     * 使用单调时钟：
     * 用户修改日期或系统校准时间不会改变超时计算。
     */
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        /* 该时钟在目标 Mac 系统上应始终可用。 */
        return 0;
    }

    return (uint64_t)value.tv_sec * 1000
         + (uint64_t)value.tv_nsec / 1000000;
#endif
}

void cb_net_sleep(unsigned milliseconds)
{
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec remaining;

    remaining.tv_sec = milliseconds / 1000;
    remaining.tv_nsec = (long)(milliseconds % 1000) * 1000000;

    /* 被信号打断时，继续休眠剩余时间。 */
    while (nanosleep(&remaining, &remaining) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
#endif
}

/* 计算距离截止时间的剩余毫秒数。 */
static unsigned time_left(uint64_t deadline)
{
    uint64_t now = cb_net_now();

    if (now >= deadline) {
        return 0;
    }

    uint64_t remaining = deadline - now;
    return remaining > UINT_MAX ? UINT_MAX : (unsigned)remaining;
}

int cb_net_wait(
    cb_socket socket,
    int write_ready,
    unsigned timeout_ms
)
{
    if (socket == CB_INVALID) {
        return -1;
    }

#ifndef _WIN32
    /*
     * POSIX 的 select 不能处理超出 FD_SETSIZE 的描述符。
     * 提前拒绝，避免 FD_SET 越界。
     */
    if (socket < 0 || socket >= FD_SETSIZE) {
        return -1;
    }
#endif

    uint64_t deadline = cb_net_now() + timeout_ms;

    for (;;) {
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(socket, &ready);

        unsigned remaining = time_left(deadline);

        struct timeval timeout;
        timeout.tv_sec = (long)(remaining / 1000);
        timeout.tv_usec = (long)(remaining % 1000) * 1000;

#ifdef _WIN32
        /* Winsock 忽略 select 的第一个参数。 */
        int count = select(
            0,
            write_ready ? NULL : &ready,
            write_ready ? &ready : NULL,
            NULL,
            &timeout
        );
#else
        int count = select(
            socket + 1,
            write_ready ? NULL : &ready,
            write_ready ? &ready : NULL,
            NULL,
            &timeout
        );
#endif

        if (count > 0) {
            return 1;
        }

        if (count == 0) {
            return 0;
        }

        if (!interrupted(socket_error())) {
            return -1;
        }

        /* 信号中断后仍沿用原截止时间，不重新开始整个超时。 */
        if (time_left(deadline) == 0) {
            return 0;
        }
    }
}

cb_socket cb_net_listen(const char *address, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    /* 监听地址必须明确，不能由域名解析成其他接口。 */
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

    if (getaddrinfo(address, port, &hints, &addresses) != 0) {
        return CB_INVALID;
    }

    cb_socket listener = CB_INVALID;

    for (struct addrinfo *a = addresses; a != NULL; a = a->ai_next) {
        /*
         * 拒绝 IPv4/IPv6 通配地址。
         * 正式使用时只绑定填写的 Tailscale IP。
         */
        if (a->ai_family == AF_INET) {
            const struct sockaddr_in *v4 =
                (const struct sockaddr_in *)a->ai_addr;

            if (v4->sin_addr.s_addr == htonl(INADDR_ANY)) {
                continue;
            }
        } else if (a->ai_family == AF_INET6) {
            const struct sockaddr_in6 *v6 =
                (const struct sockaddr_in6 *)a->ai_addr;
            const unsigned char zero[16] = {0};

            if (memcmp(&v6->sin6_addr, zero, sizeof(zero)) == 0) {
                continue;
            }
        }

        listener = socket(a->ai_family, a->ai_socktype, a->ai_protocol);

        if (listener == CB_INVALID) {
            continue;
        }

#ifdef _WIN32
        /*
         * Windows 使用独占绑定，避免其他进程同时占用该端口。
         */
        int enabled = 1;
        int option = SO_EXCLUSIVEADDRUSE;
#else
        /* Mac 允许程序退出后较快重新绑定原端口。 */
        int enabled = 1;
        int option = SO_REUSEADDR;
#endif

        if (setsockopt(
                listener, SOL_SOCKET, option,
                (const char *)&enabled, sizeof(enabled)) != 0 ||
            prepare_socket(listener) != 0 ||
            bind(listener, a->ai_addr, (cb_socklen)a->ai_addrlen) != 0 ||
            listen(listener, 1) != 0) {
            cb_net_close(listener);
            listener = CB_INVALID;
            continue;
        }

        break;
    }

    freeaddrinfo(addresses);
    return listener;
}

cb_socket cb_net_accept(cb_socket listener)
{
    cb_socket connection = accept(listener, NULL, NULL);

    if (connection == CB_INVALID) {
        return CB_INVALID;
    }

    if (prepare_socket(connection) != 0) {
        cb_net_close(connection);
        return CB_INVALID;
    }

    return connection;
}

cb_socket cb_net_connect(
    const char *address,
    const char *port,
    unsigned timeout_ms
)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    if (getaddrinfo(address, port, &hints, &addresses) != 0) {
        return CB_INVALID;
    }

    cb_socket connection = CB_INVALID;

    for (struct addrinfo *a = addresses; a != NULL; a = a->ai_next) {
        connection = socket(a->ai_family, a->ai_socktype, a->ai_protocol);

        if (connection == CB_INVALID) {
            continue;
        }

        if (prepare_socket(connection) != 0) {
            cb_net_close(connection);
            connection = CB_INVALID;
            continue;
        }

        int result = connect(
            connection, a->ai_addr, (cb_socklen)a->ai_addrlen
        );

        if (result != 0) {
            int error = socket_error();

#ifdef _WIN32
            int pending = error == WSAEWOULDBLOCK
                       || error == WSAEINPROGRESS;
#else
            int pending = error == EINPROGRESS;
#endif

            if (!pending ||
                cb_net_wait(connection, 1, timeout_ms) != 1) {
                cb_net_close(connection);
                connection = CB_INVALID;
                continue;
            }
        }

        /*
         * 可写不一定表示连接成功，也可能是连接失败。
         * 读取 SO_ERROR 确认最终连接结果。
         */
        int final_error = 0;
        cb_socklen error_length = sizeof(final_error);

        if (getsockopt(
                connection, SOL_SOCKET, SO_ERROR,
                (char *)&final_error, &error_length) != 0 ||
            final_error != 0) {
            cb_net_close(connection);
            connection = CB_INVALID;
            continue;
        }

        break;
    }

    freeaddrinfo(addresses);
    return connection;
}

/*
 * 统一处理完整收发。
 *
 * write_mode 为 0 时读取，为 1 时发送。
 * 每次只处理 INT_MAX 以内的字节，兼容 Winsock 的长度参数。
 */
static int transfer(
    cb_socket socket,
    void *buffer,
    size_t length,
    unsigned timeout_ms,
    int write_mode
)
{
    unsigned char *bytes = buffer;
    size_t completed = 0;
    uint64_t deadline = cb_net_now() + timeout_ms;

    while (completed < length) {
        unsigned remaining = time_left(deadline);

        if (remaining == 0 ||
            cb_net_wait(socket, write_mode, remaining) != 1) {
            return -1;
        }

        size_t available = length - completed;
        int chunk = available > INT_MAX ? INT_MAX : (int)available;

        int flags = 0;

        /*
         * 某些 POSIX 平台支持按单次发送禁止 SIGPIPE。
         * Mac 已通过 SO_NOSIGPIPE 设置同等行为。
         */
#ifdef MSG_NOSIGNAL
        if (write_mode) {
            flags = MSG_NOSIGNAL;
        }
#endif

#ifdef _WIN32
        int count;
#else
        ssize_t count;
#endif

        if (write_mode) {
            count = send(
                socket,
                (const char *)(bytes + completed),
                chunk,
                flags
            );
        } else {
            count = recv(
                socket,
                (char *)(bytes + completed),
                chunk,
                0
            );
        }

        if (count > 0) {
            completed += (size_t)count;
            continue;
        }

        /* 读取返回 0 表示对端已关闭连接。 */
        if (count == 0) {
            return -1;
        }

        int error = socket_error();

        if (interrupted(error) || would_block(error)) {
            continue;
        }

        return -1;
    }

    return 0;
}

int cb_net_read(
    cb_socket socket,
    void *buffer,
    size_t length,
    unsigned timeout_ms
)
{
    return transfer(socket, buffer, length, timeout_ms, 0);
}

int cb_net_write(
    cb_socket socket,
    const void *buffer,
    size_t length,
    unsigned timeout_ms
)
{
    /*
     * transfer 的发送分支不会修改缓冲区。
     * 转换指针仅用于复用统一的收发实现。
     */
    return transfer(socket, (void *)buffer, length, timeout_ms, 1);
}
