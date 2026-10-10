/* 守护进程独占临时系统设置。主进程只写共享请求，不在网络线程广播设置。
 * 父进程退出/被终止时，守护进程通过真实进程句柄获知并恢复。
 * 请求超过 1.2 秒未续租也恢复，避免网络阻塞/断线后一直保留加速值。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "scroll_control.h"
#include "scroll_lines.h"
#include "scroll_restore.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct { volatile LONG requested, pulse, status; } shared_request;
static HANDLE mapping, guard, stop_event;
static shared_request *shared;

/* 不用 SPIF_UPDATEINIFILE：只改当前会话，绝不把临时值写入用户配置。
 * 广播变化让缓存滚动行数的应用重新读取设置。 */
static int set_lines(unsigned lines,void *context) {
    (void)context;
    return SystemParametersInfoW(SPI_SETWHEELSCROLLLINES,lines,NULL,SPIF_SENDCHANGE) ? 0 : -1;
}
static HANDLE parse_handle(const char *text) {
    char *end=NULL;
    unsigned long long value=strtoull(text,&end,10);
    if(!*text || !end || *end || !value) return NULL;
    return (HANDLE)(uintptr_t)value;
}
int cb_scroll_guard_entry(int argc,char **argv) {
    if(argc<2 || strcmp(argv[1],"--internal-scroll-guard")!=0) return -1;
    if(argc!=6) return 1;
    HANDLE parent=parse_handle(argv[2]), map=parse_handle(argv[3]);
    HANDLE stop=parse_handle(argv[4]), ready=parse_handle(argv[5]);
    if(!parent || !map || !stop || !ready) return 1;
    shared_request *request=MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(*request));
    if(!request) return 1;
    /* 不允许两个程序互相覆盖基线。命名互斥量按 Windows 登录会话隔离。 */
    HANDLE owner=CreateMutexW(NULL,FALSE,L"Local\\ClipboardBridgeScrollLines-v1");
    DWORD ownership=owner ? WaitForSingleObject(owner,0) : WAIT_FAILED;
    UINT original=0;
    int ok=(ownership==WAIT_OBJECT_0 || ownership==WAIT_ABANDONED) &&
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&original,0);
    InterlockedExchange(&request->status,ok ? 1 : -1);
    SetEvent(ready);
    int failed=0;
    cb_scroll_owner state={original,original};
    cb_lines_lease lease={0};
    LONG pulse=0;
    HANDLE waits[2]={parent,stop};
    while(ok) {
        DWORD result=WaitForMultipleObjects(2,waits,FALSE,50);
        if(result!=WAIT_TIMEOUT) break;
        LONG next=InterlockedCompareExchange(&request->pulse,0,0);
        if(next!=pulse) {
            pulse=next;
            unsigned lines=(unsigned)InterlockedCompareExchange(&request->requested,0,0);
            cb_lines_renew(&lease,lines,(uint64_t)GetTickCount64());
        }
        unsigned desired=cb_lines_desired(&lease,(uint64_t)GetTickCount64());
        if(cb_scroll_apply(&state,desired,set_lines,NULL)!=0) {failed=1;break;}
    }
    /* 失败也尝试恢复；失败状态交给父进程报告，不假装修改成功。 */
    if(cb_scroll_apply(&state,0,set_lines,NULL)!=0) failed=1;
    if(failed) InterlockedExchange(&request->status,-1);
    if(ownership==WAIT_OBJECT_0 || ownership==WAIT_ABANDONED) ReleaseMutex(owner);
    if(owner) CloseHandle(owner);
    UnmapViewOfFile(request);
    CloseHandle(parent);CloseHandle(map);CloseHandle(stop);CloseHandle(ready);
    return ok && !failed ? 0 : 1;
}
int cb_scroll_init(void) {
    if(shared) return 0;
    SECURITY_ATTRIBUTES security={(DWORD)sizeof(security),NULL,TRUE};
    HANDLE parent=NULL,ready=NULL;
    HANDLE inherit[4];
    PROCESS_INFORMATION process={0};
    STARTUPINFOEXW startup={0};
    startup.StartupInfo.cb=(DWORD)sizeof(startup);
    SIZE_T bytes=0;
    wchar_t executable[32768],command[33000];
    int ok=0;
    mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,
        (DWORD)sizeof(shared_request),NULL);
    stop_event=CreateEventW(&security,TRUE,FALSE,NULL);
    ready=CreateEventW(&security,TRUE,FALSE,NULL);
    if(!mapping || !stop_event || !ready ||
       !DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),
           &parent,SYNCHRONIZE,TRUE,0)) goto done;
    shared=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(*shared));
    if(!shared) goto done;
    memset(shared,0,sizeof(*shared));
    DWORD length=GetModuleFileNameW(NULL,executable,32768);
    if(!length || length>=32768) goto done;
    if(swprintf(command,33000,L"\"%ls\" --internal-scroll-guard %llu %llu %llu %llu",
        executable,(unsigned long long)(uintptr_t)parent,(unsigned long long)(uintptr_t)mapping,
        (unsigned long long)(uintptr_t)stop_event,(unsigned long long)(uintptr_t)ready)<0) goto done;
    /* 只继承四个指定句柄，不把配对密钥文件/其他应用资源交给子进程。 */
    InitializeProcThreadAttributeList(NULL,1,0,&bytes);
    startup.lpAttributeList=malloc(bytes);
    if(!startup.lpAttributeList) goto done;
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes)) {
        free(startup.lpAttributeList);startup.lpAttributeList=NULL;goto done;
    }
    inherit[0]=parent;inherit[1]=mapping;inherit[2]=stop_event;inherit[3]=ready;
    if(!UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherit,sizeof(inherit),NULL,NULL)) goto done;
    if(!CreateProcessW(executable,command,NULL,NULL,TRUE,
        CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,NULL,NULL,&startup.StartupInfo,&process)) goto done;
    guard=process.hProcess;CloseHandle(process.hThread);
    if(WaitForSingleObject(ready,3000)==WAIT_OBJECT_0 &&
        InterlockedCompareExchange(&shared->status,0,0)==1) ok=1;
