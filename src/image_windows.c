/* WIC 负责 PNG 编解码；剪贴板句柄仅在打开期间借用。 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include "image_windows.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static IWICImagingFactory *factory;
static int com_initialized;
int cb_windows_image_init(void) {
 HRESULT hr=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
 if(FAILED(hr)) return -1;
 com_initialized=1;
 hr=CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void **)&factory);
 if(FAILED(hr)) { cb_windows_image_close(); return -1; }
 return 0;
}
void cb_windows_image_close(void) {
 if(factory) { IWICImagingFactory_Release(factory); factory=NULL; }
 if(com_initialized) { CoUninitialize(); com_initialized=0; }
}
static HGLOBAL memory_copy(const unsigned char *data,size_t length) {
 HGLOBAL h=GlobalAlloc(GMEM_MOVEABLE,length); void *p;
 if(!h) return NULL;
 p=GlobalLock(h); if(!p) { GlobalFree(h); return NULL; }
 memcpy(p,data,length); GlobalUnlock(h); return h;
}
/* 在 CopyPixels 和像素分配前读取尺寸并检查总像素上限。 */
static int decode_png(const unsigned char *png,size_t length,unsigned char **pixels,UINT *width,UINT *height) {
 HGLOBAL memory=NULL; IStream *stream=NULL; IWICBitmapDecoder *decoder=NULL;
 IWICBitmapFrameDecode *frame=NULL; IWICFormatConverter *converter=NULL; HRESULT hr=E_FAIL; size_t bytes;
 *pixels=NULL;
 if(!factory || !cb_png_valid(png,length)) return -1;
 memory=memory_copy(png,length); if(!memory) return -1;
 hr=CreateStreamOnHGlobal(memory,TRUE,&stream);
 if(FAILED(hr)) { GlobalFree(memory); return -1; }
 hr=IWICImagingFactory_CreateDecoderFromStream(factory,stream,NULL,WICDecodeMetadataCacheOnDemand,&decoder);
 if(FAILED(hr)) goto done;
 hr=IWICBitmapDecoder_GetFrame(decoder,0,&frame); if(FAILED(hr)) goto done;
 hr=IWICBitmapFrameDecode_GetSize(frame,width,height);
 if(FAILED(hr) || !cb_image_dimensions(*width,*height)) { hr=E_INVALIDARG; goto done; }
 bytes=(size_t)*width * *height * 4u;
 if(bytes>UINT_MAX) { hr=E_INVALIDARG; goto done; }
 hr=IWICImagingFactory_CreateFormatConverter(factory,&converter); if(FAILED(hr)) goto done;
 hr=IWICFormatConverter_Initialize(converter,(IWICBitmapSource *)frame,&GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,NULL,0.0,WICBitmapPaletteTypeCustom);
 if(FAILED(hr)) goto done;
 *pixels=malloc(bytes); if(!*pixels) { hr=E_OUTOFMEMORY; goto done; }
 hr=IWICFormatConverter_CopyPixels(converter,NULL,*width*4u,(UINT)bytes,*pixels);
 done:
 if(converter) IWICFormatConverter_Release(converter);
 if(frame) IWICBitmapFrameDecode_Release(frame);
 if(decoder) IWICBitmapDecoder_Release(decoder);
 IStream_Release(stream);
 if(FAILED(hr)) { free(*pixels); *pixels=NULL; return -1; }
 return 0;
}
static int encode_png(const unsigned char *pixels,UINT width,UINT height,unsigned char **png,size_t *length) {
 IStream *stream=NULL; IWICBitmapEncoder *encoder=NULL; IWICBitmapFrameEncode *frame=NULL;
 IPropertyBag2 *properties=NULL; WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
 HRESULT hr; HGLOBAL memory; STATSTG stat; const void *p;
 *png=NULL; *length=0;
 if(!factory || !cb_image_dimensions(width,height)) return -1;
 hr=CreateStreamOnHGlobal(NULL,TRUE,&stream); if(FAILED(hr)) return -1;
 hr=IWICImagingFactory_CreateEncoder(factory,&GUID_ContainerFormatPng,NULL,&encoder); if(FAILED(hr)) goto done;
 hr=IWICBitmapEncoder_Initialize(encoder,stream,WICBitmapEncoderNoCache); if(FAILED(hr)) goto done;
 hr=IWICBitmapEncoder_CreateNewFrame(encoder,&frame,&properties); if(FAILED(hr)) goto done;
 hr=IWICBitmapFrameEncode_Initialize(frame,properties); if(FAILED(hr)) goto done;
 hr=IWICBitmapFrameEncode_SetSize(frame,width,height); if(FAILED(hr)) goto done;
 hr=IWICBitmapFrameEncode_SetPixelFormat(frame,&format);
 if(FAILED(hr) || !IsEqualGUID(&format,&GUID_WICPixelFormat32bppBGRA)) { hr=E_FAIL; goto done; }
 hr=IWICBitmapFrameEncode_WritePixels(frame,height,width*4u,width*height*4u,(BYTE *)pixels); if(FAILED(hr)) goto done;
 hr=IWICBitmapFrameEncode_Commit(frame); if(FAILED(hr)) goto done;
 hr=IWICBitmapEncoder_Commit(encoder); if(FAILED(hr)) goto done;
 hr=IStream_Stat(stream,&stat,STATFLAG_NONAME); if(FAILED(hr)) goto done;
 if(stat.cbSize.HighPart || stat.cbSize.LowPart>CB_MAX_PNG || !stat.cbSize.LowPart) { hr=E_INVALIDARG; goto done; }
 hr=GetHGlobalFromStream(stream,&memory); if(FAILED(hr)) goto done;
 p=GlobalLock(memory); if(!p) { hr=E_FAIL; goto done; }
 *length=(size_t)stat.cbSize.LowPart; *png=malloc(*length);
 if(*png) memcpy(*png,p,*length); else hr=E_OUTOFMEMORY;
 GlobalUnlock(memory);
 done:
 if(properties) IPropertyBag2_Release(properties);
 if(frame) IWICBitmapFrameEncode_Release(frame);
 if(encoder) IWICBitmapEncoder_Release(encoder);
 IStream_Release(stream);
 if(FAILED(hr)) { free(*png); *png=NULL; *length=0; return -1; }
 return 0;
}
static int read_dib(UINT format,unsigned char **png,size_t *length) {
 HGLOBAL handle=(HGLOBAL)GetClipboardData(format); const unsigned char *d;
 cb_dib_layout layout; unsigned char *pixels; size_t n; int result;
 if(!handle) return -1;
 n=(size_t)GlobalSize(handle); d=GlobalLock(handle); if(!d) return -1;
 if(cb_windows_dib_layout(d,n,&layout)!=0) { GlobalUnlock(handle); return -1; }
 pixels=malloc((size_t)layout.width*layout.height*4u);
 if(!pixels) { GlobalUnlock(handle); return -1; }
 for(uint32_t y=0;y<layout.height;++y) {
  uint32_t sy=layout.top_down ? y:layout.height-1u-y;
  const unsigned char *row=d+layout.offset+(size_t)sy*layout.stride;
  unsigned char *out=pixels+(size_t)y*layout.width*4u;
  for(uint32_t x=0;x<layout.width;++x) {
   size_t s=(size_t)x*(layout.bits/8u),t=(size_t)x*4u;
   out[t]=row[s]; out[t+1]=row[s+1]; out[t+2]=row[s+2];
   /* BI_RGB 的保留字节常为零；只有显式 alpha mask 才解释透明度。 */
   out[t+3]=layout.alpha ? row[s+3]:255;
  }
 }
 GlobalUnlock(handle); result=encode_png(pixels,layout.width,layout.height,png,length); free(pixels); return result;
}
static int read_bitmap(unsigned char **png,size_t *length) {
 HBITMAP bitmap=(HBITMAP)GetClipboardData(CF_BITMAP); BITMAP info; BITMAPINFO dib;
 HDC dc; unsigned char *pixels; size_t bytes; int result;
 if(!bitmap || GetObjectW(bitmap,(int)sizeof(info),&info)!=(int)sizeof(info) || info.bmWidth<=0 || info.bmHeight<=0 || !cb_image_dimensions((uint32_t)info.bmWidth,(uint32_t)info.bmHeight)) return -1;
 memset(&dib,0,sizeof(dib)); dib.bmiHeader.biSize=(DWORD)sizeof(BITMAPINFOHEADER);
 dib.bmiHeader.biWidth=info.bmWidth; dib.bmiHeader.biHeight=-info.bmHeight;
 dib.bmiHeader.biPlanes=1; dib.bmiHeader.biBitCount=32; dib.bmiHeader.biCompression=BI_RGB;
 bytes=(size_t)info.bmWidth*(size_t)info.bmHeight*4u; pixels=malloc(bytes); if(!pixels) return -1;
 dc=GetDC(NULL); if(!dc) { free(pixels); return -1; }
 result=GetDIBits(dc,bitmap,0,(UINT)info.bmHeight,pixels,&dib,DIB_RGB_COLORS);
 ReleaseDC(NULL,dc);
 if(result!=info.bmHeight) { free(pixels); return -1; }
 for(size_t i=3;i<bytes;i+=4) pixels[i]=255;
 result=encode_png(pixels,(UINT)info.bmWidth,(UINT)info.bmHeight,png,length); free(pixels); return result;
}
/* GlobalSize 可能包括分配器填充；PNG 的 IEND 给出实际格式长度。 */
static size_t clipboard_png_length(const unsigned char *p,size_t capacity) {
 size_t pos=8;
 static const unsigned char signature[]={137,80,78,71,13,10,26,10};
 if(capacity<8 || memcmp(p,signature,8)) return 0;
 if(capacity>CB_MAX_PNG) capacity=CB_MAX_PNG;
 while(pos<=capacity && capacity-pos>=12) {
  uint32_t n=((uint32_t)p[pos]<<24)|((uint32_t)p[pos+1]<<16)|((uint32_t)p[pos+2]<<8)|p[pos+3];
  if((size_t)n>capacity-pos-12) return 0;
  if(memcmp(p+pos+4,"IEND",4)==0) return n==0 ? pos+12:0;
  pos+=(size_t)n+12;
 }
 return 0;
}
int cb_windows_read_image(UINT png_format,unsigned char **png,size_t *length) {
 *png=NULL; *length=0;
 if(IsClipboardFormatAvailable(png_format)) {
  HGLOBAL h=(HGLOBAL)GetClipboardData(png_format); const unsigned char *p; size_t n; unsigned char *pixels=NULL; UINT w=0,height=0;
  if(!h) goto invalid;
  n=(size_t)GlobalSize(h); if(!n) goto invalid;
  p=GlobalLock(h); if(!p) goto invalid;
  n=clipboard_png_length(p,n);
  if(!n) { GlobalUnlock(h); goto invalid; }
  /* 同时进行实际解码，拒绝结构合法但压缩数据损坏的 PNG。 */
  if(decode_png(p,n,&pixels,&w,&height)!=0) { GlobalUnlock(h); goto invalid; }
  free(pixels); *png=malloc(n);
  if(*png) { memcpy(*png,p,n); *length=n; }
  GlobalUnlock(h); if(!*png) goto invalid;
  return 1;
 }
 if(IsClipboardFormatAvailable(CF_DIBV5)) { if(read_dib(CF_DIBV5,png,length)!=0) goto invalid; return 1; }
 if(IsClipboardFormatAvailable(CF_DIB)) { if(read_dib(CF_DIB,png,length)!=0) goto invalid; return 1; }
 if(IsClipboardFormatAvailable(CF_BITMAP)) { if(read_bitmap(png,length)!=0) goto invalid; return 1; }
 return 0;
 invalid:
 fprintf(stderr,"clipboard image rejected: invalid, unsupported or oversized image\n"); return -1;
}
int cb_windows_prepare_image(const unsigned char *png,size_t length,HGLOBAL *png_handle,HGLOBAL *dib_handle) {
 unsigned char *pixels=NULL; UINT width=0,height=0; size_t bytes; BITMAPV5HEADER *header;
 *png_handle=NULL; *dib_handle=NULL;
 if(decode_png(png,length,&pixels,&width,&height)!=0) return -1;
 bytes=(size_t)width*height*4u;
 *png_handle=memory_copy(png,length);
 *dib_handle=GlobalAlloc(GMEM_MOVEABLE,sizeof(BITMAPV5HEADER)+bytes);
 if(!*png_handle || !*dib_handle) goto invalid;
 header=GlobalLock(*dib_handle); if(!header) goto invalid;
 memset(header,0,sizeof(*header)); header->bV5Size=(DWORD)sizeof(*header);
 header->bV5Width=(LONG)width; header->bV5Height=-(LONG)height;
 header->bV5Planes=1; header->bV5BitCount=32; header->bV5Compression=BI_BITFIELDS;
 header->bV5SizeImage=(DWORD)bytes; header->bV5RedMask=0x00ff0000u; header->bV5GreenMask=0x0000ff00u;
 header->bV5BlueMask=0x000000ffu; header->bV5AlphaMask=0xff000000u; header->bV5CSType=LCS_sRGB;
 memcpy((unsigned char *)header+sizeof(*header),pixels,bytes);
 GlobalUnlock(*dib_handle); free(pixels); return 0;
 invalid:
 if(*png_handle) GlobalFree(*png_handle);
 if(*dib_handle) GlobalFree(*dib_handle);
 *png_handle=NULL; *dib_handle=NULL; free(pixels); return -1;
}
