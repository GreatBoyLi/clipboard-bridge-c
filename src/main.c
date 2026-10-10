/*
 * Clipboard Bridge 主程序。
 *
 * 工作流程：
 *   1. 读取设备配对密钥；
 *   2. 建立 TCP 连接；
 *   3. 双方验证配对密钥；
 *   4. 检测本机新复制的文本并发送；
 *   5. 将远端文本写入本机剪贴板；
 *   6. 连接中断后重新等待或主动重连。
 *
 * 当前只支持文本，不处理图片、文件或快捷键。
 *
 * 网络数据本身没有额外加密：
 * 正式使用时必须通过 Tailscale 地址连接。
 * 配对密钥用于认证设备，不代替网络加密。
 *
 * 日志不输出剪贴板内容或配对密钥。
 */

#include "net.h"
#include "platform.h"
#include "protocol.h"

#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

/* 默认监听端口；也可以通过 --port 修改。 */
#define DEFAULT_PORT "47321"

/* 完整发送或接收一条消息的超时。 */
#define IO_TIMEOUT_MS 5000u

/* 文本发送后，等待对端写入确认的超时。 */
#define ACK_TIMEOUT_MS 5000u

/* 空闲时每隔 10 秒发送一次心跳。 */
#define HEARTBEAT_MS 10000u

/* 超过 30 秒没有收到任何有效消息，认为连接失效。 */
#define PEER_TIMEOUT_MS 30000u

/* 连接失败后的重试间隔。 */
#define RECONNECT_MS 2000u

/* 每轮最多等待 100 毫秒，然后检查本机剪贴板。 */
#define POLL_MS 100u

/*
 * 信号处理函数只修改退出标记。
 * 不在信号处理函数里调用网络、剪贴板或内存管理函数。
 */
static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void print_usage(const char *program)
{
    fprintf(stderr,
        "Clipboard Bridge: text synchronization over Tailscale\n"
        "\n"
        "Generate a pairing key:\n"
        "  %s --generate-key PATH\n"
        "\n"
        "Listen for a connection:\n"
        "  %s --listen IP --key-file PATH [--port PORT]\n"
        "\n"
        "Connect to another device:\n"
        "  %s --connect HOST --key-file PATH [--port PORT]\n"
        "\n"
        "Use a Tailscale address for normal operation.\n",
        program, program, program);
}

/*
 * 检查端口字符串。
 * 不允许负数、非数字、空字符串或超出端口范围的数值。
 */
static int valid_port(const char *port)
{
    if (port == NULL || *port == '\0') {
        return 0;
    }

    unsigned value = 0;

    for (const unsigned char *p =
             (const unsigned char *)port; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return 0;
        }

        value = value * 10 + (unsigned)(*p - '0');

        /* 及时检查，避免很长的输入导致整数溢出。 */
        if (value > 65535u) {
            return 0;
        }
    }

    return value != 0;
}

/*
 * 生成配对密钥文件。
 *
 * 32 个随机字节转换成 64 个十六进制字符，末尾添加换行。
 * 使用独占创建，已存在的文件不会被覆盖。
 *
 * Mac 文件权限设置为仅当前用户可读写。
 * Windows 文件访问权限由所在目录的 ACL 管理。
 */
