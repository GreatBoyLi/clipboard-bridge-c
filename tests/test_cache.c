/* 仅使用独立临时目录，不扫描用户实际缓存。 */
#include "../src/files.c"
#include <assert.h>
#include <time.h>
static const char *protected_path;
static int guard(const char *directory) {
    return protected_path ? cb_file_cache_contains(directory,protected_path) : 0;
}
static void age(const char *path) {
#ifdef _WIN32
    wchar_t *wide=wide_path(path);
    HANDLE h=CreateFileW(wide,FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);free(wide);
    assert(h!=INVALID_HANDLE_VALUE);
    uint64_t ticks=((uint64_t)time(NULL)-90000+UINT64_C(11644473600))*10000000;
    FILETIME ft={(DWORD)ticks,(DWORD)(ticks>>32)};
    assert(SetFileTime(h,NULL,NULL,&ft));CloseHandle(h);
#else
    struct timespec times[2]={{time(NULL)-90000,0},{time(NULL)-90000,0}};
    assert(utimensat(AT_FDCWD,path,times,0)==0);
#endif
}
int main(void) {
    char *base=make_directory();assert(base);
    char *old=join_path(base,"clipboard-bridge-ABC123");
    char *recent=join_path(base,"clipboard-bridge-DEF456");
    char *other=join_path(base,"clipboard-bridge-my-files");
    assert(create_directory(old)==0 && create_directory(recent)==0 && create_directory(other)==0);
    char *nested=join_path(old,"folder"),*file=join_path(nested,"file.txt");
    assert(create_directory(nested)==0);
    FILE *f=open_regular(file,1);assert(f);fputs("keep",f);fclose(f);
    age(old);age(other);
    protected_path=file;
    assert(cb_file_cache_clean_at(base,time(NULL),guard)==0);
    uint64_t size=0,stamp=0;assert(path_kind(file,&size,&stamp)==1);
    /* 相似前缀不能被认为是子目录；无法解析的剪贴板路径保守保留。 */
    assert(!cb_file_cache_contains(old,recent));
    assert(cb_file_cache_contains(old,"/nonexistent-clipboard-path"));
    protected_path=NULL;
#ifndef _WIN32
    char *link=join_path(old,"link");assert(symlink(other,link)==0);age(old);
    assert(cb_file_cache_clean_at(base,time(NULL),guard)==0);
    assert(path_kind(file,&size,&stamp)==1); /* 遇到链接，整个缓存保留。 */
    assert(unlink(link)==0);free(link);age(old);
#endif
    assert(cb_file_cache_clean_at(base,time(NULL),guard)==1);
    assert(path_kind(old,&size,&stamp)==0);
    assert(path_kind(recent,&size,&stamp)==2 && path_kind(other,&size,&stamp)==2);
    delete_path(recent,1);delete_path(other,1);delete_path(base,1);
    free(base);free(old);free(recent);free(other);free(nested);free(file);
    puts("Cache cleanup tests passed");return 0;
}
