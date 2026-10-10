/* 原生 CF_HDROP 内存格式往返，不打开或修改系统剪贴板。 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
static HGLOBAL test_drop;
static HANDLE test_get_clipboard(UINT format) {return format==CF_HDROP?test_drop:NULL;}
static BOOL test_available(UINT format) {return format==CF_HDROP && test_drop!=NULL;}
#define GetClipboardData test_get_clipboard
#define IsClipboardFormatAvailable test_available
#include "../src/platform_windows.c"
int main(void) {
    char *paths[]={"C:\\Users\\test\\中文🙂.txt","C:\\Users\\test\\second.bin"};
    cb_content input={0},output={0};input.files=paths;input.file_count=2;
    test_drop=prepare_files(&input);assert(test_drop);
    assert(read_files(&output)==1 && output.file_count==2 && !output.text && !output.png);
    assert(!strcmp(paths[0],output.files[0]) && !strcmp(paths[1],output.files[1]));
    cb_content_free(&output);GlobalFree(test_drop);test_drop=NULL;
    assert(read_files(&output)==0);
    input.file_count=0;assert(!prepare_files(&input));
    input.file_count=2;input.text="mixed";assert(!prepare_files(&input));
    puts("Windows CF_HDROP Unicode multi-file snapshot tests passed");return 0;
}
