#include "content.h"
#include "protocol.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/* 一个完整、透明的 1x1 PNG。 */
static const unsigned char png[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,122,94,171,63,0,0,0,0,73,69,78,68,174,66,96,130};
int main(void) {
    assert(cb_png_valid(png,sizeof(png)));
    assert(!cb_png_valid(png,24));
    assert(!cb_image_dimensions(0,1));
    assert(!cb_image_dimensions(16384,16384));
    cb_content c={"中文🙂",10,(unsigned char *)png,sizeof(png),NULL,0}, out={0};
    unsigned char *bytes=NULL;size_t n=0;
    assert(cb_content_pack(&c,&bytes,&n)==0);
    assert(cb_content_unpack(bytes,n,&out)==0);
    assert(out.text_length==10 && memcmp(out.text,c.text,10)==0);
    assert(out.png_length==sizeof(png) && memcmp(out.png,png,sizeof(png))==0);
    cb_content_free(&out);
    assert(cb_content_unpack(bytes,n-1,&out)<0);
    bytes[0]=2; assert(cb_content_unpack(bytes,n,&out)<0); free(bytes);
    c.text="";c.text_length=0;
    assert(cb_content_pack(&c,&bytes,&n)==0);
    assert(cb_content_unpack(bytes,n,&out)==0 && out.text!=NULL && !out.text_length);
    cb_content_free(&out);free(bytes);
    c.text=NULL;
    assert(cb_content_pack(&c,&bytes,&n)==0);
    assert(cb_content_unpack(bytes,n,&out)==0 && out.text==NULL);
    cb_content_free(&out);free(bytes);
    c.png_length=CB_MAX_PNG+1;assert(cb_content_pack(&c,&bytes,&n)<0);
    unsigned char header[CB_HEADER_SIZE];cb_header h={CB_IMAGE_BEGIN,1,12}, decoded;
    cb_encode_header(header,&h);assert(cb_decode_header(header,&decoded)==0);
    h.length=13;cb_encode_header(header,&h);assert(cb_decode_header(header,&decoded)<0);
    h.type=CB_IMAGE_CHUNK;h.length=CB_CONTENT_CHUNK_SIZE+1;
    cb_encode_header(header,&h);assert(cb_decode_header(header,&decoded)<0);
    h.length=1;h.sequence=0;cb_encode_header(header,&h);assert(cb_decode_header(header,&decoded)<0);
    h.type=CB_IMAGE_CREDIT;h.sequence=1;h.length=0;
    cb_encode_header(header,&h);assert(cb_decode_header(header,&decoded)==0);
    unsigned char corrupt[sizeof(png)];memcpy(corrupt,png,sizeof(png));corrupt[33]^=1;
    assert(!cb_png_valid(corrupt,sizeof(corrupt)));
    puts("Image and compound content tests passed");
}