static int generate_key(const char *path)
{
    unsigned char random_bytes[32];
    char output[CB_KEY_SIZE + 1];
    static const char digits[] = "0123456789abcdef";

    if (cb_platform_random(random_bytes, sizeof(random_bytes)) != 0) {
        fprintf(stderr, "System random generator failed.\n");
        return -1;
    }

    for (size_t i = 0; i < sizeof(random_bytes); ++i) {
        output[i * 2] = digits[random_bytes[i] >> 4];
        output[i * 2 + 1] = digits[random_bytes[i] & 0x0f];
    }

    output[CB_KEY_SIZE] = '\n';

#ifdef _WIN32
    int file = _open(
        path,
        _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
        _S_IREAD | _S_IWRITE
    );
#else
    int file = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif

    if (file < 0) {
        fprintf(stderr,
            "Cannot create key file; check the path or existing file.\n");
        return -1;
    }

    size_t completed = 0;
    int success = 1;

    while (completed < sizeof(output)) {
#ifdef _WIN32
        int written = _write(
            file, output + completed,
            (unsigned)(sizeof(output) - completed)
        );
#else
        ssize_t written = write(
            file, output + completed,
            sizeof(output) - completed
        );
#endif

        if (written <= 0) {
            success = 0;
            break;
        }

        completed += (size_t)written;
    }

#ifdef _WIN32
    if (_close(file) != 0) {
        success = 0;
    }
#else
    if (close(file) != 0) {
        success = 0;
    }
#endif

    if (!success) {
        /* 不留下可能被误认为有效密钥的半成品。 */
        remove(path);
        fprintf(stderr, "Key file write failed.\n");
        return -1;
    }

    puts("Pairing key file created. Do not commit or share it publicly.");
    return 0;
}

/*
 * 读取密钥文件。
 *
 * 允许：
 *   64 个十六进制字符；
 *   后接可选的 LF 或 CRLF 换行。
 *
 * 不允许多余数据或被截断的密钥。
 * key 不作为普通字符串使用，因此无需末尾 '\0'。
 */
static int load_key(const char *path, char key[CB_KEY_SIZE])
{
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        fprintf(stderr, "Cannot open pairing key file.\n");
        return -1;
    }

    int valid = 1;

    for (size_t i = 0; i < CB_KEY_SIZE; ++i) {
        int value = fgetc(file);

        if (value == EOF || !isxdigit((unsigned char)value)) {
            valid = 0;
            break;
        }

        key[i] = (char)value;
    }

    if (valid) {
        int tail = fgetc(file);

        if (tail == '\r') {
            if (fgetc(file) != '\n') {
                valid = 0;
            }

            tail = fgetc(file);
        } else if (tail == '\n') {
            tail = fgetc(file);
        }

        if (tail != EOF) {
            valid = 0;
        }
    }

    if (ferror(file)) {
        valid = 0;
    }

    fclose(file);

    if (!valid) {
        fprintf(stderr,
            "Invalid key file: expected 64 hexadecimal characters.\n");
        return -1;
    }

    return 0;
}

/*
 * 返回距离截止时间的剩余毫秒数。
 * 用于让消息头和消息体共用同一个总超时。
 */
static unsigned remaining_time(uint64_t deadline)
{
    uint64_t now = cb_net_now();

    if (now >= deadline) {
        return 0;
    }

    return (unsigned)(deadline - now);
}

/*
 * 发送一条完整消息。
 *
 * 此函数不取得 data 的所有权。
 * 消息头和消息体共享 IO_TIMEOUT_MS 的总时间。
 */
static int send_frame(
    cb_socket connection,
    unsigned char type,
    uint32_t sequence,
    const void *data,
    uint32_t length
)
{
    cb_header header = {type, sequence, length};
    unsigned char wire[CB_HEADER_SIZE];

    cb_encode_header(wire, &header);

    uint64_t deadline = cb_net_now() + IO_TIMEOUT_MS;

    if (cb_net_write(
            connection, wire, sizeof(wire),
            remaining_time(deadline)) != 0) {
        return -1;
    }

    if (length != 0 &&
        cb_net_write(
            connection, data, length,
            remaining_time(deadline)) != 0) {
        return -1;
    }

    return 0;
}

/*
 * 接收并校验一条完整消息。
 *
 * 成功时：
 *   header 保存已校验的消息头；
 *   *data 保存 malloc 分配的消息体；
 *   调用方必须 free(*data)。
 *
 * 缓冲区额外分配一个 '\0'，便于后续操作文本，
 * 但该字节不是网络消息的一部分。
 *
 * 失败时输出 data 为 NULL，调用方应关闭连接。
 */
