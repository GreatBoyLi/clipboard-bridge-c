/*
 * Windows 原生剪贴板接口。
 *
 * 对外实现 platform.h 定义的 C 接口。
 * 文本在 Windows 内部使用 UTF-16，在网络中使用 UTF-8。
 *
 * 通过隐藏的消息窗口接收剪贴板变化通知，
 * 并使用系统版本号确认内容是否真的变化。
 *
 * 本模块不模拟快捷键、不访问网络、不调用 PowerShell。
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <shlobj.h> /* DROPFILES 的声明不在 shellapi.h 中。 */

#include "platform.h"
#include "protocol.h"
#include "content.h"
#include "files.h"
#include "image_windows.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* 消息窗口不会显示在桌面上，也不抢占焦点。 */
static HWND clipboard_window = NULL;

/* 所有剪贴板操作应在创建窗口的线程执行。 */
static DWORD owner_thread = 0;

/* 最近处理过的剪贴板版本。 */
static DWORD last_sequence = 0;

/* 是否收到新的剪贴板通知。 */
static int change_notified = 0;

/* 记录窗口类是否由本模块注册，以便退出时释放。 */
static int class_registered = 0;
static UINT png_format = 0;

static const wchar_t WINDOW_CLASS_NAME[] =
    L"ClipboardBridgeMessageWindow";

/*
 * 窗口回调只记录变化，不在回调中读取、转换或发送数据。
 * 实际处理由主循环调用 cb_platform_poll 完成。
 */