done:
    if(startup.lpAttributeList) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);free(startup.lpAttributeList);
    }
    if(parent) CloseHandle(parent);
    if(ready) CloseHandle(ready);
    if(!ok) {cb_scroll_close();return -1;}
    puts("Windows scroll guard ready: linear 1-5 lines; original setting retained.");
    return 0;
}
int cb_scroll_set(unsigned lines) {
    if(!shared || lines>5 || InterlockedCompareExchange(&shared->status,0,0)!=1 ||
        WaitForSingleObject(guard,0)!=WAIT_TIMEOUT) return -1;
    InterlockedExchange(&shared->requested,(LONG)lines);
    InterlockedIncrement(&shared->pulse);
    return 0;
}
void cb_scroll_close(void) {
    if(stop_event) SetEvent(stop_event);
    if(guard) {
        /* 不强杀守护进程：设置广播可能正在等待应用，它仍负责最终恢复。 */
        if(WaitForSingleObject(guard,3000)!=WAIT_OBJECT_0)
            fprintf(stderr,"Scroll guard is still restoring; it will exit independently.\n");
        else {
            DWORD result=1;
            if(!GetExitCodeProcess(guard,&result) || result)
                fprintf(stderr,"Scroll setting guard failed; check Windows mouse settings.\n");
        }
        CloseHandle(guard);guard=NULL;
    }
    if(shared) {UnmapViewOfFile(shared);shared=NULL;}
    if(mapping) {CloseHandle(mapping);mapping=NULL;}
    if(stop_event) {CloseHandle(stop_event);stop_event=NULL;}
}
