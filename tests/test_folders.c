/* 只在私有测试目录操作，不接触系统剪贴板。包含实现以复用原生路径助手。 */
#include <assert.h>
#include "../src/files.c"
static char *join(const char *a,const char *b) {
    size_t n=strlen(a)+strlen(b)+2;char *p=malloc(n);assert(p);snprintf(p,n,"%s/%s",a,b);return p;
}
static void transfer(cb_file_tx *tx,cb_file_rx *rx,unsigned char *meta,size_t length) {
    assert(cb_file_rx_begin(rx,meta,length)==0);
    unsigned char block[CB_FILE_CHUNK];size_t n=0;int r;
    while((r=cb_file_tx_chunk(tx,block,&n))>0) assert(cb_file_rx_chunk(rx,block,n)==0);
    assert(r==0 && cb_file_tx_end(tx,block,&n)==0 && cb_file_rx_end(rx,block,n)==0);
}
int main(void) {
    char *fixture=make_directory();assert(fixture);
    char *folder=join(fixture,"中文🙂"),*sub=join(folder,"sub"),*empty=join(sub,"empty");
    assert(create_directory(folder)==0 && create_directory(sub)==0 && create_directory(empty)==0);
    char *file=join(sub,"data.txt"),*zero=join(folder,"zero"),*extra=join(fixture,"extra.txt");
    FILE *f=open_regular(file,1);assert(f);assert(fwrite("123456789",1,9,f)==9);assert(fclose(f)==0);
    f=open_regular(zero,1);assert(f && fclose(f)==0);
    f=open_regular(extra,1);assert(f);fputs("other",f);assert(fclose(f)==0);
    const char *paths[]={folder,extra};cb_file_tx tx={0};cb_file_rx rx={0};unsigned char *meta=NULL;size_t length=0;
    assert(cb_file_tx_open(&tx,paths,2,&meta,&length)==0);
    transfer(&tx,&rx,meta,length);
    assert(rx.root_count==2); // 顶层文件夹和文件，不能把子文件单独粘贴出去。
    char *received=join(rx.roots[0],"sub/data.txt"),*received_empty=join(rx.roots[0],"sub/empty");
    f=open_regular(received,0);assert(f);char text[10]={0};assert(fread(text,1,9,f)==9);fclose(f);assert(!strcmp(text,"123456789"));
    uint64_t size=0,stamp=0;assert(path_kind(received_empty,&size,&stamp)==2);
    cb_file_rx_close(&rx);assert(path_kind(received,&size,&stamp)==0);cb_file_tx_close(&tx);free(meta);meta=NULL;
    /* 未完成的目录树接收也清理所有已创建的层级。 */
    assert(cb_file_tx_open(&tx,paths,2,&meta,&length)==0);
    assert(cb_file_rx_begin(&rx,meta,length)==0);
    char *unfinished=copy_path(rx.directory);assert(unfinished);
    unsigned char partial[CB_FILE_CHUNK];size_t part_length=0;
    assert(cb_file_tx_chunk(&tx,partial,&part_length)==1 && cb_file_rx_chunk(&rx,partial,part_length)==0);
    cb_file_rx_close(&rx);assert(path_kind(unfinished,&size,&stamp)==0);
    free(unfinished);cb_file_tx_close(&tx);free(meta);meta=NULL;
    /* 只选择空文件夹时没有数据块，也必须发布完整目录。 */
    const char *only_empty[]={empty};assert(cb_file_tx_open(&tx,only_empty,1,&meta,&length)==0);
    transfer(&tx,&rx,meta,length);assert(rx.root_count==1 && rx.total==0 && rx.verified);
    cb_file_rx_close(&rx);cb_file_tx_close(&tx);free(meta);meta=NULL;
    /* 复制中的源树改变：不能报告完整成功。 */
    assert(cb_file_tx_open(&tx,paths,2,&meta,&length)==0);
    char *added=join(folder,"added");f=open_regular(added,1);assert(f && fclose(f)==0);
    unsigned char block[CB_FILE_CHUNK];size_t n=0;int r;
    while((r=cb_file_tx_chunk(&tx,block,&n))>0) {}
    assert(r==0 && cb_file_tx_end(&tx,block,&n)!=0);cb_file_tx_close(&tx);free(meta);meta=NULL;delete_path(added,0);free(added);
#ifndef _WIN32
    /* 符号链接不被跟随，避免复制到选择范围之外。 */
    char *link=join(folder,"link");assert(symlink(extra,link)==0);
    assert(cb_file_tx_open(&tx,paths,2,&meta,&length)!=0);delete_path(link,0);free(link);
#endif
    /* 树清单也拒绝 ..，且在验证之前不创建目标目录。 */
    unsigned char bad[]={0x80,0,0,1,0,0,0,0,0,0,0,0,0,4,'.','.', '/', 'x'};
    assert(cb_file_rx_begin(&rx,bad,sizeof(bad))!=0 && !rx.directory);
    unsigned char too_many[]={0x80,0,4,1};
    assert(cb_file_rx_begin(&rx,too_many,sizeof(too_many))!=0 && !rx.directory);
    unsigned char orphan[]={0x80,0,0,1,0,0,0,0,0,0,0,0,0,3,'a','/','b'};
    assert(cb_file_rx_begin(&rx,orphan,sizeof(orphan))!=0 && !rx.directory);
    free(received);free(received_empty);
    delete_path(file,0);delete_path(zero,0);delete_path(extra,0);delete_path(empty,1);delete_path(sub,1);delete_path(folder,1);delete_path(fixture,1);
    free(file);free(zero);free(extra);free(empty);free(sub);free(folder);free(fixture);
    puts("Folder hierarchy, empty folders, mixed roots, changes and link rejection tests passed");return 0;
}