static LRESULT CALLBACK window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
)
{
    if (message == WM_CLIPBOARDUPDATE) {
        change_notified = 1;
        return 0;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

/* 检查初始化状态和线程。 */
static int ready_on_owner_thread(void)
{
    return clipboard_window != NULL &&
           GetCurrentThreadId() == owner_thread;
}

/*
 * 处理隐藏窗口收到的消息。
 * 网络主循环会定期调用平台接口，因此无需单独创建线程。
 */
static void pump_messages(void)
{
    MSG message;

    while (PeekMessageW(
            &message, clipboard_window, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

/*
 * 系统剪贴板一次只能被一个应用打开。
 * 如果其他应用短暂占用，最多重试 10 次。
 *
 * 总等待约 100 毫秒，避免长期阻塞网络主循环。
 */
static int open_clipboard_with_retry(void)
{
    for (unsigned attempt = 0; attempt < 10; ++attempt) {
        if (OpenClipboard(clipboard_window)) {
            return 0;
        }

        Sleep(10);
    }

    return -1;
}

int cb_platform_init(void)
{
    if (clipboard_window != NULL) {
        return ready_on_owner_thread() ? 0 : -1;
    }

    owner_thread = GetCurrentThreadId();
    if (cb_windows_image_init() != 0) { owner_thread = 0; return -1; }
    png_format = RegisterClipboardFormatW(L"PNG");
    if (png_format == 0) { cb_platform_close(); return -1; }

    WNDCLASSW window_class;
    memset(&window_class, 0, sizeof(window_class));

    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = GetModuleHandleW(NULL);
    window_class.lpszClassName = WINDOW_CLASS_NAME;

    if (RegisterClassW(&window_class) != 0) {
        class_registered = 1;
    } else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        cb_platform_close();
        return -1;
    }

    /*
     * HWND_MESSAGE 创建仅处理消息的窗口：
     * 没有可见界面，不出现在任务栏，不抢占输入焦点。
     */
    clipboard_window = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        L"Clipboard Bridge",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        NULL,
        GetModuleHandleW(NULL),
        NULL
    );

    if (clipboard_window == NULL) {
        cb_platform_close();
        return -1;
    }

    if (!AddClipboardFormatListener(clipboard_window)) {
        cb_platform_close();
        return -1;
    }

    /* 仅记录基线，不同步启动前已有的内容。 */
    last_sequence = GetClipboardSequenceNumber();
    change_notified = 0;

    return 0;
}

void cb_platform_close(void)
{
    /* COM 和隐藏窗口必须由初始化线程释放。 */
    if (owner_thread != 0 && GetCurrentThreadId() != owner_thread) return;
    if (clipboard_window != NULL) {
        RemoveClipboardFormatListener(clipboard_window);
        DestroyWindow(clipboard_window);
        clipboard_window = NULL;
    }

    if (class_registered) {
        UnregisterClassW(
            WINDOW_CLASS_NAME,
            GetModuleHandleW(NULL)
        );
        class_registered = 0;
    }

    cb_windows_image_close();
    png_format = 0;
    owner_thread = 0;
    last_sequence = 0;
    change_notified = 0;
}

void cb_platform_reset(void)
{
    if (!ready_on_owner_thread()) {
        return;
    }

    pump_messages();

    /* 网络重连后不补发剪贴板中的旧内容。 */
    last_sequence = GetClipboardSequenceNumber();
    change_notified = 0;
}

/* 文本和图像读取均由调用方在同一次 OpenClipboard 内完成。 */
static int read_text(char **text, size_t *length)
{
    HGLOBAL handle;
    const wchar_t *wide;
    size_t units = 0, capacity;
    int bytes = 0;
    char *buffer;
    *text = NULL; *length = 0;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return 0;
    handle = (HGLOBAL)GetClipboardData(CF_UNICODETEXT);
    if (!handle) return -1;
    capacity = (size_t)GlobalSize(handle) / sizeof(wchar_t);
    wide = GlobalLock(handle);
    if (!wide) return -1;
    while (units < capacity && units <= CB_MAX_TEXT && wide[units] != L'\0') ++units;
    if (units == capacity || units > CB_MAX_TEXT || units > INT_MAX) {
        GlobalUnlock(handle); return -1;
    }
    if (units) {
        bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            wide, (int)units, NULL, 0, NULL, NULL);
        if (bytes <= 0 || (unsigned)bytes > CB_MAX_TEXT) {
            GlobalUnlock(handle); return -1;
        }
    }
    buffer = malloc((size_t)bytes + 1u);
    if (!buffer) { GlobalUnlock(handle); return -1; }
    if (units && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            wide, (int)units, buffer, bytes, NULL, NULL) != bytes) {
        free(buffer); GlobalUnlock(handle); return -1;
    }
    buffer[bytes] = '\0';
    GlobalUnlock(handle);
    *text = buffer; *length = (size_t)bytes;
    return 1;
}

/* CF_HDROP 中是原生 UTF-16 路径；保留整个文件列表，不复制路径文本。 */
static int read_files(cb_content *content)
{
    if(!IsClipboardFormatAvailable(CF_HDROP)) return 0;
    HDROP drop=(HDROP)GetClipboardData(CF_HDROP);if(!drop) return -1;
    UINT count=DragQueryFileW(drop,0xffffffffu,NULL,0);
    if(!count || count>CB_FILE_COUNT) return -1;
    content->files=calloc(count,sizeof(char *));if(!content->files) return -1;
    content->file_count=count;
    for(UINT i=0;i<count;i++) {
        UINT n=DragQueryFileW(drop,i,NULL,0);if(!n || n>32767) return -1;
        wchar_t *wide=malloc(((size_t)n+1)*sizeof(*wide));if(!wide) return -1;
        if(DragQueryFileW(drop,i,wide,n+1)!=n) {free(wide);return -1;}
        int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,-1,NULL,0,NULL,NULL);
        content->files[i]=bytes>0?malloc((size_t)bytes):NULL;
        if(!content->files[i] || !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,-1,
            content->files[i],bytes,NULL,NULL)) {free(wide);return -1;}
        free(wide);
    }
    return 1;
}
static HGLOBAL prepare_files(const cb_content *content)
{
    if(!content->files || !content->file_count || content->file_count>CB_FILE_COUNT || content->text || content->png) return NULL;
    size_t units=1;int lengths[CB_FILE_COUNT];
    for(unsigned i=0;i<content->file_count;i++) {
        if(!content->files[i]) return NULL;
        lengths[i]=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,content->files[i],-1,NULL,0);
        if(lengths[i]<=1 || lengths[i]>32768) return NULL;
        units+=(size_t)lengths[i];
    }
    HGLOBAL handle=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(DROPFILES)+units*sizeof(wchar_t));
    if(!handle) return NULL;
    DROPFILES *drop=GlobalLock(handle);if(!drop) {GlobalFree(handle);return NULL;}
    drop->pFiles=(DWORD)sizeof(DROPFILES);drop->fWide=TRUE;
    wchar_t *paths=(wchar_t *)(drop+1);
    for(unsigned i=0;i<content->file_count;i++) {
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,content->files[i],-1,paths,lengths[i])) {
            GlobalUnlock(handle);GlobalFree(handle);return NULL;
        }
        paths+=lengths[i];
    }
    *paths=0;GlobalUnlock(handle);return handle;
}
int cb_platform_poll_content(cb_content *content)
{
    DWORD current;
    int image_result, text_result;
    if (!content) return -1;
    memset(content, 0, sizeof(*content));
    if (!ready_on_owner_thread()) return -1;
    pump_messages();
    current = GetClipboardSequenceNumber();
    if (current == last_sequence) { change_notified = 0; return 0; }
    if (open_clipboard_with_retry() != 0) return -1;
    /* 系统可能按需从 DIBV5 派生其它位图格式并改变版本。
     * 在剪贴板锁内检查所有者：仍由本窗口拥有的内容都是远端写入，不能回传。 */
    if (GetClipboardOwner() == clipboard_window) {
        last_sequence = GetClipboardSequenceNumber();
        change_notified = 0;
        CloseClipboard();
        return 0;
    }
    int file_result=read_files(content);
    if(file_result!=0) {
        last_sequence=GetClipboardSequenceNumber();change_notified=0;CloseClipboard();
        if(file_result<0) cb_content_free(content);
        return file_result;
    }
    image_result = cb_windows_read_image(png_format,
        &content->png, &content->png_length);
    text_result = image_result < 0 ? -1 : read_text(
        &content->text, &content->text_length);
    /* 延迟格式生成会更新版本，在全部读取完成后保存同一快照版本。 */
    current = GetClipboardSequenceNumber();
    CloseClipboard();
    last_sequence = current;
    change_notified = 0;
    if (image_result < 0 || text_result < 0) {
        cb_content_free(content); return -1;
    }
    return image_result > 0 || text_result > 0 ? 1 : 0;
}

