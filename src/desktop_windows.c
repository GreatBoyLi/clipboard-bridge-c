/* Windows 原生托盘入口。凭据独立保存；切换设备必须等待旧进程完成恢复。 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <bcrypt.h>
#include <sddl.h>
#include <strsafe.h>
#include <wchar.h>
#include <string.h>
#include "desktop_pairing.h"

#define MAX_PAIRS 32
#define NAME_CAP 64
#define PATH_CAP 1024
#define WM_TRAY (WM_APP + 1)
#define ID_PAIR 100
#define ID_ADD 200
#define ID_EXPORT 201
#define ID_REMOVE 202
#define ID_SETTINGS 203
#define ID_PAUSE 204
#define ID_QUIT 205
#define ID_EDIT1 300
#define ID_EDIT2 301
#define ID_EDIT3 302

typedef struct { WCHAR name[NAME_CAP]; WCHAR id[33]; } Pair;
typedef struct {
    DWORD magic, version, count, port, scroll;
    int selected;
    WCHAR address[16];
    Pair pairs[MAX_PAIRS];
} Config;
typedef struct { WCHAR title[64], labels[3][64], values[3][NAME_CAP]; int count; } Form;
static Config config;
static WCHAR directory[PATH_CAP], worker[PATH_CAP];
static HWND window;
static HINSTANCE instance;
static HANDLE mutex_handle, process_handle, control_write, output_read;
static PSECURITY_DESCRIPTOR private_descriptor;
static UINT taskbar_created;
static BOOL quitting, paused, stopping, want_start;
static const WCHAR *status = L"未连接";
static char output_line[512];
static size_t output_length;
static BOOL output_overflow;

static void error_box(const WCHAR *message) {
    MessageBoxW(window, message, L"Clipboard Bridge", MB_OK | MB_ICONERROR);
}
static BOOL path_for(WCHAR *path, const WCHAR *leaf) {
    return SUCCEEDED(StringCchPrintfW(path, PATH_CAP, L"%s\\%s", directory, leaf));
}
static BOOL key_path(WCHAR *path, const WCHAR *id) {
    WCHAR leaf[48];
    return SUCCEEDED(StringCchPrintfW(leaf, 48, L"%s.key", id)) && path_for(path, leaf);
}
/* 现有目录也重设受保护的 ACL；不把共享密钥放入注册表。 */
static BOOL secure_directory(void) {
    HANDLE token = NULL;
    DWORD length = 0;
    TOKEN_USER *user = NULL;
    LPWSTR sid = NULL;
    WCHAR sddl[256];
    PSECURITY_DESCRIPTOR descriptor = NULL;
    BOOL ok = FALSE;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) goto done;
    (void)GetTokenInformation(token, TokenUser, NULL, 0, &length);
    if (length == 0 || length > 65536) goto done;
    user = (TOKEN_USER *)HeapAlloc(GetProcessHeap(), 0, length);
    if (!user || !GetTokenInformation(token, TokenUser, user, length, &length)) goto done;
    if (!ConvertSidToStringSidW(user->User.Sid, &sid)) goto done;
    if (FAILED(StringCchPrintfW(sddl, 256, L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;%s)", sid))) goto done;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, NULL)) goto done;
    {
        SECURITY_ATTRIBUTES security = { (DWORD)sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE };
        if (!CreateDirectoryW(directory, &security) && GetLastError() != ERROR_ALREADY_EXISTS) goto done;
    }
    {
        DWORD attributes = GetFileAttributesW(directory);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) goto done;
    }
    ok = SetFileSecurityW(directory, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor);
    if (ok) { private_descriptor = descriptor; descriptor = NULL; }
done:
    if (descriptor) LocalFree(descriptor);
    if (sid) LocalFree(sid);
    if (user) HeapFree(GetProcessHeap(), 0, user);
    if (token) CloseHandle(token);
    return ok;
}
/* 同目录临时文件 + 原子替换，失败时保留旧配置。 */
static BOOL atomic_write(const WCHAR *path, const void *data, DWORD size) {
    WCHAR temporary[PATH_CAP];
    HANDLE file;
    DWORD written = 0;
    BOOL ok;
    SECURITY_ATTRIBUTES security = { (DWORD)sizeof(SECURITY_ATTRIBUTES), private_descriptor, FALSE };
    if (FAILED(StringCchPrintfW(temporary, PATH_CAP, L"%s.tmp", path))) return FALSE;
    file = CreateFileW(temporary, GENERIC_WRITE, 0, &security, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    {
        BY_HANDLE_FILE_INFORMATION info = {0};
        ok = GetFileInformationByHandle(file, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
             SetFileSecurityW(temporary, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, private_descriptor);
    }
    ok = ok && WriteFile(file, data, size, &written, NULL) && written == size && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) ok = MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temporary);
    return ok;
}
static BOOL save_config(void) {
    WCHAR path[PATH_CAP];
    return path_for(path, L"devices.dat") && atomic_write(path, &config, (DWORD)sizeof(config));
}
static BOOL load_key_file(const WCHAR *path, char key[65]) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    char data[68];
    DWORD count = 0;
    BOOL ok;
    BY_HANDLE_FILE_INFORMATION info = {0};
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    ok = GetFileInformationByHandle(file, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
         ReadFile(file, data, (DWORD)sizeof(data), &count, NULL);
    CloseHandle(file);
    if (!ok || !(count == 64 || (count == 65 && data[64] == '\n') ||
                 (count == 66 && data[64] == '\r' && data[65] == '\n'))) {
        SecureZeroMemory(data, sizeof(data)); return FALSE;
    }
    memcpy(key, data, 64); key[64] = 0;
    SecureZeroMemory(data, sizeof(data));
    return cb_desktop_key_valid(key) ? TRUE : FALSE;
}
static BOOL narrow_address(char address[16]) {
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, config.address, -1, address, 16, NULL, NULL) > 0 &&
           cb_desktop_ipv4_valid(address);
}
static BOOL config_valid(void) {
    DWORD i, j;
    char address[16];
    if (config.magic != 0x43425452 || config.version != 1 || config.count > MAX_PAIRS ||
        config.port == 0 || config.port > 65535 || config.scroll > 1 || config.selected < -1 ||
        config.selected >= (int)config.count || config.address[15] != 0 ||
        (config.address[0] && !narrow_address(address))) return FALSE;
    for (i = 0; i < config.count; ++i) {
        const Pair *pair = &config.pairs[i];
        if (!pair->name[0] || pair->name[NAME_CAP - 1] != 0 || pair->id[32] != 0) return FALSE;
        for (j = 0; j < 32; ++j) {
            if (pair->id[j] > 127 || cb_desktop_hex((unsigned char)pair->id[j]) < 0) return FALSE;
        }
        for (j = 0; j < i; ++j) {
            if (wcscmp(pair->id, config.pairs[j].id) == 0 ||
                _wcsicmp(pair->name, config.pairs[j].name) == 0) return FALSE;
        }
    }
    /* 手动替换凭据也不能让两个设备共用一个密钥。 */
    for (i = 0; i < config.count; ++i) {
        WCHAR path[PATH_CAP]; char a[65], b[65];
        if (!key_path(path, config.pairs[i].id) ||
            !SetFileSecurityW(path, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, private_descriptor) ||
            !load_key_file(path, a)) return FALSE;
        for (j = 0; j < i; ++j) {
            if (!key_path(path, config.pairs[j].id) || !load_key_file(path, b)) {
                SecureZeroMemory(a, sizeof(a)); return FALSE;
            }
            if (cb_desktop_key_equal(a, b)) {
                SecureZeroMemory(a, sizeof(a)); SecureZeroMemory(b, sizeof(b)); return FALSE;
            }
            SecureZeroMemory(b, sizeof(b));
        }
        SecureZeroMemory(a, sizeof(a));
    }
    return TRUE;
}
static BOOL load_config(void) {
    WCHAR path[PATH_CAP];
    HANDLE file;
    DWORD count = 0;
    LARGE_INTEGER size = {0};
    ZeroMemory(&config, sizeof(config));
    config.magic = 0x43425452; config.version = 1; config.selected = -1;
    config.port = 47321; config.scroll = 1;
    if (!path_for(path, L"devices.dat")) return FALSE;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    {
        BY_HANDLE_FILE_INFORMATION info = {0};
        BOOL ok = GetFileInformationByHandle(file, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                  SetFileSecurityW(path, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, private_descriptor) &&
                  GetFileSizeEx(file, &size) && size.QuadPart == (LONGLONG)sizeof(config) &&
                  ReadFile(file, &config, (DWORD)sizeof(config), &count, NULL) && count == (DWORD)sizeof(config);
        CloseHandle(file);
        return ok && config_valid();
    }
}

/* 在内存中构建简单对话框，无资源文件，所有文本使用 Unicode。 */
static WORD *put_string(WORD *p, const WCHAR *text) {
    size_t length = wcslen(text) + 1;
    memcpy(p, text, length * sizeof(WCHAR));
    return p + length;
}
static WORD *add_control(WORD *p, DWORD style, short x, short y, short width, short height,
                         WORD id, WORD class_id, const WCHAR *text) {
    DLGITEMTEMPLATE *item;
    p = (WORD *)(((ULONG_PTR)p + 3) & ~(ULONG_PTR)3);
    item = (DLGITEMTEMPLATE *)p;
    item->style = style | WS_CHILD | WS_VISIBLE;
    item->dwExtendedStyle = 0; item->x = x; item->y = y;
    item->cx = width; item->cy = height; item->id = id;
    p = (WORD *)(item + 1); *p++ = 0xffff; *p++ = class_id;
    p = put_string(p, text); *p++ = 0;
    return p;
}
static INT_PTR CALLBACK form_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    Form *form = (Form *)GetWindowLongPtrW(dialog, DWLP_USER);
    int i;
    if (message == WM_INITDIALOG) {
        form = (Form *)lparam;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)form);
        SetWindowTextW(dialog, form->title);
        for (i = 0; i < form->count; ++i) {
            SetDlgItemTextW(dialog, ID_EDIT1 + i, form->values[i]);
            SendDlgItemMessageW(dialog, ID_EDIT1 + i, EM_SETLIMITTEXT, NAME_CAP - 1, 0);
        }
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDOK && form) {
        for (i = 0; i < form->count; ++i)
            GetDlgItemTextW(dialog, ID_EDIT1 + i, form->values[i], NAME_CAP);
        EndDialog(dialog, IDOK); return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL); return TRUE;
    }
    return FALSE;
}
static BOOL show_form(Form *form) {
    /* DWORD 数组保证 DLGTEMPLATE 的对齐。 */
    DWORD storage[2048];
    DLGTEMPLATE *dialog = (DLGTEMPLATE *)storage;
    WORD *p;
    int i;
    ZeroMemory(storage, sizeof(storage));
    dialog->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT;
    dialog->cdit = (WORD)(form->count * 2 + 2);
    dialog->x = 10; dialog->y = 10; dialog->cx = 260; dialog->cy = (short)(form->count * 34 + 42);
    p = (WORD *)(dialog + 1); *p++ = 0; *p++ = 0;
    p = put_string(p, form->title); *p++ = 9; p = put_string(p, L"Segoe UI");
    for (i = 0; i < form->count; ++i) {
        p = add_control(p, 0, 10, (short)(10 + i * 34), 240, 12, (WORD)(400 + i), 0x82, form->labels[i]);
        p = add_control(p, WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 10, (short)(23 + i * 34), 240, 14,
                         (WORD)(ID_EDIT1 + i), 0x81, L"");
    }
    p = add_control(p, WS_TABSTOP | BS_DEFPUSHBUTTON, 130, (short)(form->count * 34 + 17), 55, 14, IDOK, 0x80, L"确定");
    (void)add_control(p, WS_TABSTOP | BS_PUSHBUTTON, 195, (short)(form->count * 34 + 17), 55, 14, IDCANCEL, 0x80, L"取消");
    return DialogBoxIndirectParamW(instance, dialog, window, form_proc, (LPARAM)form) == IDOK;
}
static BOOL choose_key(WCHAR path[PATH_CAP], BOOL save) {
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = (DWORD)sizeof(ofn); ofn.hwndOwner = window;
    ofn.lpstrFilter = L"配对密钥 (*.key)\0*.key\0所有文件\0*.*\0\0";
    ofn.lpstrFile = path; ofn.nMaxFile = PATH_CAP; ofn.lpstrDefExt = L"key";
    ofn.lpstrTitle = save ? L"导出给对应 Mac 的配对密钥" : L"导入现有 Mac 的配对密钥";
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
}
static BOOL same_file(const BY_HANDLE_FILE_INFORMATION *a, const BY_HANDLE_FILE_INFORMATION *b) {
    return a->dwVolumeSerialNumber == b->dwVolumeSerialNumber &&
           a->nFileIndexHigh == b->nFileIndexHigh && a->nFileIndexLow == b->nFileIndexLow;
}
static BOOL write_export(const WCHAR *path, const char key[65]) {
    HANDLE file;
    DWORD written = 0, i;
    BOOL ok = TRUE;
    WCHAR resolved[PATH_CAP], internal[PATH_CAP];
    BY_HANDLE_FILE_INFORMATION target = {0};
    LARGE_INTEGER zero = {0};
    DWORD n = GetFullPathNameW(path, PATH_CAP, resolved, NULL);
    size_t length = wcslen(directory);
    if (!n || n >= PATH_CAP) return FALSE;
    if (_wcsnicmp(resolved, directory, length) == 0 && (resolved[length] == L'\\' || resolved[length] == 0)) return FALSE;
    /* 先打开并检查，绝不在检查链接之前截断文件。 */
    file = CreateFileW(resolved, GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0, NULL, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    ok = GetFileInformationByHandle(file, &target) &&
         !(target.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
    /* 同样拒绝通过短路径或硬链接覆盖内部凭据。 */
    for (i = 0; ok && i <= config.count; ++i) {
        HANDLE owned;
        BY_HANDLE_FILE_INFORMATION info = {0};
        BOOL found = i == config.count ? path_for(internal, L"devices.dat") : key_path(internal, config.pairs[i].id);
        if (!found) { ok = FALSE; break; }
        owned = CreateFileW(internal, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (owned == INVALID_HANDLE_VALUE) {
            /* 打开中的目标若恰好是内部文件，独占共享也会阻止第二次打开。 */
            if (GetLastError() != ERROR_FILE_NOT_FOUND) ok = FALSE;
        } else {
            if (!GetFileInformationByHandle(owned, &info) || same_file(&target, &info)) ok = FALSE;
            CloseHandle(owned);
        }
    }
    ok = ok && SetFilePointerEx(file, zero, NULL, FILE_BEGIN) && SetEndOfFile(file) &&
         WriteFile(file, key, 64, &written, NULL) && written == 64 && FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}
static void update_tray(BOOL add) {
    NOTIFYICONDATAW icon;
    ZeroMemory(&icon, sizeof(icon));
    icon.cbSize = (DWORD)sizeof(icon); icon.hWnd = window; icon.uID = 1;
    icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    icon.uCallbackMessage = WM_TRAY; icon.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    if (config.selected >= 0)
        (void)StringCchPrintfW(icon.szTip, ARRAYSIZE(icon.szTip), L"Clipboard Bridge · %.40s · %s", config.pairs[config.selected].name, status);
    else (void)StringCchPrintfW(icon.szTip, ARRAYSIZE(icon.szTip), L"Clipboard Bridge · %s", status);
    (void)Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &icon);
}
static void request_stop(void) {
    if (process_handle) { stopping = TRUE; status = L"正在恢复并断开…"; }
    else if (paused) status = L"已暂停";
    else if (config.selected < 0) status = L"未选择 Mac";
    else if (!config.address[0]) status = L"请设置本机 Tailscale IPv4";
    /* 关闭写端立即向 worker 发送 EOF；不等待、不强杀。 */
    if (control_write) { CloseHandle(control_write); control_write = NULL; }
    update_tray(FALSE);
}
static void restart_selected(void) {
    want_start = config.selected >= 0 && !paused && !quitting;
    request_stop();
}
static BOOL start_worker(void) {
    WCHAR keyfile[PATH_CAP], command[PATH_CAP * 3];
    HANDLE input_read = NULL, output_write = NULL;
    SECURITY_ATTRIBUTES security = { (DWORD)sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    STARTUPINFOEXW startup;
    PROCESS_INFORMATION process;
    SIZE_T attribute_size = 0;
    HANDLE inherit[2];
    BOOL ok = FALSE;
    char key[65];
    ZeroMemory(&startup, sizeof(startup)); ZeroMemory(&process, sizeof(process));
    if (!config.address[0]) {
        status = L"请设置本机 Tailscale IPv4"; update_tray(FALSE);
        error_box(L"请在连接设置中填写本机 Tailscale IPv4 地址，再点击所选 Mac 重试。"); return FALSE;
    }
    if (config.selected < 0 || !key_path(keyfile, config.pairs[config.selected].id) || !load_key_file(keyfile, key)) goto done;
    SecureZeroMemory(key, sizeof(key));
    if (FAILED(StringCchPrintfW(command, ARRAYSIZE(command),
        L"\"%s\" --listen %s --port %lu --key-file \"%s\" %s --desktop-control",
        worker, config.address, (unsigned long)config.port, keyfile, config.scroll ? L"--scroll-lines" : L""))) goto done;
    if (!CreatePipe(&input_read, &control_write, &security, 0) ||
        !SetHandleInformation(control_write, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&output_read, &output_write, &security, 0) ||
        !SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0)) goto done;
    startup.StartupInfo.cb = (DWORD)sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input_read;
    startup.StartupInfo.hStdOutput = output_write; startup.StartupInfo.hStdError = output_write;
    (void)InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_size);
    startup.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attribute_size);
    if (!startup.lpAttributeList) goto done;
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_size)) {
        HeapFree(GetProcessHeap(), 0, startup.lpAttributeList); startup.lpAttributeList = NULL; goto done;
    }
    inherit[0] = input_read; inherit[1] = output_write;
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                    inherit, sizeof(inherit), NULL, NULL)) goto done;
    ok = CreateProcessW(worker, command, NULL, NULL, TRUE, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                         NULL, NULL, &startup.StartupInfo, &process);
    if (ok) {
        process_handle = process.hProcess; CloseHandle(process.hThread);
        status = L"正在启动…"; stopping = FALSE; output_length = 0; output_overflow = FALSE;
    }
