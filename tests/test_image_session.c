/* 真实本地双向套接字 + 模拟剪贴板，验证同时大图片传输不死锁。 */
#include <assert.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "content.h"
#include "image_fixture.h"
#include "net.h"
int observed_read(cb_socket,void *,size_t,unsigned);
#define main cb_application_main
#define cb_net_read observed_read
#include "../src/main.c"
#undef main
#undef cb_net_read
static int emitted,applied,confirmed,pasted,invalid_case;
int observed_read(cb_socket s,void *out,size_t n,unsigned timeout) {
    int r=cb_net_read(s,out,n,timeout);
    if(r==0 && n==CB_HEADER_SIZE) {
        cb_header h;
        if(cb_decode_header(out,&h)==0 && h.type==CB_ACK) confirmed=1;
    }
    return r;
}
int cb_platform_init(void) {return 0;}
void cb_platform_close(void) {}
void cb_platform_reset(void) {}
int cb_platform_random(unsigned char *p,size_t n) {memset(p,0,n);return 0;}
int cb_platform_poll_content(cb_content *c) {
    memset(c,0,sizeof(*c));if(emitted) return 0;emitted=1;
    c->text=malloc(CB_MAX_TEXT+1);assert(c->text);
    memset(c->text,'a',CB_MAX_TEXT);c->text[CB_MAX_TEXT]=0;c->text_length=CB_MAX_TEXT;
    c->png=malloc(sizeof(cb_test_png));assert(c->png);memcpy(c->png,cb_test_png,sizeof(cb_test_png));c->png_length=sizeof(cb_test_png);
    return 1;
}
int cb_platform_set_content(const cb_content *c) {
    assert(c->text_length==CB_MAX_TEXT && c->png_length==sizeof(cb_test_png));
    assert(!memcmp(c->png,cb_test_png,sizeof(cb_test_png)) && c->text[0]=='a' && c->text[CB_MAX_TEXT-1]=='a');
    ++applied;return 0;
}
int cb_platform_poll(char **p,size_t *n) {(void)p;(void)n;assert(0);return 0;}
int cb_platform_set(const char *p,size_t n) {(void)p;(void)n;assert(0);return 0;}
int cb_keyboard_init(int m) {(void)m;return 0;}
void cb_keyboard_close(void) {}
void cb_keyboard_connected(int v) {(void)v;}
unsigned cb_keyboard_scroll_lines(void) {return 0;}
int cb_keyboard_take_paste(void) {if(invalid_case) return 0;if(applied && confirmed && pasted) stop_requested=1;return !pasted;}
void cb_keyboard_finish_paste(int ok) {assert(ok && confirmed);pasted=1;}
int main(void) {
    int sockets[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    int size=32768;assert(setsockopt(sockets[0],SOL_SOCKET,SO_SNDBUF,&size,sizeof(size))==0);
    assert(setsockopt(sockets[1],SOL_SOCKET,SO_SNDBUF,&size,sizeof(size))==0);
    pid_t child=fork();assert(child>=0);
    alarm(15);
    if(!child) {close(sockets[0]);run_session(sockets[1]);assert(applied==1 && confirmed && pasted);close(sockets[1]);_exit(0);}
    close(sockets[1]);run_session(sockets[0]);assert(applied==1 && confirmed && pasted);close(sockets[0]);
    int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    /* 不完整、错误序号、超限和坏 CRC 一律不能写入，更不能确认成功。 */
    for(int mode=0;mode<6;mode++) {
        assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
        child=fork();assert(child>=0);
        if(!child) {
            close(sockets[0]);alarm(5);stop_requested=0;emitted=1;
            applied=confirmed=pasted=0;invalid_case=1;
            run_session(sockets[1]);assert(!applied && !confirmed && !pasted);
            close(sockets[1]);_exit(0);
        }
        close(sockets[1]);
        cb_content image={NULL,0,(unsigned char *)cb_test_png,sizeof(cb_test_png)};
        unsigned char *body=NULL;size_t length=0;
        assert(cb_content_pack(&image,&body,&length)==0);
        if(mode==2) {body[8]=2;} // PNG 声明长度远超 20 MiB。
        assert(send_frame(sockets[0],CB_IMAGE_BEGIN,1,body,CB_CONTENT_META)==0);
        if(mode!=2) {
            if(mode==4) body[20]^=1; // PNG 内容及 CRC 不一致。
            if(mode==0) { // 中途断线：只到达半个格式体。
                assert(send_frame(sockets[0],CB_IMAGE_CHUNK,1,body+12,20)==0);
            } else if(mode==1) {
                assert(send_frame(sockets[0],CB_IMAGE_CHUNK,2,body+12,(uint32_t)(length-12))==0);
            } else if(mode==3) {
                unsigned char extra[sizeof(cb_test_png)+1];memcpy(extra,cb_test_png,sizeof(cb_test_png));extra[sizeof(cb_test_png)]=0;
                assert(send_frame(sockets[0],CB_IMAGE_CHUNK,1,extra,sizeof(extra))==0);
            } else if(mode==5) {
                assert(send_frame(sockets[0],CB_TEXT,2,"new",3)==0); // 不能在半张图片内插入新复制。
            } else {
                assert(send_frame(sockets[0],CB_IMAGE_CHUNK,1,body+12,(uint32_t)(length-12))==0);
            }
        }
        free(body);close(sockets[0]);
        assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    }
    puts("Simultaneous images, paste ACK and interrupted/malformed transfer tests passed");
}

int cb_keyboard_caps_enabled(void) {return 0;}
void cb_keyboard_caps_ready(int ready) {(void)ready;}
unsigned cb_keyboard_caps_take(void) {return 0;}