static int receive_frame(
    cb_socket connection,
    cb_header *header,
    unsigned char **data
)
{
    unsigned char wire[CB_HEADER_SIZE];
    *data = NULL;

    uint64_t deadline = cb_net_now() + IO_TIMEOUT_MS;

    if (cb_net_read(
            connection, wire, sizeof(wire),
            remaining_time(deadline)) != 0) {
        return -1;
    }

    /*
     * 先校验长度再分配内存，防止异常消息要求
     * 分配超大缓冲区。
     */
    if (cb_decode_header(wire, header) != 0) {
        return -1;
    }

    unsigned char *buffer = malloc((size_t)header->length + 1);

    if (buffer == NULL) {
        return -1;
    }

    if (header->length != 0 &&
        cb_net_read(
            connection, buffer, header->length,
            remaining_time(deadline)) != 0) {
        free(buffer);
        return -1;
    }

    buffer[header->length] = '\0';
    *data = buffer;

    return 0;
}

/*
 * 双方验证相同的配对密钥。
 *
 * 客户端先发送认证消息。
 * 服务端确认正确后才发送自己的认证消息。
 * 认证完成之前，禁止任何剪贴板操作。
 */
static int authenticate(
    cb_socket connection,
    int listener_mode,
    const char key[CB_KEY_SIZE]
)
{
    if (!listener_mode &&
        send_frame(connection, CB_AUTH, 0, key, CB_KEY_SIZE) != 0) {
        return -1;
    }

    cb_header header;
    unsigned char *data = NULL;

    if (receive_frame(connection, &header, &data) != 0) {
        return -1;
    }

    int valid = header.type == CB_AUTH &&
                cb_key_equal(
                    key, (const char *)data, CB_KEY_SIZE);

    free(data);

    if (!valid) {
        return -1;
    }

    if (listener_mode &&
        send_frame(connection, CB_AUTH, 0, key, CB_KEY_SIZE) != 0) {
        return -1;
    }

    return 0;
}

/* 序号递增；保留 0 给认证等控制消息。 */
static uint32_t next_sequence(uint32_t sequence)
{
    ++sequence;
    return sequence == 0 ? 1 : sequence;
}

/*
 * 运行一个已经认证的连接。
 *
 * 同时最多有一条文本等待 ACK。
 * 等待期间再次复制时，只保留最新文本，不积累历史。
 *
 * 连接中断时放弃尚未完成的同步，不在重连后补发旧内容。
 * 用户下一次复制时，再发送新的内容。
 */
