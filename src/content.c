#include "content.h"
#include "protocol.h"
#include <stdlib.h>
#include <string.h>
static uint32_t read32(const unsigned char *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void write32(unsigned char *p,uint32_t n) {
    p[0]=(unsigned char)(n>>24);p[1]=(unsigned char)(n>>16);
    p[2]=(unsigned char)(n>>8);p[3]=(unsigned char)n;
}
void cb_content_free(cb_content *c) {
    if(c) {free(c->text);free(c->png);memset(c,0,sizeof(*c));}
}
int cb_image_dimensions(uint32_t w,uint32_t h) {
    return w && h && w<=16384 && h<=16384 && (uint64_t)w*h<=CB_MAX_PIXELS;
}
/* 校验所有块的长度与 CRC，先限制尺寸，再允许原生解码器分配像素。
 * CRC 表只在栈中构造，不依赖额外压缩库，也没有跨线程全局状态。 */
int cb_png_valid(const unsigned char *p,size_t n) {
    static const unsigned char magic[]={137,80,78,71,13,10,26,10};
    if(!p || n<45 || n>CB_MAX_PNG || memcmp(p,magic,8)) return 0;
    uint32_t table[256];
    for(unsigned i=0;i<256;i++) {
        uint32_t c=i;
        for(unsigned j=0;j<8;j++) c=(c>>1)^((c&1)?0xedb88320u:0);
        table[i]=c;
    }
    size_t pos=8;int first=1,idat=0;
    while(n-pos>=12) {
        uint32_t len=read32(p+pos);
        if((size_t)len>n-pos-12) return 0;
        const unsigned char *type=p+pos+4,*body=p+pos+8;
        uint32_t crc=0xffffffffu;
        for(size_t j=0;j<(size_t)len+4;j++) crc=(crc>>8)^table[(crc^type[j])&255];
        if((crc^0xffffffffu)!=read32(body+len)) return 0;
        if(first) {
            if(len!=13 || memcmp(type,"IHDR",4) || !cb_image_dimensions(read32(body),read32(body+4))) return 0;
            first=0;
        } else if(!memcmp(type,"IHDR",4)) return 0;
        if(!memcmp(type,"IDAT",4)) idat=1;
        pos+=(size_t)len+12;
        if(!memcmp(type,"IEND",4)) return len==0 && idat && pos==n;
    }
    return 0;
}
/* Wire body: text-present (0/1), UTF-8 length, PNG length, text, PNG.
 * 所有长度是大端 uint32；空文本和没有文本用独立标记区分。 */
int cb_content_pack(const cb_content *c,unsigned char **out,size_t *n) {
    *out=NULL;*n=0;
    if(!c || c->text_length>CB_MAX_TEXT || (!c->text && c->text_length) ||
       !cb_valid_utf8((const unsigned char *)c->text,c->text_length) || !cb_png_valid(c->png,c->png_length)) return -1;
    size_t size=CB_CONTENT_META+c->text_length+c->png_length;
    unsigned char *p=malloc(size);if(!p) return -1;
    write32(p,c->text!=NULL);write32(p+4,(uint32_t)c->text_length);write32(p+8,(uint32_t)c->png_length);
    if(c->text_length) memcpy(p+12,c->text,c->text_length);
    memcpy(p+12+c->text_length,c->png,c->png_length);
    *out=p;*n=size;return 0;
}
int cb_content_unpack(const unsigned char *p,size_t n,cb_content *c) {
    memset(c,0,sizeof(*c));
    if(!p || n<12) return -1;
    uint32_t has=read32(p),text=read32(p+4),png=read32(p+8);
    if(has>1 || (!has && text) || text>CB_MAX_TEXT || png>CB_MAX_PNG ||
       (size_t)text+png!=n-12 || !cb_valid_utf8(p+12,text) || !cb_png_valid(p+12+text,png)) return -1;
    if(has) {
        c->text=malloc((size_t)text+1);if(!c->text) return -1;
        memcpy(c->text,p+12,text);c->text[text]=0;c->text_length=text;
    }
    c->png=malloc(png);if(!c->png) {cb_content_free(c);return -1;}
    memcpy(c->png,p+12+text,png);c->png_length=png;return 0;
}
