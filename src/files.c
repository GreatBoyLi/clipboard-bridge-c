#define _DARWIN_C_SOURCE
#include "files.h"
#include "protocol.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <io.h>
#else
#include <unistd.h>
#endif
static uint32_t get32(const unsigned char *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void put32(unsigned char *p,uint32_t n) {
    p[0]=(unsigned char)(n>>24);p[1]=(unsigned char)(n>>16);p[2]=(unsigned char)(n>>8);p[3]=(unsigned char)n;
}
static uint64_t get64(const unsigned char *p) {return ((uint64_t)get32(p)<<32)|get32(p+4);}
static void put64(unsigned char *p,uint64_t n) {put32(p,(uint32_t)(n>>32));put32(p+4,(uint32_t)n);}
/* CRC32 是传输完整性检查，认证和加密仍由既有密钥/Tailscale 提供。 */
static uint32_t crc_update(uint32_t crc,const unsigned char *p,size_t n) {
    uint32_t table[256];
    for(unsigned i=0;i<256;i++) {uint32_t c=i;for(unsigned j=0;j<8;j++) c=(c>>1)^((c&1)?0xedb88320u:0);table[i]=c;}
    for(size_t i=0;i<n;i++) crc=(crc>>8)^table[(crc^p[i])&255];
    return crc;
}
static int same_name(const char *a,const char *b) {
    while(*a && *b) {if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++)) return 0;}return *a==*b;
}
int cb_file_name_valid(const char *s) {
    if(!s) return 0;
    size_t n=strlen(s);if(!n || n>240 || s[n-1]=='.' || s[n-1]==' ' ||
       !cb_valid_utf8((const unsigned char *)s,n)) return 0;
    for(size_t i=0;i<n;i++) if((unsigned char)s[i]<32 || strchr("/\\:*?\"<>|",s[i])) return 0;
    char stem[16]={0};size_t k=0;
    while(k<n && s[k]!='.' && k<sizeof(stem)-1) {stem[k]=(char)toupper((unsigned char)s[k]);k++;}
    if(!strcmp(stem,"CON") || !strcmp(stem,"PRN") || !strcmp(stem,"AUX") || !strcmp(stem,"NUL")) return 0;
    if(k==4 && (!memcmp(stem,"COM",3) || !memcmp(stem,"LPT",3)) && stem[3]>='1' && stem[3]<='9') return 0;
    return 1;
}
#ifdef _WIN32
static wchar_t *wide_path(const char *path) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);
    if(n<=0) return NULL;
    wchar_t *p=malloc((size_t)n*sizeof(*p));
    if(p && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,p,n)) {free(p);return NULL;}return p;
}
static FILE *open_regular(const char *path,int writing) {
    wchar_t *wide=wide_path(path);if(!wide) return NULL;
    HANDLE h=CreateFileW(wide,writing?GENERIC_WRITE:GENERIC_READ,
        writing?0:FILE_SHARE_READ,NULL,writing?CREATE_NEW:OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_ATTRIBUTE_NORMAL,NULL);free(wide);
    if(h==INVALID_HANDLE_VALUE) return NULL;
    BY_HANDLE_FILE_INFORMATION info={0};
    if(GetFileType(h)!=FILE_TYPE_DISK || !GetFileInformationByHandle(h,&info) ||
       (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) {CloseHandle(h);return NULL;}
    int fd=_open_osfhandle((intptr_t)h,(writing?_O_WRONLY:_O_RDONLY)|_O_BINARY);
    if(fd<0) {CloseHandle(h);return NULL;}
    FILE *f=_fdopen(fd,writing?"wb":"rb");if(!f) _close(fd);return f;
}
static int signature(FILE *f,uint64_t *size,uint64_t *stamp) {
    BY_HANDLE_FILE_INFORMATION i={0};
    if(!GetFileInformationByHandle((HANDLE)_get_osfhandle(_fileno(f)),&i)) return -1;
    *size=((uint64_t)i.nFileSizeHigh<<32)|i.nFileSizeLow;
    *stamp=((uint64_t)i.ftLastWriteTime.dwHighDateTime<<32)|i.ftLastWriteTime.dwLowDateTime;return 0;
}
static void delete_path(const char *path,int directory) {
    wchar_t *p=wide_path(path);if(p) {if(directory) RemoveDirectoryW(p);else DeleteFileW(p);free(p);}
}
static char *make_directory(void) {
    wchar_t base[MAX_PATH+1],path[MAX_PATH+80];unsigned char random[12];
    DWORD n=GetTempPathW(MAX_PATH+1,base);if(!n || n>MAX_PATH) return NULL;
    for(unsigned retry=0;retry<10;retry++) {
        if(BCryptGenRandom(NULL,random,(ULONG)sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0) return NULL;
        memcpy(path,base,((size_t)n+1)*sizeof(wchar_t));
        static const wchar_t prefix[]=L"clipboard-bridge-";
        memcpy(path+n,prefix,(sizeof(prefix)/sizeof(*prefix)-1)*sizeof(wchar_t));
        size_t pos=(size_t)n+sizeof(prefix)/sizeof(*prefix)-1;
        for(unsigned i=0;i<sizeof(random);i++) {
            path[pos++]=L"0123456789abcdef"[random[i]>>4];path[pos++]=L"0123456789abcdef"[random[i]&15];
        }
        path[pos]=0;
        if(!CreateDirectoryW(path,NULL)) continue;
        int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,NULL,0,NULL,NULL);
        char *result=bytes>0?malloc((size_t)bytes):NULL;
        if(result && WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,result,bytes,NULL,NULL)) return result;
        free(result);RemoveDirectoryW(path);return NULL;
    }return NULL;
}
#else
static FILE *open_regular(const char *path,int writing) {
    int fd=open(path,writing?(O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW):(O_RDONLY|O_NOFOLLOW|O_NONBLOCK),0600);
    if(fd<0) return NULL;
    struct stat s;if(fstat(fd,&s)!=0 || !S_ISREG(s.st_mode)) {close(fd);return NULL;}
    FILE *f=fdopen(fd,writing?"wb":"rb");if(!f) close(fd);return f;
}
static int signature(FILE *f,uint64_t *size,uint64_t *stamp) {
    struct stat s;if(fstat(fileno(f),&s)!=0 || s.st_size<0) return -1;
    *size=(uint64_t)s.st_size;
#ifdef __APPLE__
    *stamp=(uint64_t)s.st_mtimespec.tv_sec*UINT64_C(1000000000)+(uint64_t)s.st_mtimespec.tv_nsec;
#else
    *stamp=(uint64_t)s.st_mtim.tv_sec*UINT64_C(1000000000)+(uint64_t)s.st_mtim.tv_nsec;
#endif
    return 0;
}
static void delete_path(const char *path,int directory) {if(directory) (void)rmdir(path);else (void)unlink(path);}
static char *make_directory(void) {
    const char *base=getenv("TMPDIR");if(!base || !*base) base="/tmp";
    size_t n=strlen(base)+64;char *p=malloc(n);if(!p) return NULL;
    snprintf(p,n,"%s/clipboard-bridge-XXXXXX",base);
    if(!mkdtemp(p)) {free(p);return NULL;}return p;
}
#endif
void cb_file_tx_close(cb_file_tx *tx) {
    for(unsigned i=0;i<CB_FILE_COUNT;i++) if(tx->streams[i]) fclose(tx->streams[i]);
    memset(tx,0,sizeof(*tx));
}
int cb_file_tx_open(cb_file_tx *tx,const char *const *paths,unsigned count,unsigned char **manifest,size_t *length) {
    if(!tx || !manifest || !length) return -1;
    *manifest=NULL;*length=0;
    if(!paths || !count || count>CB_FILE_COUNT) return -1;
    const char *names[CB_FILE_COUNT];size_t meta=4;
    for(unsigned i=0;i<count;i++) {
        if(!paths[i]) goto fail;
        const char *a=strrchr(paths[i],'/'),*b=strrchr(paths[i],'\\');
        names[i]=a && (!b || a>b)?a+1:b?b+1:paths[i];
        if(!cb_file_name_valid(names[i])) goto fail;
        for(unsigned j=0;j<i;j++) if(same_name(names[i],names[j])) goto fail;
        meta+=10+strlen(names[i]);
        tx->streams[i]=open_regular(paths[i],0);
        if(!tx->streams[i] || signature(tx->streams[i],&tx->sizes[i],&tx->stamps[i])!=0 ||
           tx->sizes[i]>CB_FILE_TOTAL-tx->total) goto fail;
        tx->total+=tx->sizes[i];tx->crc[i]=0xffffffffu;
    }
    if(meta>CB_FILE_META) goto fail;
    unsigned char *p=malloc(meta);if(!p) goto fail;
    put32(p,count);size_t pos=4;
    for(unsigned i=0;i<count;i++) {
        size_t n=strlen(names[i]);put64(p+pos,tx->sizes[i]);p[pos+8]=(unsigned char)(n>>8);p[pos+9]=(unsigned char)n;
        memcpy(p+pos+10,names[i],n);pos+=10+n;
    }
    tx->count=count;*manifest=p;*length=meta;return 0;
fail:cb_file_tx_close(tx);return -1;
}
int cb_file_tx_chunk(cb_file_tx *tx,unsigned char *block,size_t *length) {
    *length=0;
    while(tx->index<tx->count) {
        unsigned i=tx->index;
        if(tx->offset==tx->sizes[i]) {
            uint64_t size=0,stamp=0;
            if(fgetc(tx->streams[i])!=EOF || ferror(tx->streams[i]) ||
               signature(tx->streams[i],&size,&stamp)!=0 || size!=tx->sizes[i] || stamp!=tx->stamps[i]) return -1;
            fclose(tx->streams[i]);tx->streams[i]=NULL;tx->index++;tx->offset=0;continue;
        }
        uint64_t remaining=tx->sizes[i]-tx->offset;
        size_t n=remaining>CB_FILE_CHUNK?CB_FILE_CHUNK:(size_t)remaining;
        if(fread(block,1,n,tx->streams[i])!=n) return -1;
        tx->crc[i]=crc_update(tx->crc[i],block,n);tx->offset+=n;tx->done+=n;*length=n;return 1;
    }return 0;
}
int cb_file_tx_end(cb_file_tx *tx,unsigned char *checksums,size_t *length) {
    if(tx->index!=tx->count || !tx->count) return -1;
    for(unsigned i=0;i<tx->count;i++) put32(checksums+4*i,tx->crc[i]^0xffffffffu);
    *length=(size_t)tx->count*4;return 0;
}
void cb_file_rx_close(cb_file_rx *rx) {
    if(rx->stream) fclose(rx->stream);
    for(unsigned i=0;i<CB_FILE_COUNT;i++) if(rx->paths[i]) {
        if(!rx->published) delete_path(rx->paths[i],0);free(rx->paths[i]);
    }
    if(rx->directory) {if(!rx->published) delete_path(rx->directory,1);free(rx->directory);}
    memset(rx,0,sizeof(*rx));
}
/* 空文件也必须落盘。每个目标都以独占创建打开，防止覆盖/符号链接。 */
static int advance(cb_file_rx *rx) {
    while(rx->index<rx->count) {
        if(!rx->stream) {rx->stream=open_regular(rx->paths[rx->index],1);if(!rx->stream) return -1;}
        if(rx->offset!=rx->sizes[rx->index]) return 0;
        int ok=fclose(rx->stream);rx->stream=NULL;if(ok!=0) return -1;
        rx->index++;rx->offset=0;
    }return 0;
}
int cb_file_rx_begin(cb_file_rx *rx,const unsigned char *meta,size_t length) {
    if(rx->directory || !meta || length<4 || length>CB_FILE_META) return -1;
    unsigned count=get32(meta);if(!count || count>CB_FILE_COUNT) return -1;
    char names[CB_FILE_COUNT][241];size_t pos=4;uint64_t total=0;
    for(unsigned i=0;i<count;i++) {
        if(length-pos<10) return -1;
        uint64_t size=get64(meta+pos);size_t n=((size_t)meta[pos+8]<<8)|meta[pos+9];pos+=10;
        if(!n || n>240 || n>length-pos || size>CB_FILE_TOTAL-total || memchr(meta+pos,0,n)) return -1;
        memcpy(names[i],meta+pos,n);names[i][n]=0;pos+=n;
        if(!cb_file_name_valid(names[i])) return -1;
        for(unsigned j=0;j<i;j++) if(same_name(names[i],names[j])) return -1;
        rx->sizes[i]=size;rx->crc[i]=0xffffffffu;total+=size;
    }
    if(pos!=length) return -1;
    rx->directory=make_directory();if(!rx->directory) return -1;
    rx->count=count;rx->total=total;
    for(unsigned i=0;i<count;i++) {
        size_t n=strlen(rx->directory)+strlen(names[i])+2;rx->paths[i]=malloc(n);
        if(!rx->paths[i]) goto fail;
        snprintf(rx->paths[i],n,"%s/%s",rx->directory,names[i]);
    }
    if(advance(rx)!=0) goto fail;
    return 0;
fail:cb_file_rx_close(rx);return -1;
}
int cb_file_rx_chunk(cb_file_rx *rx,const unsigned char *block,size_t length) {
    if(!rx->directory || rx->verified || !length || length>CB_FILE_CHUNK || (uint64_t)length>rx->total-rx->done) return -1;
    size_t pos=0;
    while(pos<length) {
        if(advance(rx)!=0 || rx->index>=rx->count) return -1;
        uint64_t remaining=rx->sizes[rx->index]-rx->offset;
        size_t n=remaining>length-pos?length-pos:(size_t)remaining;
        if(fwrite(block+pos,1,n,rx->stream)!=n) return -1;
        rx->crc[rx->index]=crc_update(rx->crc[rx->index],block+pos,n);rx->offset+=n;rx->done+=n;pos+=n;
    }return advance(rx);
}
int cb_file_rx_end(cb_file_rx *rx,const unsigned char *checksums,size_t length) {
    if(!rx->directory || rx->verified || rx->done!=rx->total || length!=(size_t)rx->count*4 || advance(rx)!=0) return -1;
    for(unsigned i=0;i<rx->count;i++) if(get32(checksums+4*i)!=(rx->crc[i]^0xffffffffu)) return -1;
    rx->verified=1;return 0;
}