int cb_platform_poll(char **text, size_t *length)
{
    cb_content content;
    int result;
    if (!text || !length) return -1;
    *text = NULL; *length = 0;
    result = cb_platform_poll_content(&content);
    if (result > 0 && content.text) {
        *text = content.text; *length = content.text_length;
        content.text = NULL;
    } else if (result > 0) result = 0;
    cb_content_free(&content);
    return result;
}

static HGLOBAL prepare_text(const char *text, size_t length)
{
    int units = 0;
    HGLOBAL handle;
    wchar_t *wide;
    if (length) {
        units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            text, (int)length, NULL, 0);
        if (units <= 0) return NULL;
    }
    handle = GlobalAlloc(GMEM_MOVEABLE,
        ((SIZE_T)units + 1u) * sizeof(wchar_t));
    if (!handle) return NULL;
    wide = GlobalLock(handle);
    if (!wide) { GlobalFree(handle); return NULL; }
    if (length && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            text, (int)length, wide, units) != units) {
        GlobalUnlock(handle); GlobalFree(handle); return NULL;
    }
    wide[units] = L'\0'; GlobalUnlock(handle);
    return handle;
}

int cb_platform_set_content(const cb_content *content)
{
    HGLOBAL text_handle = NULL, png_handle = NULL, dib_handle = NULL, file_handle=NULL;
    int result = -1;
    DWORD before_close, after_close;
    if (!ready_on_owner_thread() || !content ||
        (!content->text && !content->png && !content->files) ||
        (!content->text && content->text_length) ||
        (!content->png && content->png_length)) return -1;
    if(content->files) {
        file_handle=prepare_files(content);if(!file_handle) goto done;
    }
    if (content->text) {
        if (content->text_length > CB_MAX_TEXT || content->text_length > INT_MAX ||
            !cb_valid_utf8((const unsigned char *)content->text,
                content->text_length)) return -1;
        text_handle = prepare_text(content->text, content->text_length);
        if (!text_handle) goto done;
    }
    /* 所有解码、分配和转换必须先成功，然后才清空旧剪贴板。 */
    if (content->png && cb_windows_prepare_image(content->png,
            content->png_length, &png_handle, &dib_handle) != 0) goto done;
    if (open_clipboard_with_retry() != 0) goto done;
    if (!EmptyClipboard()) { CloseClipboard(); goto done; }
    if(file_handle) {
        if(!SetClipboardData(CF_HDROP,file_handle)) goto close;
        file_handle=NULL;
        /* 未提供剪切格式，资源管理器默认按复制处理。 */
    }
    if (png_handle) {
        if (!SetClipboardData(png_format, png_handle)) goto close;
        png_handle = NULL;
        if (!SetClipboardData(CF_DIBV5, dib_handle)) goto close;
        dib_handle = NULL;
    }
    if (text_handle) {
        if (!SetClipboardData(CF_UNICODETEXT, text_handle)) goto close;
        text_handle = NULL;
    }
    result = 0;
close:
    before_close = GetClipboardSequenceNumber();
    CloseClipboard();
    /* CloseClipboard 会最终发布格式；只在仍是本窗口所有者时取新版本。 */
    after_close = GetClipboardSequenceNumber();
    last_sequence = GetClipboardOwner() == clipboard_window ?
        after_close : before_close;
    change_notified = 0;
done:
    if (file_handle) GlobalFree(file_handle);
    if (text_handle) GlobalFree(text_handle);
    if (png_handle) GlobalFree(png_handle);
    if (dib_handle) GlobalFree(dib_handle);
    return result;
}

int cb_platform_set(const char *text, size_t length)
{
    cb_content content;
    memset(&content, 0, sizeof(content));
    content.text = (char *)text; content.text_length = length;
    return cb_platform_set_content(&content);
}

int cb_platform_random(unsigned char *out, size_t length)
{
    if (length == 0) {
        return 0;
    }

    if (out == NULL || length > ULONG_MAX) {
        return -1;
    }

    /*
     * 配对密钥使用系统安全随机源，
     * 不依赖时间戳、rand() 或进程编号。
     */
    NTSTATUS status = BCryptGenRandom(
        NULL,
        out,
        (ULONG)length,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );

    return status == 0 ? 0 : -1;
}