static void run_session(cb_socket connection)
{
    char *queued_text = NULL;
    size_t queued_length = 0;

    uint32_t outgoing_sequence = 0;
    uint32_t incoming_sequence = 0;
    uint32_t pending_ack = 0;

    uint64_t ack_deadline = 0;
    uint64_t last_received = cb_net_now();
    uint64_t last_sent = last_received;

    /*
     * 将连接建立前的剪贴板内容设为基线。
     * 不自动覆盖两台设备各自已有的内容。
     */
    cb_platform_reset();

    puts("Connected and authenticated. Clipboard synchronization active.");

    while (!stop_requested) {
        uint64_t now = cb_net_now();

        if (now - last_received >= PEER_TIMEOUT_MS) {
            fprintf(stderr, "Peer connection timed out.\n");
            break;
        }

        if (pending_ack != 0 && now >= ack_deadline) {
            fprintf(stderr, "Clipboard acknowledgement timed out.\n");
            break;
        }

        int ready = cb_net_wait(connection, 0, POLL_MS);

        if (ready < 0) {
            break;
        }

        if (ready == 1) {
            cb_header header;
            unsigned char *data = NULL;

            if (receive_frame(connection, &header, &data) != 0) {
                break;
            }

            int valid = 1;

            switch (header.type) {
            case CB_TEXT:
                /*
                 * 一个连接内的文本序号必须连续。
                 * TCP 会保证顺序，因此乱序消息属于协议错误。
                 */
                if (header.sequence != next_sequence(incoming_sequence) ||
                    !cb_valid_utf8(data, header.length)) {
                    valid = 0;
                    break;
                }

                /*
                 * 只有成功写入系统剪贴板后才发送 ACK。
                 * 写入失败则结束连接，不报告同步成功。
                 */
                if (cb_platform_set(
                        (const char *)data, header.length) != 0) {
                    fprintf(stderr, "Remote text could not be applied.\n");
                    valid = 0;
                    break;
                }

                incoming_sequence = header.sequence;

                if (send_frame(
                        connection, CB_ACK,
                        header.sequence, NULL, 0) != 0) {
                    valid = 0;
                    break;
                }

                last_sent = cb_net_now();

                printf("Received and applied text: %u bytes.\n",
                       (unsigned)header.length);
                break;

            case CB_ACK:
                if (pending_ack == 0 || header.sequence != pending_ack) {
                    valid = 0;
                    break;
                }

                pending_ack = 0;
                puts("Remote clipboard update confirmed.");
                break;

            case CB_PING:
                if (send_frame(
                        connection, CB_PONG,
                        header.sequence, NULL, 0) != 0) {
                    valid = 0;
                    break;
                }

                last_sent = cb_net_now();
                break;

            case CB_PONG:
                break;

            default:
                /* 认证后不能再次发送 AUTH 或未知消息。 */
                valid = 0;
                break;
            }

            free(data);

            if (!valid) {
                fprintf(stderr, "Session ended due to a protocol or apply error.\n");
                break;
            }

            last_received = cb_net_now();
        }

        /*
         * 所有剪贴板调用都在主线程执行。
         * 平台接口会抑制程序刚刚写入的远端文本。
         */
        char *local_text = NULL;
        size_t local_length = 0;

        int changed = cb_platform_poll(&local_text, &local_length);

        if (changed == 1) {
            /*
             * 在公共层再次检查，避免不同平台实现
             * 意外产生不符合协议的文本。
             */
            if (local_length <= CB_MAX_TEXT &&
                cb_valid_utf8(
                    (const unsigned char *)local_text, local_length)) {
                free(queued_text);
                queued_text = local_text;
                queued_length = local_length;
                local_text = NULL;
            } else {
                fprintf(stderr, "Local text exceeds the protocol limits.\n");
            }
        } else if (changed < 0) {
            fprintf(stderr, "Clipboard read failed or text was unsupported.\n");
        }

        free(local_text);

        if (queued_text != NULL && pending_ack == 0) {
            outgoing_sequence = next_sequence(outgoing_sequence);

            if (send_frame(
                    connection, CB_TEXT, outgoing_sequence,
                    queued_text, (uint32_t)queued_length) != 0) {
                break;
            }

            printf("Sent text: %zu bytes; waiting for confirmation.\n",
                   queued_length);

            free(queued_text);
            queued_text = NULL;
            queued_length = 0;

            pending_ack = outgoing_sequence;
            last_sent = cb_net_now();
            ack_deadline = last_sent + ACK_TIMEOUT_MS;
        }

        now = cb_net_now();

        if (now - last_sent >= HEARTBEAT_MS) {
            if (send_frame(connection, CB_PING, 0, NULL, 0) != 0) {
                break;
            }

            last_sent = cb_net_now();
        }
    }

    free(queued_text);

    puts("Disconnected. Pending old text will not be replayed.");
}

/*
 * 重连等待分成小段，Ctrl+C 后不必等待整个间隔结束。
 */
static void wait_before_retry(void)
{
    for (unsigned elapsed = 0;
         elapsed < RECONNECT_MS && !stop_requested;
         elapsed += POLL_MS) {
        cb_net_sleep(POLL_MS);
    }
}

