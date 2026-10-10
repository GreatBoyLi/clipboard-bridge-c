#ifndef CB_FILES_H
#define CB_FILES_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define CB_FILE_COUNT 64u
#define CB_FILE_CHUNK 16384u
#define CB_FILE_META 32768u
#define CB_FILE_TOTAL (UINT64_C(1024)*1024*1024)
/* 文件清单只带基本名称与大小，不允许网络路径指定接收位置。 */
typedef struct {
    unsigned count,index;
    FILE *streams[CB_FILE_COUNT];
    uint64_t sizes[CB_FILE_COUNT],stamps[CB_FILE_COUNT],offset,total,done;
    uint32_t crc[CB_FILE_COUNT];
} cb_file_tx;
typedef struct {
    unsigned count,index;
    FILE *stream;
    char *directory,*paths[CB_FILE_COUNT];
    uint64_t sizes[CB_FILE_COUNT],offset,total,done;
    uint32_t crc[CB_FILE_COUNT];
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
#endif
