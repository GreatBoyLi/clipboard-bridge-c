#define _DARWIN_C_SOURCE
#include "files.h"
#include "protocol.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <io.h>
#else
#include <unistd.h>
#include <dirent.h>
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
/* 文件夹嵌套可能超过 MAX_PATH；统一转换为绝对扩展路径。 */
static wchar_t *wide_path(const char *path) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);
    if(n<=0) return NULL;
    wchar_t *raw=malloc((size_t)n*sizeof(*raw));if(!raw) return NULL;
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,raw,n)) {free(raw);return NULL;}
    for(int i=0;i<n;i++) if(raw[i]==L'/') raw[i]=L'\\';
    if(n>=5 && raw[0]==L'\\' && raw[1]==L'\\' && raw[2]==L'?' && raw[3]==L'\\') return raw;
    DWORD needed=GetFullPathNameW(raw,0,NULL,NULL);if(!needed) {free(raw);return NULL;}
    wchar_t *full=malloc((size_t)needed*sizeof(*full));if(!full) {free(raw);return NULL;}
    DWORD used=GetFullPathNameW(raw,needed,full,NULL);free(raw);
    if(!used || used>=needed) {free(full);return NULL;}
    int unc=used>=2 && full[0]==L'\\' && full[1]==L'\\';
    const wchar_t *prefix=unc?L"\\\\?\\UNC\\":L"\\\\?\\";
    size_t prefix_len=unc?8u:4u,skip=unc?2u:0u;
    wchar_t *out=malloc((prefix_len+(size_t)used-skip+1)*sizeof(*out));
    if(out) {memcpy(out,prefix,prefix_len*sizeof(*out));memcpy(out+prefix_len,full+skip,((size_t)used-skip+1)*sizeof(*out));}
    free(full);return out;
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
/* 目录树只接受相对路径，每一段均满足两平台文件名限制。 */
static int relative_valid(const char *name) {
    size_t n=strlen(name);if(!n || n>CB_FILE_PATH || name[0]=='/') return 0;
    unsigned depth=0;const char *p=name;
    while(*p) {
        const char *slash=strchr(p,'/');size_t length=slash?(size_t)(slash-p):strlen(p);
        if(!length || length>240 || ++depth>CB_FILE_DEPTH) return 0;
        char part[241];memcpy(part,p,length);part[length]=0;
        if(!cb_file_name_valid(part)) return 0;
        if(!slash) return 1;
        p=slash+1;
    }return 0;
}
static char *join_path(const char *a,const char *b) {
    size_t n=strlen(a)+strlen(b)+2;char *p=malloc(n);
    if(p) snprintf(p,n,"%s/%s",a,b);return p;
}
static char *copy_path(const char *p) {
    size_t n=strlen(p)+1;char *out=malloc(n);if(out) memcpy(out,p,n);return out;
}
#ifdef _WIN32
static int create_directory(const char *path) {
    wchar_t *p=wide_path(path);if(!p) return -1;
    int ok=CreateDirectoryW(p,NULL)!=0;free(p);return ok?0:-1;
}
/* 打开属性句柄时不跟随重解析点（符号链接、junction 等）。 */
static int path_kind(const char *path,uint64_t *size,uint64_t *stamp) {
    wchar_t *p=wide_path(path);if(!p) return 0;
    HANDLE h=CreateFileW(p,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);free(p);
    if(h==INVALID_HANDLE_VALUE) return 0;
    BY_HANDLE_FILE_INFORMATION i={0};int kind=0;
    if(GetFileInformationByHandle(h,&i) && GetFileType(h)==FILE_TYPE_DISK && !(i.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
        *size=((uint64_t)i.nFileSizeHigh<<32)|i.nFileSizeLow;
        *stamp=((uint64_t)i.ftLastWriteTime.dwHighDateTime<<32)|i.ftLastWriteTime.dwLowDateTime;
        kind=(i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)?2:1;
    }
    CloseHandle(h);return kind;
}
#else
static int create_directory(const char *path) {return mkdir(path,0700);}
static int path_kind(const char *path,uint64_t *size,uint64_t *stamp) {
    struct stat s;if(lstat(path,&s)!=0 || s.st_size<0) return 0;
    if(!S_ISREG(s.st_mode) && !S_ISDIR(s.st_mode)) return 0;
    *size=(uint64_t)s.st_size;
#ifdef __APPLE__
    *stamp=(uint64_t)s.st_mtimespec.tv_sec*UINT64_C(1000000000)+(uint64_t)s.st_mtimespec.tv_nsec;
#else
    *stamp=(uint64_t)s.st_mtim.tv_sec*UINT64_C(1000000000)+(uint64_t)s.st_mtim.tv_nsec;
#endif
    return S_ISDIR(s.st_mode)?2:1;
}
#endif
static int collect(cb_file_tx *tx,const char *path,const char *name);
static int collect_child(cb_file_tx *tx,const char *path,const char *name,const char *child) {
    if(!strcmp(child,".") || !strcmp(child,"..")) return 0;
    if(!cb_file_name_valid(child)) return -1;
    char *full=join_path(path,child),*relative=join_path(name,child);
    int ok=full && relative?collect(tx,full,relative):-1;
    free(full);free(relative);return ok;
}
static int collect(cb_file_tx *tx,const char *path,const char *name) {
    if(tx->count>=CB_FILE_ENTRIES || !relative_valid(name)) return -1;
    for(unsigned j=0;j<tx->count;j++) if(same_name(name,tx->names[j])) return -1;
    unsigned i=tx->count;uint64_t size=0,stamp=0;int kind=path_kind(path,&size,&stamp);
    if(!kind || (kind==1 && size>CB_FILE_TOTAL-tx->total)) return -1;
    tx->meta_length+=10+strlen(name);if(tx->meta_length>CB_FILE_META) return -1;
    tx->paths[i]=copy_path(path);tx->names[i]=copy_path(name);
    if(!tx->paths[i] || !tx->names[i]) return -1;
    tx->directories[i]=(unsigned char)(kind==2);tx->stamps[i]=stamp;
    tx->sizes[i]=kind==2?0:size;tx->crc[i]=0xffffffffu;tx->total+=tx->sizes[i];tx->count++;
    if(kind==1) return 0;
    tx->tree=1;
#ifdef _WIN32
    char *pattern=join_path(path,"*");if(!pattern) return -1;
    wchar_t *wide=wide_path(pattern);free(pattern);if(!wide) return -1;
    WIN32_FIND_DATAW data={0};HANDLE find=FindFirstFileW(wide,&data);free(wide);
    if(find==INVALID_HANDLE_VALUE) return GetLastError()==ERROR_FILE_NOT_FOUND?0:-1;
    int result=0;
    do {
        int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,data.cFileName,-1,NULL,0,NULL,NULL);
        char *child=n>0?malloc((size_t)n):NULL;
        if(!child || !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,data.cFileName,-1,child,n,NULL,NULL)) {
            free(child);result=-1;break;
        }
        result=collect_child(tx,path,name,child);free(child);if(result!=0) break;
    } while(FindNextFileW(find,&data));
    if(result==0 && GetLastError()!=ERROR_NO_MORE_FILES) result=-1;
    FindClose(find);return result;
