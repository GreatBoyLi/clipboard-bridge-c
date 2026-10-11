#ifndef CB_DESKTOP_CONTROL_H
#define CB_DESKTOP_CONTROL_H
/* 界面通过独立 stdin 管道请求退出；父界面崩溃/退出时 EOF 也触发清理。
 * 仅 --desktop-control 模式启用，普通终端使用不受影响。 */
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
static inline int cb_desktop_stop_requested(void) {
    HANDLE input=GetStdHandle(STD_INPUT_HANDLE);DWORD available=0;
    if(!input || input==INVALID_HANDLE_VALUE || GetFileType(input)!=FILE_TYPE_PIPE) return 1;
    if(!PeekNamedPipe(input,NULL,0,NULL,&available,NULL)) return 1;
    if(!available) return 0;
    char command;DWORD read=0;(void)ReadFile(input,&command,1,&read,NULL);return 1;
}
#else
#include <poll.h>
#include <unistd.h>
static inline int cb_desktop_stop_requested(void) {
    struct pollfd input={STDIN_FILENO,POLLIN,0};
    int ready=poll(&input,1,0);
    if(ready<=0) return 0;
    if(input.revents&(POLLHUP|POLLERR|POLLNVAL)) return 1;
    if(input.revents&POLLIN) {char command;return read(STDIN_FILENO,&command,1)>=0;}
    return 0;
}
#endif
#endif
