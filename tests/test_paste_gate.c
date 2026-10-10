/* 模拟网络和剪贴板，不访问系统剪贴板、不发送实际按键。 */
#include <assert.h>
#define main cb_application_main
#define cb_net_wait test_wait
#define cb_net_read test_read
#define cb_net_write test_write
#define cb_net_now test_now
#include "../src/main.c"
#undef main
#undef cb_net_wait
#undef cb_net_read
#undef cb_net_write
#undef cb_net_now

enum { NATIVE, TEXT_ACK, BAD_ACK, DISCONNECT, ALREADY_PENDING, NEWER_QUEUED, INVALID_NEWER_QUEUED };
static int mode, iteration, completed, requested, active, sent, writing_body;
static uint32_t acked, last_sequence;
static cb_header writing_header;
int test_wait(cb_socket s, int w, unsigned t) {
    (void)s; (void)w; (void)t;
    ++iteration;
    if (mode == NATIVE) return iteration == 1 ? 0 : -1;
    if (mode == TEXT_ACK || mode == BAD_ACK) return iteration == 1 ? 0 : iteration == 2 ? 1 : -1;
    if (mode == DISCONNECT) return iteration == 1 ? 0 : -1;
    if(mode==INVALID_NEWER_QUEUED) return iteration<=3?0:iteration==4?1:-1;
    if (iteration <= 2) return 0;
    if (iteration == 3 || (mode == NEWER_QUEUED && iteration == 4)) return 1;
    return -1;
}
int test_read(cb_socket s, void *out, size_t n, unsigned t) {
    (void)s; (void)t;
    assert(n == CB_HEADER_SIZE && sent && !completed);
    uint32_t seq = mode == BAD_ACK ? last_sequence + 1 : last_sequence;
    cb_header h = {CB_ACK, seq, 0};
    cb_encode_header(out, &h); acked = seq; return 0;
}
int test_write(cb_socket s, const void *data, size_t n, unsigned t) {
    (void)s; (void)t;
    assert(mode != NATIVE && !completed); // 不得把旧 Mac 文本覆盖到 Windows 文件剪贴板。
    if (!writing_body) {
        assert(n == CB_HEADER_SIZE && cb_decode_header(data, &writing_header) == 0);
        assert(writing_header.type == CB_TEXT && writing_header.length == 5);
        last_sequence = writing_header.sequence;
        writing_body = 1;
    } else {
        assert(n == 5 && memcmp(data, sent == 0 ? "hello" : "world", 5) == 0);
        ++sent; writing_body = 0;
    }
    return 0;
}
uint64_t test_now(void) { return (uint64_t)iteration * 100; }
int cb_platform_init(void) { return 0; }
void cb_platform_close(void) { }
void cb_platform_reset(void) { }
int cb_platform_poll(char **text, size_t *n) {
    *text = NULL; *n = 0;
    if(mode==INVALID_NEWER_QUEUED && iteration==3) return -1;
    if (mode == NATIVE) return 0; // Mac 没有新复制：旧文字不应重发。
    if (iteration == 1 || ((mode == NEWER_QUEUED || mode==INVALID_NEWER_QUEUED) && iteration == 2)) {
        *text = malloc(6); assert(*text);
        memcpy(*text, iteration == 1 ? "hello" : "world", 6); *n = 5;
        return 1;
    }
    return 0;
}
int cb_platform_set(const char *text, size_t n) { (void)text; (void)n; return 0; }
int cb_platform_random(unsigned char *out, size_t n) { memset(out, 0, n); return 0; }
unsigned cb_keyboard_scroll_lines(void) { return 0; }
int cb_keyboard_init(int m) { (void)m; return 0; }
void cb_keyboard_close(void) { }
void cb_keyboard_connected(int value) { active = value; }
int cb_keyboard_take_paste(void) {
    if (requested || ((mode == ALREADY_PENDING || mode == NEWER_QUEUED || mode==INVALID_NEWER_QUEUED) && iteration < 2)) return 0;
    assert(active); requested = 1;
    return 1; // 请求本身不读取 Mac 的旧内容。
}
void cb_keyboard_finish_paste(int success) {
    if(mode==INVALID_NEWER_QUEUED) {assert(active && !success);return;}
    assert(active && success);
    if (mode == NATIVE) assert(sent == 0);
    else assert((mode == TEXT_ACK || mode == ALREADY_PENDING || mode == NEWER_QUEUED) && sent && acked == last_sequence);
    if (mode == NEWER_QUEUED) assert(sent == 2 && acked == 2);
    ++completed;
}
int main(void) {
    for (mode = NATIVE; mode <= INVALID_NEWER_QUEUED; ++mode) {
        iteration = completed = requested = active = sent = writing_body = 0;
        acked = last_sequence = 0;
        run_session(123);
        assert(!active);
        assert(sent == (mode == NATIVE ? 0 : mode == NEWER_QUEUED ? 2 : 1));
        assert(completed == (mode == BAD_ACK || mode == DISCONNECT || mode==INVALID_NEWER_QUEUED ? 0 : 1));
    }
    puts("Native paste and pending text ACK tests passed");
}

int cb_platform_poll_content(cb_content *c) {
    memset(c,0,sizeof(*c));return cb_platform_poll(&c->text,&c->text_length);
}
int cb_platform_set_content(const cb_content *c) {return cb_platform_set(c->text,c->text_length);}

int cb_keyboard_caps_enabled(void) {return 0;}
void cb_keyboard_caps_ready(int ready) {(void)ready;}
unsigned cb_keyboard_caps_take(void) {return 0;}

void cb_platform_cleanup_cache(void) {}