#else
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd<0) return -1;
    DIR *dir=fdopendir(fd);if(!dir) {close(fd);return -1;}
    int result=0;struct dirent *entry;
    for(;;) {
        errno=0;entry=readdir(dir);if(!entry) {if(errno) result=-1;break;}
        result=collect_child(tx,path,name,entry->d_name);if(result!=0) break;
    }
    closedir(dir);return result;
#endif
}
void cb_file_tx_close(cb_file_tx *tx) {
    if(tx->stream) fclose(tx->stream);
    for(unsigned i=0;i<CB_FILE_ENTRIES;i++) {free(tx->paths[i]);free(tx->names[i]);}
    memset(tx,0,sizeof(*tx));
}
int cb_file_tx_open(cb_file_tx *tx,const char *const *paths,unsigned count,unsigned char **manifest,size_t *length) {
    if(!tx || !manifest || !length) return -1;
    *manifest=NULL;*length=0;
    if(!paths || !count || count>CB_FILE_COUNT) return -1;
    tx->meta_length=4;
    for(unsigned i=0;i<count;i++) {
        if(!paths[i] || !*paths[i]) goto fail;
        /* 规范化选择的根路径结尾（例如目录路径尾部的 /）。 */
        char *full=copy_path(paths[i]);if(!full) goto fail;
        size_t len=strlen(full);while(len>1 && (full[len-1]=='/' || full[len-1]=='\\')) full[--len]=0;
        const char *a=strrchr(full,'/'),*b=strrchr(full,'\\');
        const char *name=a && (!b || a>b)?a+1:b?b+1:full;
        int ok=cb_file_name_valid(name)?collect(tx,full,name):-1;
        free(full);if(ok!=0) goto fail;
    }
    if(!tx->tree && tx->count>CB_FILE_COUNT) goto fail;
    unsigned char *p=malloc(tx->meta_length);if(!p) goto fail;
    put32(p,tx->count|(tx->tree?0x80000000u:0));size_t pos=4;
    for(unsigned i=0;i<tx->count;i++) {
        size_t n=strlen(tx->names[i]);put64(p+pos,tx->directories[i]?UINT64_MAX:tx->sizes[i]);
        p[pos+8]=(unsigned char)(n>>8);p[pos+9]=(unsigned char)n;
        memcpy(p+pos+10,tx->names[i],n);pos+=10+n;
    }
    *manifest=p;*length=tx->meta_length;return 0;
fail:cb_file_tx_close(tx);return -1;
}
int cb_file_tx_chunk(cb_file_tx *tx,unsigned char *block,size_t *length) {
    *length=0;
    while(tx->index<tx->count) {
        unsigned i=tx->index;if(tx->directories[i]) {tx->index++;continue;}
        if(!tx->stream) {
            uint64_t size=0,stamp=0;tx->stream=open_regular(tx->paths[i],0);
            if(!tx->stream || signature(tx->stream,&size,&stamp)!=0 || size!=tx->sizes[i] || stamp!=tx->stamps[i]) return -1;
        }
        if(tx->offset==tx->sizes[i]) {
            uint64_t size=0,stamp=0;
            if(fgetc(tx->stream)!=EOF || ferror(tx->stream) || signature(tx->stream,&size,&stamp)!=0 ||
               size!=tx->sizes[i] || stamp!=tx->stamps[i]) return -1;
            fclose(tx->stream);tx->stream=NULL;tx->index++;tx->offset=0;continue;
        }
        uint64_t remaining=tx->sizes[i]-tx->offset;
        size_t n=remaining>CB_FILE_CHUNK?CB_FILE_CHUNK:(size_t)remaining;
        if(fread(block,1,n,tx->stream)!=n) return -1;
        tx->crc[i]=crc_update(tx->crc[i],block,n);tx->offset+=n;tx->done+=n;*length=n;return 1;
    }return 0;
}
int cb_file_tx_end(cb_file_tx *tx,unsigned char *checksums,size_t *length) {
    if(tx->index!=tx->count || !tx->count) return -1;
    for(unsigned i=0;i<tx->count;i++) {
        if(tx->directories[i]) {uint64_t size=0,stamp=0;
            if(path_kind(tx->paths[i],&size,&stamp)!=2 || stamp!=tx->stamps[i]) return -1;}
        put32(checksums+4*i,tx->crc[i]^0xffffffffu);
    }
    *length=(size_t)tx->count*4;return 0;
}
void cb_file_rx_close(cb_file_rx *rx) {
    if(rx->stream) fclose(rx->stream);
    /* 逆序删除，先子文件和空目录，最后根目录；只清理本次实际创建的条目。 */
    for(unsigned i=rx->count;i>0;i--) if(rx->paths[i-1] && !rx->published && rx->created[i-1])
        delete_path(rx->paths[i-1],rx->directories[i-1]);
    for(unsigned i=0;i<CB_FILE_ENTRIES;i++) free(rx->paths[i]);
    if(rx->directory) {if(!rx->published) delete_path(rx->directory,1);free(rx->directory);}
    memset(rx,0,sizeof(*rx));
}
static int advance(cb_file_rx *rx) {
    while(rx->index<rx->count) {
        if(rx->directories[rx->index]) {rx->index++;continue;}
        if(!rx->stream) {
            rx->stream=open_regular(rx->paths[rx->index],1);if(!rx->stream) return -1;
            rx->created[rx->index]=1;
        }
        if(rx->offset!=rx->sizes[rx->index]) return 0;
        int ok=fclose(rx->stream);rx->stream=NULL;if(ok!=0) return -1;
        rx->index++;rx->offset=0;
    }return 0;
}
int cb_file_rx_begin(cb_file_rx *rx,const unsigned char *meta,size_t length) {
    if(rx->directory || !meta || length<4 || length>CB_FILE_META) return -1;
    uint32_t version=get32(meta);int tree=(version&0x80000000u)!=0;unsigned count=version&0x7fffffffu;
    if(!count || count>(tree?CB_FILE_ENTRIES:CB_FILE_COUNT)) return -1;
    /* 先完整验证清单；非法路径不能创建任何接收文件。 */
    char **names=calloc(count,sizeof(char *));if(!names) return -1;
    unsigned roots=0;size_t pos=4;uint64_t total=0;int result=-1;
    for(unsigned i=0;i<count;i++) {
        if(length-pos<10) goto done;
        uint64_t size=get64(meta+pos);size_t n=((size_t)meta[pos+8]<<8)|meta[pos+9];pos+=10;
        int directory=tree && size==UINT64_MAX;
        if(!n || n>(tree?CB_FILE_PATH:240u) || n>length-pos || memchr(meta+pos,0,n) ||
           (!directory && size>CB_FILE_TOTAL-total)) goto done;
        names[i]=malloc(n+1);if(!names[i]) goto done;
        memcpy(names[i],meta+pos,n);names[i][n]=0;pos+=n;
        if(tree?!relative_valid(names[i]):!cb_file_name_valid(names[i])) goto done;
        for(unsigned j=0;j<i;j++) if(same_name(names[i],names[j])) goto done;
        char *slash=strrchr(names[i],'/');
        if(slash) {
            *slash=0;int parent=0;
            for(unsigned j=0;j<i;j++) if(rx->directories[j] && !strcmp(names[j],names[i])) {parent=1;break;}
            *slash='/';if(!parent) goto done;
        } else if(++roots>CB_FILE_COUNT) goto done;
        rx->directories[i]=(unsigned char)directory;rx->sizes[i]=directory?0:size;
        rx->crc[i]=0xffffffffu;total+=rx->sizes[i];
    }
    if(pos!=length || !roots) goto done;
    rx->directory=make_directory();if(!rx->directory) goto done;
    rx->count=count;rx->total=total;
    for(unsigned i=0;i<count;i++) {
        rx->paths[i]=join_path(rx->directory,names[i]);if(!rx->paths[i]) goto done;
        if(!strchr(names[i],'/')) rx->roots[rx->root_count++]=rx->paths[i];
        if(rx->directories[i]) {
            if(create_directory(rx->paths[i])!=0) goto done;
            rx->created[i]=1;
        }
    }
    if(advance(rx)!=0) goto done;
    result=0;
done:
    for(unsigned i=0;i<count;i++) free(names[i]);free(names);
    if(result!=0) cb_file_rx_close(rx);
    return result;
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

/* 缓存维护只识别本程序生成的随机目录名，避免误删同前缀的普通目录。 */
static int cache_name_valid(const char *name) {
    const char *prefix="clipboard-bridge-";size_t n=strlen(prefix);
    if(strncmp(name,prefix,n)) return 0;
    const char *suffix=name+n;size_t length=strlen(suffix);
    if(length!=6 && length!=24) return 0;
    for(size_t i=0;i<length;i++) {
        unsigned char c=(unsigned char)suffix[i];
        if(length==24 ? !isxdigit(c) : !((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))) return 0;
    }return 1;
}
/* 枚举不跟随目录链接；回调失败时停止，调用方保留无法安全处理的目录。 */
typedef int (*cache_visitor)(const char *,const char *,void *);
static int cache_walk(const char *path,cache_visitor visit,void *context) {
#ifdef _WIN32
    char *pattern=join_path(path,"*");if(!pattern) return -1;
    wchar_t *wide=wide_path(pattern);free(pattern);if(!wide) return -1;
    WIN32_FIND_DATAW data={0};HANDLE find=FindFirstFileW(wide,&data);free(wide);
    if(find==INVALID_HANDLE_VALUE) return GetLastError()==ERROR_FILE_NOT_FOUND?0:-1;
    int result=0;
    do {
        if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L"..")) continue;
        int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,data.cFileName,-1,NULL,0,NULL,NULL);
        char *name=n>0?malloc((size_t)n):NULL;
        if(!name || !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,data.cFileName,-1,name,n,NULL,NULL)) {
            free(name);result=-1;break;
        }
        char *child=join_path(path,name);result=child?visit(child,name,context):-1;
        free(child);free(name);if(result) break;
    } while(FindNextFileW(find,&data));
    if(!result && GetLastError()!=ERROR_NO_MORE_FILES) result=-1;
    FindClose(find);return result;
