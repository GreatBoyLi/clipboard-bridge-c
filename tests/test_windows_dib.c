#include "../src/image_windows.h"
#include <assert.h>
#include <string.h>
static void put(unsigned char *p, uint32_t v) { p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24); }
int main(void) {
 unsigned char d[160]; cb_dib_layout l;
 memset(d,0,sizeof(d)); put(d,40); put(d+4,2); put(d+8,2); d[12]=1; d[14]=24;
 assert(cb_windows_dib_layout(d,56,&l)==0); assert(l.stride==8 && l.offset==40 && !l.top_down);
 assert(cb_windows_dib_layout(d,55,&l)==-1);
 put(d+8,0xfffffffeu); assert(cb_windows_dib_layout(d,56,&l)==0 && l.top_down);
 put(d+8,0x80000000u); assert(cb_windows_dib_layout(d,sizeof(d),&l)==-1);
 put(d+8,2); put(d+4,0xffffffffu); assert(cb_windows_dib_layout(d,sizeof(d),&l)==-1);
 put(d+4,2); d[14]=32; put(d+16,3); put(d+40,0x00ff0000u); put(d+44,0x0000ff00u); put(d+48,0x000000ffu);
 assert(cb_windows_dib_layout(d,68,&l)==0 && l.offset==52);
 assert(cb_windows_dib_layout(d,51,&l)==-1);
 put(d+40,0xffu); assert(cb_windows_dib_layout(d,68,&l)==-1);
 put(d+40,0x00ff0000u); put(d,124); put(d+112,124); put(d+116,4); assert(cb_windows_dib_layout(d,sizeof(d),&l)==-1);
 memset(d,0,sizeof(d)); put(d,40); put(d+4,16385); put(d+8,1); d[12]=1; d[14]=24;
 assert(cb_windows_dib_layout(d,sizeof(d),&l)==-1);
 put(d+4,2); put(d+8,2); put(d+32,256); assert(cb_windows_dib_layout(d,sizeof(d),&l)==-1);
 put(d+32,0); put(d+20,1); assert(cb_windows_dib_layout(d,56,&l)==-1);
 put(d+20,17); assert(cb_windows_dib_layout(d,56,&l)==-1);
 put(d+20,16); assert(cb_windows_dib_layout(d,56,&l)==0);
 /* 对任意损坏头部和每个截断长度检查解析器不会越界。 */
 uint32_t seed=1;
 for(unsigned run=0;run<100000;run++) {
  for(size_t i=0;i<sizeof(d);i++) { seed=seed*1664525u+1013904223u; d[i]=(unsigned char)(seed>>24); }
  (void)cb_windows_dib_layout(d,run%sizeof(d),&l);
 }
 return 0;
}