int main(int argc, char **argv)
{
    /*
     * 禁用标准输出缓冲，让连接和同步日志立即显示。
     * _IONBF 不使用缓冲区，允许长度为 0，兼容 Mac 和 Windows。
     * 必须在程序首次向 stdout 输出之前设置。
     */
    if (setvbuf(stdout, NULL, _IONBF, 0) != 0) {
        fprintf(stderr, "Cannot configure standard output.\n");
        return 1;
    }

    if (argc == 3 && strcmp(argv[1], "--generate-key") == 0) {
        return generate_key(argv[2]) == 0 ? 0 : 1;
    }

    int listener_mode = -1;
    const char *address = NULL;
    const char *port = DEFAULT_PORT;
    const char *key_path = NULL;
    int port_seen = 0;

    /*
     * 参数按“选项 + 值”解析。
     * 不接受未知选项，也不允许同时指定监听和连接模式。
     */
    for (int i = 1; i < argc; ++i) {
        if (i + 1 >= argc) {
            print_usage(argv[0]);
            return 1;
        }

        const char *option = argv[i++];
        const char *value = argv[i];

        if (strcmp(option, "--listen") == 0 && listener_mode == -1) {
            listener_mode = 1;
            address = value;
        } else if (strcmp(option, "--connect") == 0 &&
                   listener_mode == -1) {
            listener_mode = 0;
            address = value;
        } else if (strcmp(option, "--key-file") == 0 &&
                   key_path == NULL) {
            key_path = value;
        } else if (strcmp(option, "--port") == 0 && !port_seen) {
            port = value;
            port_seen = 1;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (listener_mode == -1 ||
        address == NULL || *address == '\0' ||
        key_path == NULL || !valid_port(port)) {
        print_usage(argv[0]);
        return 1;
    }

    char key[CB_KEY_SIZE];

    if (load_key(key_path, key) != 0) {
        return 1;
    }

    if (cb_net_init() != 0) {
        fprintf(stderr, "Network initialization failed.\n");
        return 1;
    }

    if (cb_platform_init() != 0) {
        fprintf(stderr, "Clipboard initialization failed.\n");
        cb_net_cleanup();
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);


    cb_socket listener = CB_INVALID;
    int exit_code = 0;

    if (listener_mode) {
        listener = cb_net_listen(address, port);

        if (listener == CB_INVALID) {
            fprintf(stderr,
                "Cannot listen on the specified IP and port.\n"
                "Check that Tailscale is connected and the IP is local.\n");
            exit_code = 1;
            goto cleanup;
        }

        printf("Listening on %s:%s. Press Ctrl+C to stop.\n",
               address, port);
    } else {
        printf("Connecting to %s:%s. Press Ctrl+C to stop.\n",
               address, port);
    }

    while (!stop_requested) {
        cb_socket connection = CB_INVALID;

        if (listener_mode) {
            int ready = cb_net_wait(listener, 0, POLL_MS);

            if (ready < 0) {
                fprintf(stderr, "Listener failed.\n");
                exit_code = 1;
                break;
            }

            if (ready == 0) {
                continue;
            }

            connection = cb_net_accept(listener);
        } else {
            connection = cb_net_connect(
                address, port, IO_TIMEOUT_MS);
        }

        if (connection == CB_INVALID) {
            if (!listener_mode && !stop_requested) {
                fprintf(stderr, "Connection failed; retrying.\n");
                wait_before_retry();
            }

            continue;
        }

        if (authenticate(connection, listener_mode, key) != 0) {
            fprintf(stderr, "Authentication failed or timed out.\n");
        } else if (!stop_requested) {
            run_session(connection);
        }

        cb_net_close(connection);

        if (!listener_mode && !stop_requested) {
            wait_before_retry();
        }
    }

cleanup:
    cb_net_close(listener);
    cb_platform_close();
    cb_net_cleanup();

    puts("Clipboard Bridge stopped.");
    return exit_code;
}
