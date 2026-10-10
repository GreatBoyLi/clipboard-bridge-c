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
 * 剪贴板支持纯文本及 PNG 图片，同一次复制可同时保留两种格式。
 * Mac 可选键盘映射及两指速度控制；不传输文件内容。
 *
 * 网络数据本身没有额外加密：
 * 正式使用时必须通过 Tailscale 地址连接。
 * 配对密钥用于认证设备，不代替网络加密。
 *
 * 日志不输出剪贴板内容或配对密钥。
 */

#include "keyboard.h"
#include "scroll_control.h"
#include "net.h"
#include "platform.h"
#include "protocol.h"
#include "content.h"

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
static int scroll_enabled;

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
        "  %s --listen IP --key-file PATH [--port PORT] [--scroll-lines]\n"
        "\n"
        "Connect to another device:\n"
        "  %s --connect HOST --key-file PATH [--port PORT] [--keyboard] [--redo ctrl-y|ctrl-shift-z]\n"
        "\n"
        "Keyboard mapping is optional and macOS only.\n"
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
    /* 图片分块交替收发，避免两端同时发送大内容时填满 TCP 缓冲区。
     * 接收缓冲区只在 BEGIN 的尺寸检查通过后分配，断线立即释放。 */
    int queued_image=0;
    unsigned char *sending_image=NULL,*receiving_image=NULL;
    size_t sending_size=0,sending_offset=0,receiving_size=0,receiving_offset=0;
    int chunk_ready=0;
    uint32_t receiving_sequence=0;
    uint64_t receiving_deadline=0;
    int paste_waiting = 0;
    int scroll_peer_ready=0;
    unsigned last_lines=0;
    uint64_t scroll_sent=0;

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
    cb_keyboard_connected(1);

    if(scroll_enabled && send_frame(connection,CB_SCROLL_CAPS,0,NULL,0)!=0) goto session_done;
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

        if(receiving_image && now>=receiving_deadline) {
            fprintf(stderr,"Image transfer progress timed out.\n");break;
        }
        int ready = cb_net_wait(connection, 0, (sending_image && chunk_ready) ? 0 : POLL_MS);

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
            case CB_IMAGE_BEGIN: {
                uint32_t flags=((uint32_t)data[0]<<24)|((uint32_t)data[1]<<16)|((uint32_t)data[2]<<8)|data[3];
                uint32_t text=((uint32_t)data[4]<<24)|((uint32_t)data[5]<<16)|((uint32_t)data[6]<<8)|data[7];
                uint32_t png=((uint32_t)data[8]<<24)|((uint32_t)data[9]<<16)|((uint32_t)data[10]<<8)|data[11];
                if(receiving_image || header.sequence!=next_sequence(incoming_sequence) ||
                   flags>1 || (!flags && text) || text>CB_MAX_TEXT || png<45 || png>CB_MAX_PNG) {valid=0;break;}
                receiving_size=CB_CONTENT_META+(size_t)text+png;
                receiving_image=malloc(receiving_size);
                if(!receiving_image) {valid=0;break;}
                memcpy(receiving_image,data,CB_CONTENT_META);
                receiving_offset=CB_CONTENT_META;receiving_sequence=header.sequence;
                receiving_deadline=cb_net_now()+PEER_TIMEOUT_MS;
                break;
            }
            case CB_IMAGE_CHUNK: {
                if(!receiving_image || header.sequence!=receiving_sequence ||
                   header.length>receiving_size-receiving_offset) {valid=0;break;}
                memcpy(receiving_image+receiving_offset,data,header.length);
                receiving_offset+=header.length;receiving_deadline=cb_net_now()+PEER_TIMEOUT_MS;
                if(receiving_offset==receiving_size) {
                    cb_content content={0};
                    if(cb_content_unpack(receiving_image,receiving_size,&content)!=0 ||
                       cb_platform_set_content(&content)!=0) {
                        cb_content_free(&content);valid=0;break;
                    }
                    printf("Received and applied image: %zu PNG bytes, %zu text bytes.\n",content.png_length,content.text_length);
                    cb_content_free(&content);free(receiving_image);receiving_image=NULL;
                    incoming_sequence=header.sequence;
                    if(send_frame(connection,CB_ACK,header.sequence,NULL,0)!=0) {valid=0;break;}
                    last_sent=cb_net_now();
                } else {
                    /* 每次只允许一块在途，防止双向大传输互相堵在 send。 */
                    if(send_frame(connection,CB_IMAGE_CREDIT,header.sequence,NULL,0)!=0) valid=0;
                    last_sent=cb_net_now();
                }
                break;
            }
            case CB_IMAGE_CREDIT:
                if(!sending_image || chunk_ready || header.sequence!=pending_ack) valid=0;
                else {chunk_ready=1;ack_deadline=cb_net_now()+PEER_TIMEOUT_MS;}
                break;
            case CB_TEXT:
                /*
                 * 一个连接内的文本序号必须连续。
                 * TCP 会保证顺序，因此乱序消息属于协议错误。
                 */
                if (receiving_image || header.sequence != next_sequence(incoming_sequence) ||
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
                if (sending_image || pending_ack == 0 || header.sequence != pending_ack) {
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

            case CB_SCROLL_CAPS:
#ifdef __APPLE__
                scroll_peer_ready=1;
#else
                valid=0;
#endif
                break;
            case CB_SCROLL_LINES:
                if(!scroll_enabled || data[0]>5) valid=0;
                else if(cb_scroll_set(data[0])!=0) {
                    fprintf(stderr,"Scroll guard unavailable; stopping to restore settings.\n");
                    valid=0;stop_requested=1;
                }
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

        /* 只给声明支持的已认证 Windows 发送；不碰剪贴板 ACK 序号。
         * 200ms 活跃续租，0 立即释放，守护端独立检测超时。 */
        if(scroll_peer_ready) {
            unsigned lines=cb_keyboard_scroll_lines();
            now=cb_net_now();
            if(lines!=last_lines || (lines && now-scroll_sent>=200)) {
                unsigned char body=(unsigned char)lines;
                if(send_frame(connection,CB_SCROLL_LINES,0,&body,1)!=0) break;
                last_lines=lines;scroll_sent=now;last_sent=now;
            }
        }
        /*
         * 所有剪贴板调用都在主线程执行。
         * 平台接口会抑制程序刚刚写入的远端文本。
         */
        cb_content local={0};
        char *local_text = NULL;
        size_t local_length = 0;

        /* 粘贴请求不强制重发 Mac 的旧内容，避免覆盖 Windows 复制的文件。
         * 先检查是否有新文字，再决定是否需等网络确认。
         */
        /* 每轮都服务可选手势观察器，不能在等待文字 ACK 时暂停主事件循环。 */
        int paste_requested = cb_keyboard_take_paste();
        if (!paste_waiting && paste_requested) paste_waiting = 1;
        int changed = cb_platform_poll_content(&local);
        int local_image=local.png!=NULL;
        if(changed==1 && local_image) {
            unsigned char *body=NULL;
            if(cb_content_pack(&local,&body,&local_length)!=0) changed=-1;
            else local_text=(char *)body;
        } else if(changed==1) {
            local_text=local.text;local_length=local.text_length;local.text=NULL;
        }
        cb_content_free(&local);

        if (changed == 1) {
            /*
             * 在公共层再次检查，避免不同平台实现
             * 意外产生不符合协议的文本。
             */
            if (local_text && (local_image || (local_length <= CB_MAX_TEXT &&
                cb_valid_utf8((const unsigned char *)local_text, local_length)))) {
                free(queued_text);
                queued_text = local_text;
                queued_length = local_length;
                queued_image=local_image;
                local_text = NULL;
            } else {
                free(queued_text);queued_text=NULL;queued_length=0;
                fprintf(stderr, "Local clipboard content exceeds the protocol limits.\n");
                if (paste_waiting) cb_keyboard_finish_paste(0);
                paste_waiting = 0;
            }
        } else if (changed < 0) {
            /* 新复制被拒绝时丢弃旧的排队快照，不能随后覆盖为更旧内容。 */
            free(queued_text);queued_text=NULL;queued_length=0;
            fprintf(stderr, "Clipboard read failed or content exceeded limits.\n");
            if (paste_waiting) cb_keyboard_finish_paste(0);
            paste_waiting = 0;
        }

        free(local_text);

        if (queued_text != NULL && pending_ack == 0) {
            outgoing_sequence = next_sequence(outgoing_sequence);
            pending_ack=outgoing_sequence;
            if(queued_image) {
                if(send_frame(connection,CB_IMAGE_BEGIN,outgoing_sequence,queued_text,CB_CONTENT_META)!=0) break;
                sending_image=(unsigned char *)queued_text;sending_size=queued_length;
                sending_offset=CB_CONTENT_META;chunk_ready=1;ack_deadline=cb_net_now()+PEER_TIMEOUT_MS;
                printf("Sending image clipboard: %zu bytes.\n",queued_length);
            } else {
                if(send_frame(connection,CB_TEXT,outgoing_sequence,queued_text,(uint32_t)queued_length)!=0) break;
                printf("Sent text: %zu bytes; waiting for confirmation.\n",queued_length);
                free(queued_text);ack_deadline=cb_net_now()+ACK_TIMEOUT_MS;
            }
            queued_text=NULL;queued_length=0;last_sent=cb_net_now();
        }
        if(sending_image && chunk_ready) {
            size_t chunk=sending_size-sending_offset;
            if(chunk>CB_CONTENT_CHUNK_SIZE) chunk=CB_CONTENT_CHUNK_SIZE;
            if(send_frame(connection,CB_IMAGE_CHUNK,pending_ack,sending_image+sending_offset,(uint32_t)chunk)!=0) break;
            sending_offset+=chunk;last_sent=cb_net_now();chunk_ready=0;ack_deadline=last_sent+PEER_TIMEOUT_MS;
            if(sending_offset==sending_size) {
                free(sending_image);sending_image=NULL;
                /* ACK 覆盖完整内容和原生解码；不能在 BEGIN 时开始 5 秒倒计时。 */
                ack_deadline=last_sent+PEER_TIMEOUT_MS;
            }
        }

        /* 必须在处理最新剪贴板及发送队列之后放行，不能在旧 ACK 到来时提前粘贴。
         * 没有 Mac 新文字时，不发送 TEXT，直接使用 Windows 当前剪贴板。
         */
        if (paste_waiting && queued_text == NULL && pending_ack == 0) {
            cb_keyboard_finish_paste(1);
            paste_waiting = 0;
        }

        now = cb_net_now();

        if (now - last_sent >= HEARTBEAT_MS) {
            if (send_frame(connection, CB_PING, 0, NULL, 0) != 0) {
                break;
            }

            last_sent = cb_net_now();
        }
    }

session_done:
    if(scroll_enabled) (void)cb_scroll_set(0);
    cb_keyboard_connected(0);
    free(queued_text);free(sending_image);free(receiving_image);

    puts("Disconnected. Pending old clipboard content will not be replayed.");
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
    int guard_result=cb_scroll_guard_entry(argc,argv);
    if(guard_result>=0) return guard_result;
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
    int keyboard_enabled = 0, redo_seen = 0, redo_shift_z = 0;

    /*
     * 参数按“选项 + 值”解析。
     * 不接受未知选项，也不允许同时指定监听和连接模式。
     */
    for (int i = 1; i < argc; ++i) {
        if(strcmp(argv[i],"--scroll-lines")==0 && !scroll_enabled) {
            scroll_enabled=1;continue;
        }
        if (strcmp(argv[i], "--keyboard") == 0 && !keyboard_enabled) {
            keyboard_enabled = 1;
            continue;
        }
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
        } else if (strcmp(option, "--redo") == 0 && !redo_seen &&
                   (strcmp(value, "ctrl-y") == 0 || strcmp(value, "ctrl-shift-z") == 0)) {
            redo_seen = 1;
            redo_shift_z = strcmp(value, "ctrl-shift-z") == 0;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if ((redo_seen && !keyboard_enabled) || listener_mode == -1 ||
        address == NULL || *address == '\0' ||
        key_path == NULL || !valid_port(port)) {
        print_usage(argv[0]);
        return 1;
    }

#ifndef _WIN32
    if(scroll_enabled) {fprintf(stderr,"--scroll-lines is Windows only; use CB_SCROLL_LINES=1 on Mac.\n");return 1;}
#endif
    if(scroll_enabled && listener_mode!=1) {print_usage(argv[0]);return 1;}
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

    if (keyboard_enabled && cb_keyboard_init(redo_shift_z) != 0) {
        fprintf(stderr, "Keyboard mapping initialization failed (macOS only).\n");
        cb_platform_close();
        cb_net_cleanup();
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);


    cb_socket listener = CB_INVALID;
    int exit_code = 0;
    if(scroll_enabled && cb_scroll_init()!=0) {
        fprintf(stderr,"Cannot start scroll setting guard; no setting changed.\n");
        exit_code=1;goto cleanup;
    }

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
    cb_scroll_close();
    cb_net_close(listener);
    cb_keyboard_close();
    cb_platform_close();
    cb_net_cleanup();

    puts("Clipboard Bridge stopped.");
    return exit_code;
}
