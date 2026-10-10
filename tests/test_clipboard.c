#include "platform.h"
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 原生剪贴板的手动测试工具。
 *
 * 不连接其他设备，不发送剪贴板内容。
 * --watch 不修改剪贴板。
 * --write 会按用户提供的文本覆盖剪贴板。
 */
int main(int argc, char **argv)
{
    if (argc < 2 ||
        (strcmp(argv[1], "--watch") != 0 &&
         strcmp(argv[1], "--write") != 0)) {
        fprintf(stderr,
            "Usage:\n"
            "  test_clipboard --watch\n"
            "  test_clipboard --write \"text\"\n");
        return 1;
    }

    int write_mode = strcmp(argv[1], "--write") == 0;

    if ((!write_mode && argc != 2) ||
        (write_mode && argc != 3)) {
        fprintf(stderr, "Invalid arguments.\n");
        return 1;
    }

    if (cb_platform_init() != 0) {
        fprintf(stderr, "Clipboard initialization failed.\n");
        return 1;
    }

    if (write_mode) {
        const char *value = argv[2];

        if (cb_platform_set(value, strlen(value)) != 0) {
            fprintf(stderr, "Clipboard write failed.\n");
            cb_platform_close();
            return 1;
        }

        char *echo = NULL;
        size_t length = 0;
        int result = cb_platform_poll(&echo, &length);

        /* 无论检查结果如何，都释放可能返回的缓冲区。 */
        free(echo);

        if (result != 0) {
            fprintf(stderr, "Self-write suppression failed.\n");
            cb_platform_close();
            return 1;
        }

        puts("Clipboard write and self-write suppression passed.");
        puts("Paste into a text editor to check the actual text.");

        cb_platform_close();
        return 0;
    }

    puts("Watching new text copies for 30 seconds.");
    puts("Copy text in another application; contents will not be printed.");

    /*
     * 只检测启动后新复制的内容。
     * 每 100 毫秒检查一次版本；未变化时不读取文本。
     */
    uint64_t deadline = cb_net_now() + 30000;

    while (cb_net_now() < deadline) {
        char *text = NULL;
        size_t length = 0;

        int result = cb_platform_poll(&text, &length);

        if (result == 1) {
            printf("New text detected: %zu UTF-8 bytes.\n", length);
            fflush(stdout);
        } else if (result < 0) {
            fprintf(stderr,
                "Clipboard read failed or text was unsupported.\n");
        }

        free(text);
        cb_net_sleep(100);
    }

    cb_platform_close();
    puts("Clipboard watch finished.");
    return 0;
}
