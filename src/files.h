#ifndef CB_FILES_H
#define CB_FILES_H
#include <time.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define CB_FILE_COUNT 64u
#define CB_FILE_CHUNK 16384u
#define CB_FILE_META 262144u
#define CB_FILE_ENTRIES 1024u
#define CB_FILE_DEPTH 32u
#define CB_FILE_PATH 1024u
#define CB_FILE_TOTAL (UINT64_C(1024)*1024*1024)
/* 普通文件使用旧清单；目录树用版本位及经过校验的相对路径，不能指定接收根目录。 */
typedef struct {
    unsigned count,index;
    FILE *stream;
    char *paths[CB_FILE_ENTRIES],*names[CB_FILE_ENTRIES];
    unsigned char directories[CB_FILE_ENTRIES];
    uint64_t sizes[CB_FILE_ENTRIES],stamps[CB_FILE_ENTRIES],offset,total,done;
    uint32_t crc[CB_FILE_ENTRIES];
    size_t meta_length;
    int tree;
} cb_file_tx;
typedef struct {
    unsigned count,index;
    FILE *stream;
    char *directory,*paths[CB_FILE_ENTRIES],*roots[CB_FILE_COUNT];
    unsigned root_count;
    unsigned char directories[CB_FILE_ENTRIES],created[CB_FILE_ENTRIES];
    uint64_t sizes[CB_FILE_ENTRIES],offset,total,done;
    uint32_t crc[CB_FILE_ENTRIES];
    int verified,published;
} cb_file_rx;
int cb_file_name_valid(const char *name);
int cb_file_tx_open(cb_file_tx *tx,const char *const *paths,unsigned count,unsigned char **manifest,size_t *length);
/* 1 一块数据，0 读取完毕，-1 失败/源文件改变；内存始终有界。 */
int cb_file_tx_chunk(cb_file_tx *tx,unsigned char *block,size_t *length);
int cb_file_tx_end(cb_file_tx *tx,unsigned char *checksums,size_t *length);
void cb_file_tx_close(cb_file_tx *tx);
int cb_file_rx_begin(cb_file_rx *rx,const unsigned char *manifest,size_t length);
int cb_file_rx_chunk(cb_file_rx *rx,const unsigned char *block,size_t length);
int cb_file_rx_end(cb_file_rx *rx,const unsigned char *checksums,size_t length);
/* 未发布则删除文件和目录；已发布保留磁盘文件供之后粘贴。 */
void cb_file_rx_close(cb_file_rx *rx);
/* 每次清理前查询当前剪贴板；回调非零（含读取失败）表示保留。 */
int cb_file_cache_contains(const char *directory,const char *path);
unsigned cb_file_cache_clean(int (*in_use)(const char *));
/* 显式目录/时间入口，便于只针对独立目录进行维护和测试。 */
unsigned cb_file_cache_clean_at(const char *base,time_t now,int (*in_use)(const char *));

#endif
