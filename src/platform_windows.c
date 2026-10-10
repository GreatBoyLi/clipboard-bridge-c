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

#include "platform.h"
#include "protocol.h"

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

    WNDCLASSW window_class;
    memset(&window_class, 0, sizeof(window_class));

    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = GetModuleHandleW(NULL);
    window_class.lpszClassName = WINDOW_CLASS_NAME;

    if (RegisterClassW(&window_class) != 0) {
        class_registered = 1;
    } else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        owner_thread = 0;
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

int cb_platform_poll(char **text, size_t *length)
{
    if (text == NULL || length == NULL) {
        return -1;
    }

    *text = NULL;
    *length = 0;

    if (!ready_on_owner_thread()) {
        return -1;
    }

    pump_messages();

    DWORD current = GetClipboardSequenceNumber();

    /*
     * 通知用于及时发现变化，版本号也作为兜底。
     * 未变化时不打开剪贴板，不进行编码转换。
     */
    if (!change_notified && current == last_sequence) {
        return 0;
    }

    if (current == last_sequence) {
        change_notified = 0;
        return 0;
    }

    if (open_clipboard_with_retry() != 0) {
        /* 不更新版本，下一轮继续尝试读取。 */
        return -1;
    }

    /*
     * 打开后重新记录版本，避免等待期间其他应用修改内容。
     * 此后系统剪贴板处于本进程的打开状态。
     */
    current = GetClipboardSequenceNumber();

    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        /*
         * 图片、文件等格式当前不处理。
         * 记录版本，避免对同一内容反复读取。
         */
        last_sequence = current;
        change_notified = 0;
        CloseClipboard();
        return 0;
    }

    /*
     * GetClipboardData 返回系统管理的内存。
     * 本程序只能读取，不能 GlobalFree 这个句柄。
     */
    HGLOBAL handle = (HGLOBAL)GetClipboardData(CF_UNICODETEXT);

    if (handle == NULL) {
        CloseClipboard();
        return -1;
    }

    /*
     * 某些应用延迟提供剪贴板内容，GetClipboardData
     * 可能触发实际生成，因此再次记录版本。
     */
    current = GetClipboardSequenceNumber();

    SIZE_T capacity = GlobalSize(handle);
    const wchar_t *wide_text = GlobalLock(handle);

    if (wide_text == NULL) {
        CloseClipboard();
        return -1;
    }

    /*
     * 在系统提供的缓冲区范围内寻找结束符，
     * 不直接使用 wcslen，避免异常数据导致越界读取。
     */
    size_t max_units = (size_t)(capacity / sizeof(wchar_t));
    size_t units = 0;

    while (units < max_units && wide_text[units] != L'\0') {
        ++units;
    }

    if (units == max_units || units > CB_MAX_TEXT) {
        GlobalUnlock(handle);
        last_sequence = current;
        change_notified = 0;
        CloseClipboard();
        return -1;
    }

    int utf8_length = 0;

    if (units != 0) {
        /*
         * 先计算输出长度。
         * WC_ERR_INVALID_CHARS 拒绝不合法的 UTF-16，
         * 不把异常输入静默替换成其他字符。
         */
        utf8_length = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide_text,
            (int)units,
            NULL,
            0,
            NULL,
            NULL
        );

        if (utf8_length <= 0 ||
            (unsigned)utf8_length > CB_MAX_TEXT) {
            GlobalUnlock(handle);
            last_sequence = current;
            change_notified = 0;
            CloseClipboard();
            return -1;
        }
    }

    char *buffer = malloc((size_t)utf8_length + 1);

    if (buffer == NULL) {
        GlobalUnlock(handle);
        CloseClipboard();

        /* 内存不足时允许下一轮重试。 */
        return -1;
    }

    if (units != 0) {
        int converted = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide_text,
            (int)units,
            buffer,
            utf8_length,
            NULL,
            NULL
        );

        if (converted != utf8_length) {
            free(buffer);
            GlobalUnlock(handle);
            CloseClipboard();
            return -1;
        }
    }

    buffer[utf8_length] = '\0';

    GlobalUnlock(handle);

    /*
     * 保存读取时的版本，而不是关闭后的最新版本。
     * 如果其他应用随后复制了新内容，下一轮仍能检测到。
     */
    last_sequence = current;
    change_notified = 0;
    CloseClipboard();

    *text = buffer;
    *length = (size_t)utf8_length;

    return 1;
}

int cb_platform_set(const char *text, size_t length)
{
    if (!ready_on_owner_thread() ||
        text == NULL ||
        length > CB_MAX_TEXT ||
        !cb_valid_utf8((const unsigned char *)text, length)) {
        return -1;
    }

    int units = 0;

    if (length != 0) {
        /* 先计算 UTF-16 所需长度，不包含末尾结束符。 */
        units = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text,
            (int)length,
            NULL,
            0
        );

        if (units <= 0) {
            return -1;
        }
    }

    /*
     * SetClipboardData 要求使用可移动的全局内存。
     * 成功交给系统后，本程序不再拥有这块内存。
     */
    SIZE_T bytes = ((SIZE_T)units + 1) * sizeof(wchar_t);

    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);

    if (handle == NULL) {
        return -1;
    }

    wchar_t *wide_text = GlobalLock(handle);

    if (wide_text == NULL) {
        GlobalFree(handle);
        return -1;
    }

    if (length != 0) {
        int converted = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text,
            (int)length,
            wide_text,
            units
        );

        if (converted != units) {
            GlobalUnlock(handle);
            GlobalFree(handle);
            return -1;
        }
    }

    wide_text[units] = L'\0';
    GlobalUnlock(handle);

    /*
     * 转换和内存准备完成后再打开、清理剪贴板，
     * 尽量缩短占用时间。
     */
    if (open_clipboard_with_retry() != 0) {
        GlobalFree(handle);
        return -1;
    }

    if (!EmptyClipboard()) {
        CloseClipboard();
        GlobalFree(handle);
        return -1;
    }

    if (SetClipboardData(CF_UNICODETEXT, handle) == NULL) {
        CloseClipboard();

        /* 设置失败，内存所有权仍属于本程序。 */
        GlobalFree(handle);
        return -1;
    }

    /*
     * 设置成功后内存属于系统，不再 GlobalFree。
     * 在关闭剪贴板前记录版本，抑制自身写入的回传。
     */
    last_sequence = GetClipboardSequenceNumber();
    change_notified = 0;

    CloseClipboard();
    return 0;
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
