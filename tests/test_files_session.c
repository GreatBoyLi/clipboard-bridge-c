#define _DARWIN_C_SOURCE
/* 本地双向套接字，模拟剪贴板；仅在私有测试目录创建和删除文件。 */
#include <assert.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "content.h"
#include "net.h"
int observed_read(cb_socket,void *,size_t,unsigned);
#define main cb_application_main
#define cb_net_read observed_read
#include "../src/main.c"
#undef main
#undef cb_net_read
static int emitted,applied,confirmed,pasted,invalid_case;
static char root[1024],first[1200],empty[1200];
static char *received_paths[2];
int observed_read(cb_socket s,void *out,size_t n,unsigned timeout) {
    int r=cb_net_read(s,out,n,timeout);
    if(r==0 && n==CB_HEADER_SIZE) {cb_header h;if(cb_decode_header(out,&h)==0 && h.type==CB_ACK) confirmed=1;}
    return r;
}
int cb_platform_init(void) {return 0;}
void cb_platform_close(void) {}
void cb_platform_reset(void) {}
int cb_platform_random(unsigned char *p,size_t n) {memset(p,0,n);return 0;}
int cb_platform_poll_content(cb_content *c) {
    memset(c,0,sizeof(*c));if(emitted) return 0;emitted=1;
    c->files=calloc(2,sizeof(char *));assert(c->files);c->file_count=2;
    c->files[0]=strdup(first);c->files[1]=strdup(empty);assert(c->files[0] && c->files[1]);return 1;
}
int cb_platform_set_content(const cb_content *c) {
    assert(!invalid_case && c->files && c->file_count==2 && !c->text && !c->png);
    FILE *f=fopen(c->files[0],"rb");assert(f);
    for(unsigned i=0;i<200000;i++) assert(fgetc(f)==(int)(i%251));
    assert(fgetc(f)==EOF);fclose(f);
    f=fopen(c->files[1],"rb");assert(f && fgetc(f)==EOF);fclose(f);
    for(unsigned i=0;i<2;i++) {received_paths[i]=strdup(c->files[i]);assert(received_paths[i]);}
    ++applied;return 0;
}
int cb_platform_poll(char **p,size_t *n) {(void)p;(void)n;assert(0);return 0;}
int cb_platform_set(const char *p,size_t n) {(void)p;(void)n;assert(0);return 0;}
int cb_keyboard_init(int m) {(void)m;return 0;}
void cb_keyboard_close(void) {}
void cb_keyboard_connected(int v) {(void)v;}
unsigned cb_keyboard_scroll_lines(void) {return 0;}
int cb_keyboard_caps_enabled(void) {return 0;}
void cb_keyboard_caps_ready(int ready) {(void)ready;}
unsigned cb_keyboard_caps_take(void) {return 0;}
int cb_keyboard_take_paste(void) {if(invalid_case) return 0;if(applied && confirmed && pasted) stop_requested=1;return !pasted;}
void cb_keyboard_finish_paste(int ok) {assert(ok && confirmed);pasted=1;}
static void cleanup_received(void) {
    if(!received_paths[0]) return;
    char *directory=strdup(received_paths[0]);assert(directory);*strrchr(directory,'/')=0;
    for(unsigned i=0;i<2;i++) {assert(unlink(received_paths[i])==0);free(received_paths[i]);received_paths[i]=NULL;}
    assert(rmdir(directory)==0);free(directory);
}
static void wait_ok(pid_t child) {int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);}
int main(void) {
    strcpy(root,"/tmp/cb-files-session-XXXXXX");assert(mkdtemp(root));assert(setenv("TMPDIR",root,1)==0);
    snprintf(first,sizeof(first),"%s/中文🙂.bin",root);snprintf(empty,sizeof(empty),"%s/empty",root);
    FILE *f=fopen(first,"wb");assert(f);for(unsigned i=0;i<200000;i++) assert(fputc((int)(i%251),f)!=EOF);assert(fclose(f)==0);
    f=fopen(empty,"wb");assert(f);assert(fclose(f)==0);
    int sockets[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    int size=32768;assert(setsockopt(sockets[0],SOL_SOCKET,SO_SNDBUF,&size,sizeof(size))==0);
    assert(setsockopt(sockets[1],SOL_SOCKET,SO_SNDBUF,&size,sizeof(size))==0);
    pid_t child=fork();assert(child>=0);alarm(15);
    if(!child) {close(sockets[0]);run_session(sockets[1]);assert(applied==1 && confirmed && pasted);cleanup_received();close(sockets[1]);_exit(0);}
    close(sockets[1]);run_session(sockets[0]);assert(applied==1 && confirmed && pasted);cleanup_received();close(sockets[0]);wait_ok(child);
    /* 中断、错误序号、损坏校验均不能修改剪贴板或确认。 */
    for(int mode=0;mode<3;mode++) {
        assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);child=fork();assert(child>=0);
        if(!child) {close(sockets[0]);alarm(5);stop_requested=0;emitted=1;applied=confirmed=pasted=0;invalid_case=1;
            run_session(sockets[1]);assert(!applied && !confirmed && !pasted);close(sockets[1]);_exit(0);}
        close(sockets[1]);cb_file_tx tx={0};unsigned char *meta=NULL,block[CB_FILE_CHUNK];size_t n=0,len=0;
        const char *paths[]={first,empty};assert(cb_file_tx_open(&tx,paths,2,&meta,&len)==0);
        assert(send_frame(sockets[0],CB_FILES_BEGIN,1,meta,(uint32_t)len)==0);free(meta);
        cb_header header;unsigned char *body=NULL;assert(receive_frame(sockets[0],&header,&body)==0 && header.type==CB_FILES_CREDIT);free(body);
        while(cb_file_tx_chunk(&tx,block,&n)>0) {
            assert(send_frame(sockets[0],CB_FILES_CHUNK,mode==1?2:1,block,(uint32_t)n)==0);
            if(mode!=2) break;
            assert(receive_frame(sockets[0],&header,&body)==0 && header.type==CB_FILES_CREDIT);free(body);
        }
        if(mode==2) {assert(cb_file_tx_end(&tx,block,&n)==0);block[0]^=1;assert(send_frame(sockets[0],CB_FILES_END,1,block,(uint32_t)n)==0);}
        cb_file_tx_close(&tx);close(sockets[0]);wait_ok(child);
    }
    DIR *d=opendir(root);assert(d);unsigned entries=0;struct dirent *entry;
    while((entry=readdir(d))) if(strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")) entries++;
    closedir(d);assert(entries==2); // 仅源文件，失败接收的目录已清理。
    unlink(first);unlink(empty);assert(rmdir(root)==0);
    puts("Bidirectional files, paste ACK, interruption and integrity rejection tests passed");
}
