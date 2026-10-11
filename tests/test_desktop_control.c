/* 使用私有匿名管道，验证停止指令和界面退出后的 EOF；不接触剪贴板。 */
#include <assert.h>
#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include "../src/desktop_control.h"
int main(void) {
#ifdef _WIN32
    HANDLE read_end=NULL,write_end=NULL;
    assert(CreatePipe(&read_end,&write_end,NULL,0));
    assert(SetStdHandle(STD_INPUT_HANDLE,read_end));
    assert(!cb_desktop_stop_requested());
    DWORD written=0;assert(WriteFile(write_end,"q",1,&written,NULL) && written==1);
    assert(cb_desktop_stop_requested());CloseHandle(write_end);CloseHandle(read_end);
    assert(CreatePipe(&read_end,&write_end,NULL,0));assert(SetStdHandle(STD_INPUT_HANDLE,read_end));
    CloseHandle(write_end);assert(cb_desktop_stop_requested());CloseHandle(read_end);
#else
    int pair[2];assert(pipe(pair)==0);assert(dup2(pair[0],STDIN_FILENO)>=0);close(pair[0]);
    assert(!cb_desktop_stop_requested());assert(write(pair[1],"q",1)==1);
    assert(cb_desktop_stop_requested());close(pair[1]);
    assert(pipe(pair)==0);assert(dup2(pair[0],STDIN_FILENO)>=0);close(pair[0]);
    close(pair[1]);assert(cb_desktop_stop_requested());
#endif
    puts("Desktop stop and parent-exit tests passed");return 0;
}