done:
    SecureZeroMemory(key, sizeof(key));
    if (startup.lpAttributeList) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
    }
    if (input_read) CloseHandle(input_read);
    if (output_write) CloseHandle(output_write);
    if (!ok) {
        if (control_write) { CloseHandle(control_write); control_write = NULL; }
        if (output_read) { CloseHandle(output_read); output_read = NULL; }
        status = L"启动失败，请检查配置";
        error_box(L"无法启动同步程序。请确认 clipboard-bridge.exe 在托盘程序旁，配对密钥可读，监听地址属于此电脑。可在菜单中重试。");
    }
    update_tray(FALSE); return ok;
}
/* 只识别固定事件，丢弃原始输出：剪贴板文本和密钥绝不进入 UI 日志。 */
static void event_line(void) {
    if (stopping) return;
    output_line[output_length] = 0;
    if (strncmp(output_line, "Connected and authenticated.", 28) == 0) status = L"已连接";
    else if (strncmp(output_line, "Listening on ", 13) == 0 || strncmp(output_line, "Disconnected.", 13) == 0) status = L"等待所选 Mac";
    else if (strncmp(output_line, "Authentication failed", 21) == 0) status = L"配对验证失败，等待重试";
    else if (strncmp(output_line, "Listener failed", 15) == 0 || strncmp(output_line, "Cannot listen", 13) == 0) status = L"监听失败，请检查地址";
}
static void poll_worker(void) {
    unsigned batch;
    if (output_read) {
        for (batch = 0; batch < 16; ++batch) {
            DWORD available = 0, count = 0, i;
            char bytes[512];
            if (!PeekNamedPipe(output_read, NULL, 0, NULL, &available, NULL) || available == 0) break;
            if (available > (DWORD)sizeof(bytes)) available = (DWORD)sizeof(bytes);
            if (!ReadFile(output_read, bytes, available, &count, NULL) || count == 0) break;
            for (i = 0; i < count; ++i) {
                if (bytes[i] == '\n') {
                    if (!output_overflow) event_line();
                    output_length = 0; output_overflow = FALSE;
                } else if (!output_overflow) {
                    if (output_length + 1 < sizeof(output_line)) output_line[output_length++] = bytes[i];
                    else { output_overflow = TRUE; output_length = 0; }
                }
            }
            SecureZeroMemory(bytes, sizeof(bytes));
        }
    }
    if (process_handle && WaitForSingleObject(process_handle, 0) == WAIT_OBJECT_0) {
        DWORD result = 1;
        (void)GetExitCodeProcess(process_handle, &result);
        CloseHandle(process_handle); process_handle = NULL;
        if (control_write) { CloseHandle(control_write); control_write = NULL; }
        if (output_read) { CloseHandle(output_read); output_read = NULL; }
        if (stopping && result != 0) {
            /* 恢复失败时禁止自动切换；只能由用户从菜单明确重试。 */
            want_start = FALSE; paused = TRUE; stopping = FALSE;
            status = L"停止/恢复失败，请检查设置";
            update_tray(FALSE);
            error_box(L"同步程序停止或恢复设置失败，已暂停同步。请检查系统设置后，再从菜单恢复同步或重新选择 Mac。");
        } else {
            status = stopping ? (paused ? L"已暂停" : L"已停止") :
                (result == 0 ? L"已停止，点击菜单重试" : L"同步程序退出，请检查配置");
            stopping = FALSE;
        }
    }
    if (!process_handle && quitting) { DestroyWindow(window); return; }
    if (!process_handle && want_start) { want_start = FALSE; (void)start_worker(); }
    update_tray(FALSE);
}