#else
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd<0) return -1;
    DIR *dir=fdopendir(fd);if(!dir) {close(fd);return -1;}
    int result=0;
    for(;;) {
        errno=0;struct dirent *entry=readdir(dir);if(!entry) {if(errno) result=-1;break;}
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        char *child=join_path(path,entry->d_name);
        result=child?visit(child,entry->d_name,context):-1;free(child);if(result) break;
    }
    closedir(dir);return result;
#endif
}
#ifdef _WIN32
/* 解析 junction、大小写和扩展路径前缀，以实际路径判断剪贴板引用。 */
static wchar_t *cache_real_path(const char *path) {
    wchar_t *wide=wide_path(path);if(!wide) return NULL;
    HANDLE h=CreateFileW(wide,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);free(wide);
    if(h==INVALID_HANDLE_VALUE) return NULL;
    DWORD n=GetFinalPathNameByHandleW(h,NULL,0,FILE_NAME_NORMALIZED);
    wchar_t *out=n?malloc(((size_t)n+1)*sizeof(*out)):NULL;
    DWORD used=out?GetFinalPathNameByHandleW(h,out,n+1,FILE_NAME_NORMALIZED):0;
    CloseHandle(h);if(!used || used>n) {free(out);return NULL;}return out;
}
#endif
int cb_file_cache_contains(const char *directory,const char *path) {
#ifdef _WIN32
    wchar_t *root=cache_real_path(directory),*reference=cache_real_path(path);
    int result=1;
    if(root && reference) {
        size_t n=wcslen(root),m=wcslen(reference);
        result=m>=n && CompareStringOrdinal(root,(int)n,reference,(int)n,TRUE)==CSTR_EQUAL &&
            (reference[n]==0 || reference[n]==L'\\');
    }
#else
    char *root=realpath(directory,NULL),*reference=realpath(path,NULL);
    int result=1;
    if(root && reference) {
        size_t n=strlen(root);
        result=!strncmp(root,reference,n) && (reference[n]==0 || reference[n]=='/');
    }
#endif
    free(root);free(reference);return result;
}
/* 先检查整棵树，再删除；符号链接、重解析点或读取失败均保留缓存。 */
typedef struct {unsigned depth;int deleting;} cache_tree;
static int cache_tree_visit(const char *path,const char *name,void *context) {
    (void)name;cache_tree *tree=context;uint64_t size=0,stamp=0;
    int kind=path_kind(path,&size,&stamp);if(!kind || tree->depth>CB_FILE_DEPTH+1) return -1;
    if(kind==2) {
        cache_tree child={tree->depth+1,tree->deleting};
        if(cache_walk(path,cache_tree_visit,&child)!=0) return -1;
    }
    if(tree->deleting) {
        delete_path(path,kind==2);
        if(path_kind(path,&size,&stamp)) return -1;
    }return 0;
}
typedef struct {time_t now;int (*in_use)(const char *);unsigned removed;} cache_scan;
static int cache_candidate(const char *path,const char *name,void *context) {
    cache_scan *scan=context;if(!cache_name_valid(name)) return 0;
    uint64_t size=0,stamp=0;if(path_kind(path,&size,&stamp)!=2) return 0;
#ifdef _WIN32
    if(stamp<UINT64_C(116444736000000000)) return 0;
    uint64_t modified=(stamp-UINT64_C(116444736000000000))/10000000;
#else
    uint64_t modified=stamp/1000000000;
#endif
    if(scan->now<0 || modified>(uint64_t)scan->now || (uint64_t)scan->now-modified<86400) return 0;
    cache_tree tree={0,0};
    if(cache_tree_visit(path,name,&tree)!=0 || scan->in_use(path)!=0) return 0;
    tree.deleting=1;if(cache_tree_visit(path,name,&tree)==0) scan->removed++;
    return 0;
}
unsigned cb_file_cache_clean_at(const char *base,time_t now,int (*in_use)(const char *)) {
    if(!base || !in_use) return 0;
    cache_scan scan={now,in_use,0};(void)cache_walk(base,cache_candidate,&scan);return scan.removed;
}
unsigned cb_file_cache_clean(int (*in_use)(const char *)) {
#ifdef _WIN32
    wchar_t temp[MAX_PATH+1];DWORD n=GetTempPathW(MAX_PATH+1,temp);
    if(!n || n>MAX_PATH) return 0;
    int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,temp,-1,NULL,0,NULL,NULL);
    char *base=bytes>0?malloc((size_t)bytes):NULL;if(!base) return 0;
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,temp,-1,base,bytes,NULL,NULL)) {free(base);return 0;}
    unsigned removed=cb_file_cache_clean_at(base,time(NULL),in_use);free(base);return removed;
#else
    const char *base=getenv("TMPDIR");if(!base || !*base) base="/tmp";
    return cb_file_cache_clean_at(base,time(NULL),in_use);
#endif
}
