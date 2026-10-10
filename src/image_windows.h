#ifndef CB_IMAGE_WINDOWS_H
#define CB_IMAGE_WINDOWS_H
#include "content.h"
#include <string.h>
/* 此边界解析不依赖 Windows SDK，便于在其它平台检查恶意 DIB。 */
typedef struct { uint32_t width, height; size_t offset, stride; unsigned bits; int top_down, alpha; } cb_dib_layout;
static inline uint32_t cb_dib_u32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static inline int cb_windows_dib_layout(const unsigned char *d, size_t n, cb_dib_layout *out) {
 uint32_t hs,w,h,comp,colors; size_t offset,stride; unsigned bits; int alpha=0;
 if (!d || !out || n<40) return -1;
 hs=cb_dib_u32(d); w=cb_dib_u32(d+4); h=cb_dib_u32(d+8); comp=cb_dib_u32(d+16); colors=cb_dib_u32(d+32);
 if ((hs!=40 && hs!=52 && hs!=56 && hs!=108 && hs!=124) || hs>n || !w || w>INT32_MAX || !h || h==0x80000000u || d[12]!=1 || d[13]!=0) return -1;
 out->top_down=(h>INT32_MAX); if(out->top_down) h=0u-h;
 if(w>16384u || h>16384u || (uint64_t)w*h>CB_MAX_PIXELS) return -1;
 bits=(unsigned)d[14] | ((unsigned)d[15]<<8); if ((bits!=24 && bits!=32) || (comp!=0 && comp!=3 && comp!=6) || (comp!=0 && bits!=32) || colors>256) return -1;
 offset=hs;
 if(comp!=0) {
  size_t masks=comp==6 ? 16u:12u;
  if(hs==40) { if(n-offset<masks) return -1; offset+=masks; }
  else if(hs<40u+masks) return -1;
  if(cb_dib_u32(d+40)!=0x00ff0000u || cb_dib_u32(d+44)!=0x0000ff00u || cb_dib_u32(d+48)!=0x000000ffu) return -1;
  if(comp==6 || hs>=56) { uint32_t a=cb_dib_u32(d+52); if(a!=0 && a!=0xff000000u) return -1; alpha=(a!=0); }
 }
 /* 嵌入色彩配置会改变 packed DIB 偏移；暂拒绝而不猜测位置。 */
 if(hs==124 && (cb_dib_u32(d+112)!=0 || cb_dib_u32(d+116)!=0)) return -1;
 if((size_t)colors*4>n-offset) return -1; offset+=(size_t)colors*4;
 stride=(((size_t)w*bits+31u)/32u)*4u;
 if((size_t)h>(n-offset)/stride) return -1;
 if(cb_dib_u32(d+20)!=0 && ((size_t)cb_dib_u32(d+20)<stride*h || (size_t)cb_dib_u32(d+20)>n-offset)) return -1;
 out->width=w; out->height=h; out->bits=bits; out->offset=offset; out->stride=stride; out->alpha=alpha; return 0;
}
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
int cb_windows_image_init(void);
void cb_windows_image_close(void);
int cb_windows_read_image(UINT png_format, unsigned char **png, size_t *length);
int cb_windows_prepare_image(const unsigned char *png, size_t length, HGLOBAL *png_handle, HGLOBAL *dib_handle);
#endif
#endif
