/* WIC 内存编解码测试；不打开、不读取、不清理系统剪贴板。 */
#include <assert.h>
#include <stdio.h>
#include "image_fixture.h"
#include "../src/image_windows.c"
int main(void)
{
    unsigned char *pixels=NULL,*encoded=NULL,*decoded=NULL;
    size_t length=0;
    UINT width=0,height=0;
    HGLOBAL png_handle=NULL,dib_handle=NULL;
    assert(cb_windows_image_init()==0);
    assert(decode_png(cb_test_png,sizeof(cb_test_png),&pixels,&width,&height)==0);
    assert(width==1 && height==1 && !pixels[0] && !pixels[1] && !pixels[2] && !pixels[3]);
    free(pixels);
    /* 非透明像素及半透明像素必须无损往返，不能丢失 alpha。 */
    const unsigned char sample[]={12,34,56,255,78,90,123,128};
    assert(encode_png(sample,2,1,&encoded,&length)==0);
    assert(cb_png_valid(encoded,length));
    assert(decode_png(encoded,length,&decoded,&width,&height)==0);
    assert(width==2 && height==1 && !memcmp(decoded,sample,sizeof(sample)));
    assert(cb_windows_prepare_image(encoded,length,&png_handle,&dib_handle)==0);
    const unsigned char *dib=GlobalLock(dib_handle);cb_dib_layout layout;
    assert(dib && cb_windows_dib_layout(dib,(size_t)GlobalSize(dib_handle),&layout)==0);
    assert(layout.width==2 && layout.height==1 && layout.top_down && layout.alpha);
    assert(!memcmp(dib+layout.offset,sample,sizeof(sample)));
    GlobalUnlock(dib_handle);GlobalFree(dib_handle);GlobalFree(png_handle);
    encoded[20]^=1;
    png_handle=dib_handle=NULL;
    assert(cb_windows_prepare_image(encoded,length,&png_handle,&dib_handle)==-1);
    assert(!png_handle && !dib_handle);
    free(encoded);free(decoded);cb_windows_image_close();
    puts("Windows WIC PNG and alpha roundtrip tests passed");
    return 0;
}