static BOOL unique_key(const char key[65]) {
    DWORD i; WCHAR path[PATH_CAP]; char other[65];
    for (i = 0; i < config.count; ++i) {
        if (!key_path(path, config.pairs[i].id) || !load_key_file(path, other)) {
            SecureZeroMemory(other, sizeof(other));
            error_box(L"已有设备的凭据不可读，请修复后再添加设备。"); return FALSE;
        }
        if (cb_desktop_key_equal(key, other)) {
            SecureZeroMemory(other, sizeof(other));
            error_box(L"这个密钥已属于另一台 Mac。每台 Mac 必须使用独立密钥。"); return FALSE;
        }
        SecureZeroMemory(other, sizeof(other));
    }
    return TRUE;
}
static BOOL random_hex(char *text, size_t bytes_count) {
    BYTE bytes[32];
    static const char hex[] = "0123456789abcdef";
    size_t i;
    if (bytes_count > sizeof(bytes) || BCryptGenRandom(NULL, bytes, (ULONG)bytes_count, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return FALSE;
    for (i = 0; i < bytes_count; ++i) { text[i * 2] = hex[bytes[i] >> 4]; text[i * 2 + 1] = hex[bytes[i] & 15]; }
    text[bytes_count * 2] = 0; SecureZeroMemory(bytes, sizeof(bytes)); return TRUE;
}
static void add_pair(void) {
    Form form; Config previous; Pair pair; DWORD i;
    WCHAR path[PATH_CAP], exported[PATH_CAP] = L"";
    char key[65] = {0}, id[33]; int choice;
    if (config.count >= MAX_PAIRS) { error_box(L"最多保存 32 台 Mac。请先移除不用的设备。"); return; }
    ZeroMemory(&form, sizeof(form)); ZeroMemory(&pair, sizeof(pair));
    form.count = 1;
    (void)StringCchCopyW(form.title, 64, L"添加 Mac");
    (void)StringCchCopyW(form.labels[0], 64, L"Mac 名称（例如：工作 MacBook）");
    if (!show_form(&form)) return;
    if (!form.values[0][0]) { error_box(L"请输入 Mac 名称。"); return; }
    for (i = 0; i < config.count; ++i) {
        if (_wcsicmp(form.values[0], config.pairs[i].name) == 0) { error_box(L"此名称已存在，请使用不同名称。"); return; }
    }
    choice = MessageBoxW(window, L"为这台 Mac 生成独立密钥？\n\n是：生成并导出密钥给对应 Mac。\n否：导入已有的 64 位十六进制密钥。", L"配对方式", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) return;
    if (choice == IDYES) {
        if (!random_hex(key, 32) || !unique_key(key)) goto done;
        if (!choose_key(exported, TRUE)) goto done;
        if (!write_export(exported, key)) {
            error_box(L"密钥导出未完成，设备未添加。请另选保存位置。"); goto done;
        }
    } else {
        if (!choose_key(exported, FALSE)) goto done;
        if (!load_key_file(exported, key)) { error_box(L"密钥格式错误。需要 64 个十六进制字符，可带末尾换行。"); goto done; }
        if (!unique_key(key)) goto done;
    }
    if (!random_hex(id, 16)) { error_box(L"系统随机数不可用，设备未添加。"); goto done; }
    for (i = 0; i < 32; ++i) pair.id[i] = (WCHAR)(unsigned char)id[i];
    (void)StringCchCopyW(pair.name, NAME_CAP, form.values[0]);
    if (!key_path(path, pair.id) || GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES || !atomic_write(path, key, 64)) {
        error_box(L"无法保存设备密钥，设备未添加。"); goto done;
    }
    previous = config;
    config.pairs[config.count] = pair; config.selected = (int)config.count; ++config.count;
    if (!save_config()) {
        config = previous; DeleteFileW(path); error_box(L"配置保存失败，设备未添加。"); goto done;
    }
    paused = FALSE; restart_selected();
    MessageBoxW(window, L"设备已添加并选中。请将对应密钥文件导入这台 Mac 的菜单栏应用。密钥文件必须保密。", L"Clipboard Bridge", MB_OK | MB_ICONINFORMATION);
done:
    SecureZeroMemory(key, sizeof(key)); SecureZeroMemory(id, sizeof(id));
}
static void export_pair(void) {
    WCHAR path[PATH_CAP], target[PATH_CAP] = L""; char key[65] = {0};
    if (config.selected < 0) return;
    if (!key_path(path, config.pairs[config.selected].id) || !load_key_file(path, key)) { error_box(L"无法读取所选 Mac 的密钥。"); goto done; }
    if (choose_key(target, TRUE) && !write_export(target, key)) error_box(L"导出失败，请选择应用数据目录之外的保存位置。");
done:
    SecureZeroMemory(key, sizeof(key));
}
static void remove_pair(void) {
    Config previous; WCHAR path[PATH_CAP]; int index = config.selected; DWORD i;
    if (index < 0) return;
    if (MessageBoxW(window, L"移除所选 Mac 及本地密钥？该 Mac 将无法连接，直到重新配对。", L"移除 Mac", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    if (!key_path(path, config.pairs[index].id)) return;
    previous = config;
    for (i = (DWORD)index + 1; i < config.count; ++i) config.pairs[i - 1] = config.pairs[i];
    --config.count; ZeroMemory(&config.pairs[config.count], sizeof(Pair)); config.selected = -1;
    if (!save_config()) { config = previous; error_box(L"配置保存失败，设备未移除。"); return; }
    restart_selected();
    if (!DeleteFileW(path)) error_box(L"设备已停用，但旧密钥文件未能删除。请检查应用数据目录的文件权限。");
}
static void settings_dialog(void) {
    Form form; Config previous; char address[16]; WCHAR *end; unsigned long port;
    ZeroMemory(&form, sizeof(form)); form.count = 3;
    (void)StringCchCopyW(form.title, 64, L"连接设置");
    (void)StringCchCopyW(form.labels[0], 64, L"本机 Tailscale IPv4 地址（例如 100.64.1.2）");
    (void)StringCchCopyW(form.labels[1], 64, L"端口（1–65535）");
    (void)StringCchCopyW(form.labels[2], 64, L"滚动行数同步（1 开启，0 关闭）");
    (void)StringCchCopyW(form.values[0], NAME_CAP, config.address);
    (void)StringCchPrintfW(form.values[1], NAME_CAP, L"%lu", (unsigned long)config.port);
    (void)StringCchPrintfW(form.values[2], NAME_CAP, L"%lu", (unsigned long)config.scroll);
    if (!show_form(&form)) return;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, form.values[0], -1, address, 16, NULL, NULL) || !cb_desktop_ipv4_valid(address)) {
        error_box(L"请输入本机有效的 IPv4 地址；不能使用 0.0.0.0。"); return;
    }
    for (end = form.values[1]; *end; ++end) {
        if (*end < L'0' || *end > L'9') { error_box(L"端口必须是 1–65535 的整数。"); return; }
    }
    port = wcstoul(form.values[1], &end, 10);
    if (!form.values[1][0] || *end || port == 0 || port > 65535 ||
        !(wcscmp(form.values[2], L"0") == 0 || wcscmp(form.values[2], L"1") == 0)) {
        error_box(L"请输入有效端口；滚动行数同步只接受 0 或 1。"); return;
    }
    previous = config;
    (void)StringCchCopyW(config.address, 16, form.values[0]); config.port = (DWORD)port; config.scroll = (DWORD)(form.values[2][0] - L'0');
    if (!save_config()) { config = previous; error_box(L"设置保存失败。"); return; }
    restart_selected();
}
static void menu_text(WCHAR text[NAME_CAP * 2], const WCHAR *name) {
    size_t i = 0, j = 0;
    while (name[i] && j + 2 < NAME_CAP * 2) { if (name[i] == L'&') text[j++] = L'&'; text[j++] = name[i++]; }
    text[j] = 0;
}
static void show_menu(void) {
    HMENU menu = CreatePopupMenu(); POINT position; DWORD i; UINT command;
    WCHAR text[NAME_CAP * 2];
    if (!menu) return;
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, status);
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    for (i = 0; i < config.count; ++i) {
        menu_text(text, config.pairs[i].name);
        AppendMenuW(menu, MF_STRING | ((int)i == config.selected ? MF_CHECKED : 0), ID_PAIR + i, text);
    }
    AppendMenuW(menu, MF_STRING, ID_ADD, L"添加 Mac…");
    AppendMenuW(menu, MF_STRING | (config.selected < 0 ? MF_GRAYED : 0), ID_EXPORT, L"导出所选 Mac 密钥…");
    AppendMenuW(menu, MF_STRING | (config.selected < 0 ? MF_GRAYED : 0), ID_REMOVE, L"移除所选 Mac…");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_SETTINGS, L"连接设置…");
    AppendMenuW(menu, MF_STRING | (config.selected < 0 ? MF_GRAYED : 0), ID_PAUSE, paused ? L"恢复同步" : L"暂停同步");
    AppendMenuW(menu, MF_STRING, ID_QUIT, L"退出");
    if (quitting) { DestroyMenu(menu); return; }
    GetCursorPos(&position); SetForegroundWindow(window);
    command = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, position.x, position.y, 0, window, NULL);
    DestroyMenu(menu); PostMessageW(window, WM_NULL, 0, 0);
    if (command >= ID_PAIR && command < ID_PAIR + config.count) {
        int previous = config.selected;
        config.selected = (int)(command - ID_PAIR);
        if (!save_config()) { config.selected = previous; error_box(L"无法保存所选设备。"); return; }
        paused = FALSE; restart_selected();
    } else if (command == ID_ADD) add_pair();
    else if (command == ID_EXPORT) export_pair();
    else if (command == ID_REMOVE) remove_pair();
    else if (command == ID_SETTINGS) settings_dialog();
    else if (command == ID_PAUSE) { paused = !paused; restart_selected(); }
    else if (command == ID_QUIT) { quitting = TRUE; want_start = FALSE; request_stop(); }
}
static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == taskbar_created && taskbar_created != 0) { update_tray(TRUE); return 0; }
    switch (message) {
    case WM_TRAY:
        if (lparam == WM_RBUTTONUP || lparam == WM_LBUTTONUP || lparam == WM_LBUTTONDBLCLK) show_menu();
        return 0;
    case WM_TIMER: poll_worker(); return 0;
    case WM_CLOSE:
        quitting = TRUE; want_start = FALSE; request_stop();
        if (!process_handle) DestroyWindow(hwnd);
        return 0;
    case WM_QUERYENDSESSION:
        quitting = TRUE; want_start = FALSE; request_stop(); return TRUE;
    case WM_ENDSESSION:
        if (wparam) DestroyWindow(hwnd);
        else { quitting = FALSE; restart_selected(); }
        return 0;
    case WM_DESTROY: {
        NOTIFYICONDATAW icon; ZeroMemory(&icon, sizeof(icon));
        icon.cbSize = (DWORD)sizeof(icon); icon.hWnd = hwnd; icon.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &icon); KillTimer(hwnd, 1); PostQuitMessage(0); return 0;
    }
    default: return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}
