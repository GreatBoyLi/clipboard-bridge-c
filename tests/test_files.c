#define _DARWIN_C_SOURCE
#include "files.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static wchar_t *test_wide(const char *s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,NULL,0);assert(n>0);
    wchar_t *p=malloc((size_t)n*sizeof(*p));assert(p);
    assert(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,p,n));return p;
}
static FILE *test_fopen(const char *path,const char *mode) {
    wchar_t *p=test_wide(path);FILE *f=_wfopen(p,mode[0]=='w'?L"wb":L"rb");free(p);return f;
}
static int test_remove(const char *path) {wchar_t *p=test_wide(path);int r=_wremove(p);free(p);return r;}
#define fopen test_fopen
#define remove test_remove
#endif
int main(void) {
    assert(cb_file_name_valid("中文🙂.txt"));
    assert(!cb_file_name_valid("../x") && !cb_file_name_valid("NUL.txt"));
    assert(!cb_file_name_valid("a:b") && !cb_file_name_valid("a.") && !cb_file_name_valid("a\\b"));
    cb_file_tx tx={0}; cb_file_rx rx={0};
    unsigned char *meta=NULL, block[CB_FILE_CHUNK], sums[CB_FILE_COUNT*4];
    size_t length=0,n=0;
    /* 用接收器创建私有测试目录，避免覆盖任何已有文件。 */
    unsigned char seed[]={0,0,0,1,0,0,0,0,0,0,0,0,0,7,'f','i','x','t','u','r','e'};
    cb_file_rx fixture={0};assert(cb_file_rx_begin(&fixture,seed,sizeof(seed))==0);
    char first[1024],empty[1024];
    snprintf(first,sizeof(first),"%s/input.txt",fixture.directory);
    snprintf(empty,sizeof(empty),"%s/empty",fixture.directory);
    const char *paths[]={first,empty};
    FILE *f=fopen(paths[0],"wb");assert(f);fputs("中文🙂\nsecond line",f);assert(fclose(f)==0);
    f=fopen(paths[1],"wb");assert(f);assert(fclose(f)==0);
    assert(cb_file_tx_open(&tx,paths,2,&meta,&length)==0);
    assert(cb_file_rx_begin(&rx,meta,length)==0);
    char *directory=malloc(strlen(rx.directory)+1);assert(directory);strcpy(directory,rx.directory);
    int r;
    while((r=cb_file_tx_chunk(&tx,block,&n))>0) assert(cb_file_rx_chunk(&rx,block,n)==0);
    assert(r==0 && cb_file_tx_end(&tx,sums,&n)==0);
    assert(cb_file_rx_end(&rx,sums,n)==0);
    f=fopen(rx.paths[0],"rb");assert(f);char data[80]={0};assert(fread(data,1,sizeof(data)-1,f)>0);fclose(f);
    assert(!strcmp(data,"中文🙂\nsecond line"));
    cb_file_rx_close(&rx);
#ifndef _WIN32
    assert(access(directory,F_OK)!=0);
#endif
    free(directory);cb_file_tx_close(&tx);
    assert(cb_file_rx_begin(&rx,meta,length)==0);
    assert(cb_file_rx_end(&rx,sums,n)!=0);
    cb_file_rx_close(&rx);free(meta);meta=NULL;
    assert(cb_file_tx_open(&tx,paths,2,&meta,&length)==0);
    assert(cb_file_rx_begin(&rx,meta,length)==0);
    while((r=cb_file_tx_chunk(&tx,block,&n))>0) assert(cb_file_rx_chunk(&rx,block,n)==0);
    assert(cb_file_tx_end(&tx,sums,&n)==0);sums[0]^=1;
    assert(cb_file_rx_end(&rx,sums,n)!=0);cb_file_rx_close(&rx);cb_file_tx_close(&tx);
    assert(cb_file_rx_begin(&rx,meta,length-1)!=0);
    free(meta);meta=NULL;
    /* 空文件可完成；超限大小、路径穿越清单不能创建接收目录。 */
    unsigned char bad[sizeof(seed)];memcpy(bad,seed,sizeof(seed));bad[4]=1;
    assert(cb_file_rx_begin(&rx,bad,sizeof(bad))!=0 && !rx.directory);
    memcpy(bad,seed,sizeof(seed));memcpy(bad+14,"../evil",7);
    assert(cb_file_rx_begin(&rx,bad,sizeof(bad))!=0 && !rx.directory);
    assert(cb_file_rx_begin(&rx,seed,sizeof(seed))==0);
    unsigned char zero[4]={0};assert(cb_file_rx_end(&rx,zero,4)==0);
    cb_file_rx_close(&rx);
    const char *duplicate[]={paths[0],paths[0]};
    assert(cb_file_tx_open(&tx,duplicate,2,&meta,&length)!=0);
    cb_file_tx_close(&tx);free(meta);
    remove(paths[0]);remove(paths[1]);cb_file_rx_close(&fixture);puts("File stream, names, bounds, checksum and cleanup tests passed");
}
