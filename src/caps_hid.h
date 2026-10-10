#ifndef CB_CAPS_HID_H
#define CB_CAPS_HID_H
#include <stdint.h>
/* callback 只接收 Caps Lock；down=-1 表示该设备移除，需要取消按住。 */
typedef void (*cb_caps_hid_callback)(int down,uintptr_t device,uint64_t time_ms);
int cb_caps_hid_start(cb_caps_hid_callback callback);
void cb_caps_hid_stop(void);
uint64_t cb_caps_hid_now(void);
#endif