int WINAPI wWinMain(HINSTANCE current, HINSTANCE previous, LPWSTR command, int show) {
    WNDCLASSW cls; MSG message; WCHAR executable[PATH_CAP], base[MAX_PATH]; WCHAR *slash;
    DWORD length; int result;
    (void)previous; (void)command; (void)show; instance = current;
    mutex_handle = CreateMutexW(NULL, FALSE, L"Local\\ClipboardBridge.NativeTray.v1");
    if (!mutex_handle) { error_box(L"无法创建应用实例锁。"); return 1; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"Clipboard Bridge 已在系统托盘中运行。请点击托盘图标。", L"Clipboard Bridge", MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex_handle); return 0;
    }
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, base)) ||
        FAILED(StringCchPrintfW(directory, PATH_CAP, L"%s\\ClipboardBridge", base)) || !secure_directory()) {
        error_box(L"无法创建仅当前用户可访问的应用数据目录。"); CloseHandle(mutex_handle); return 1;
    }
    length = GetModuleFileNameW(NULL, executable, PATH_CAP);
    slash = wcsrchr(executable, L'\\');
    if (length == 0 || length >= PATH_CAP || !slash) { CloseHandle(mutex_handle); return 1; }
    *slash = 0;
    if (FAILED(StringCchPrintfW(worker, PATH_CAP, L"%s\\clipboard-bridge.exe", executable)) || !load_config()) {
        error_box(L"配对配置损坏或密钥不可读。为避免连接错误设备，应用未启动。请修复本地应用数据中的配置或凭据。");
        CloseHandle(mutex_handle); return 1;
    }
    ZeroMemory(&cls, sizeof(cls)); cls.hInstance = current; cls.lpfnWndProc = window_proc;
    cls.lpszClassName = L"ClipboardBridgeTray"; cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    if (!RegisterClassW(&cls)) { CloseHandle(mutex_handle); return 1; }
    taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    window = CreateWindowExW(0, cls.lpszClassName, L"Clipboard Bridge", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, current, NULL);
    if (!window) { CloseHandle(mutex_handle); return 1; }
    update_tray(TRUE);
    if (!SetTimer(window, 1, 250, NULL)) { DestroyWindow(window); CloseHandle(mutex_handle); return 1; }
    want_start = config.selected >= 0;
    if (config.count == 0) {
        MessageBoxW(window, L"Clipboard Bridge 已在系统托盘运行。点击托盘图标，添加并选择一台 Mac。每次只有所选 Mac 可以连接。应用不会自动随系统启动。", L"Clipboard Bridge", MB_OK | MB_ICONINFORMATION);
    }
    if (!config.address[0]) {
        want_start = FALSE; status = L"请设置本机 Tailscale IPv4";
        update_tray(FALSE); settings_dialog();
    }
    while ((result = (int)GetMessageW(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    if (control_write) CloseHandle(control_write);
    if (output_read) CloseHandle(output_read);
    if (process_handle) CloseHandle(process_handle);
    CloseHandle(mutex_handle);
    if (private_descriptor) LocalFree(private_descriptor);
    return result < 0 ? 1 : 0;
}
