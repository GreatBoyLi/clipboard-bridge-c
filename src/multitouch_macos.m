#import <CoreFoundation/CoreFoundation.h>
#include "multitouch.h"
#include "touch_swipe.h"
#include "touch_motion.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* 私有 MultitouchSupport 的已知 96-byte 触点 ABI。
 * 仅把该布局转换成纯 C 触点，不把私有类型暴露给键盘/剪贴板模块。
 * ABI 参考：https://github.com/calftrail/TrackMagic/blob/master/MultitouchSupport.h
 * macOS 升级可能改变它；运行时也验证数量、状态和归一化坐标。
 */
typedef struct { float x, y; } mt_xy;
typedef struct { mt_xy position, velocity; } mt_vector;
typedef struct {
    int32_t frame;
    double timestamp;
    int32_t identifier, state, finger, hand;
    mt_vector normalized;
    float quality;
    int32_t reserved1;
    float angle, major, minor;
    mt_vector absolute;
    int32_t reserved2, reserved3;
    float density;
} mt_contact;
_Static_assert(sizeof(mt_contact) == 96, "Unexpected multitouch contact ABI");
_Static_assert(offsetof(mt_contact, normalized) == 32, "Unexpected touch coordinate offset");
typedef const void *mt_device;
typedef void (*mt_callback)(mt_device, mt_contact *, size_t, double, size_t);
typedef CFArrayRef (*mt_list_fn)(void);
typedef void (*mt_register_fn)(mt_device, mt_callback);
typedef int (*mt_start_fn)(mt_device, int);
typedef int (*mt_stop_fn)(mt_device);

typedef struct { mt_device device; cb_touch_state gesture; cb_motion_state motion; CFAbsoluteTime motion_received; unsigned logs; int last_count, started; } device_state;
static pthread_mutex_t touch_mutex = PTHREAD_MUTEX_INITIALIZER;
static void *library;
static CFArrayRef devices;
static device_state *slots;
static CFIndex slot_count;
static int accepting, debug_enabled;
static pid_t (*get_context)(void);
static void (*send_navigation)(unsigned short, pid_t);
static mt_register_fn register_frame, unregister_frame;
static mt_start_fn start_device;
static mt_stop_fn stop_device;

static void contact_frame(mt_device device, mt_contact *contacts, size_t count, double time, size_t frame)
{
    (void)frame;
    pthread_mutex_lock(&touch_mutex);
    if (!accepting) { pthread_mutex_unlock(&touch_mutex); return; }
    device_state *slot = NULL;
    for (CFIndex i = 0; i < slot_count; ++i) if (slots[i].device == device) { slot = &slots[i]; break; }
    if (!slot) { pthread_mutex_unlock(&touch_mutex); return; }
    cb_touch_point points[4];
    int n = 0;
    if (count > 32 || (count && !contacts)) {
        slot->gesture.phase = 2;
        memset(&slot->motion,0,sizeof(slot->motion));
        pthread_mutex_unlock(&touch_mutex); return;
    }
    /* Hover/抬起残留状态不算按在触控板上的手指。四指以上只计数，不处理坐标。 */
    for (size_t i = 0; i < count; ++i) {
        if (contacts[i].state != 3 && contacts[i].state != 4) continue;
        if (n < 4) points[n] = (cb_touch_point){contacts[i].identifier,
            contacts[i].normalized.position.x, contacts[i].normalized.position.y};
        ++n;
    }
    if (debug_enabled && n != slot->last_count && slot->logs++ < 24) {
        fprintf(stderr, "[trackpad] raw contacts: fingers=%d\n", n);
        slot->last_count = n;
    }
    pid_t pid = (n == 2 || n == 3) ? get_context() : 0;
    if (n == 2) {
        cb_motion_step(&slot->motion, points[0].id, points[1].id,
            (points[0].y+points[1].y)/2.0,time,(unsigned)pid);
        slot->motion_received=CFAbsoluteTimeGetCurrent();
    } else memset(&slot->motion,0,sizeof(slot->motion));
    cb_touch_result result = cb_touch_step(&slot->gesture, points, n, time, (unsigned)pid);
    if (result.key) {
        if (debug_enabled) fprintf(stderr, "[trackpad] raw three-finger swipe recognized: %s\n", result.key == 123 ? "back" : "forward");
        send_navigation(result.key, (pid_t)result.pid);
    }
    pthread_mutex_unlock(&touch_mutex);
}

int cb_touch_start(pid_t (*context)(void), void (*navigate)(unsigned short, pid_t), int debug)
{
    if (devices || !context || !navigate) return -1;
    /* 动态加载：不启用此功能时既不依赖私有框架，也不监听底层触点。 */
    if (!library) library = dlopen("/System/Library/PrivateFrameworks/MultitouchSupport.framework/MultitouchSupport", RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "Cannot load MultitouchSupport.\n"); return -1; }
    mt_list_fn create_list = (mt_list_fn)dlsym(library, "MTDeviceCreateList");
    register_frame = (mt_register_fn)dlsym(library, "MTRegisterContactFrameCallback");
    unregister_frame = (mt_register_fn)dlsym(library, "MTUnregisterContactFrameCallback");
    start_device = (mt_start_fn)dlsym(library, "MTDeviceStart");
    stop_device = (mt_stop_fn)dlsym(library, "MTDeviceStop");
    if (!create_list || !register_frame || !unregister_frame || !start_device || !stop_device) {
        fprintf(stderr, "MultitouchSupport API unavailable on this macOS version.\n"); return -1;
    }
    devices = create_list();
    slot_count = devices ? CFArrayGetCount(devices) : 0;
    if (slot_count <= 0 || slot_count > 32) { fprintf(stderr, "No supported multitouch devices.\n"); cb_touch_stop(); return -1; }
    slots = calloc((size_t)slot_count, sizeof(*slots));
    if (!slots) { cb_touch_stop(); return -1; }
    for (CFIndex i = 0; i < slot_count; ++i) {
        slots[i].device = CFArrayGetValueAtIndex(devices, i);
        slots[i].last_count = -1;
    }
    pthread_mutex_lock(&touch_mutex);
    get_context = context; send_navigation = navigate; debug_enabled = debug; accepting = 1;
    pthread_mutex_unlock(&touch_mutex);
    for (CFIndex i = 0; i < slot_count; ++i) {
        register_frame(slots[i].device, contact_frame);
        if (start_device(slots[i].device, 0) != 0) {
            unregister_frame(slots[i].device, contact_frame);
            fprintf(stderr, "Cannot start multitouch device listener.\n");
            cb_touch_stop(); return -1;
        }
        slots[i].started = 1;
    }
    if (debug) fprintf(stderr, "[trackpad] raw multitouch listener started: devices=%ld\n", (long)slot_count);
    return 0;
}
void cb_touch_stop(void)
{
    /* 先禁止交接，再停设备回调；不能持锁等待系统线程，否则可能互相等待。 */
    pthread_mutex_lock(&touch_mutex); accepting = 0; pthread_mutex_unlock(&touch_mutex);
    if (slots) for (CFIndex i = 0; i < slot_count; ++i) if (slots[i].started) {
        stop_device(slots[i].device);
        unregister_frame(slots[i].device, contact_frame);
    }
    pthread_mutex_lock(&touch_mutex);
    free(slots); slots = NULL; slot_count = 0;
    if (devices) { CFRelease(devices); devices = NULL; }
    get_context = NULL; send_navigation = NULL;
    pthread_mutex_unlock(&touch_mutex);
    /* 保留 dlopen 句柄至进程退出，避免尚在返回的系统回调访问已卸载代码。 */
}

int cb_touch_speed(pid_t pid,double *speed)
{
    if(!speed || pid<=0) return 0;
    int found=0; CFAbsoluteTime newest=0,now=CFAbsoluteTimeGetCurrent();
    pthread_mutex_lock(&touch_mutex);
    if(accepting) for(CFIndex i=0;i<slot_count;++i) {
        device_state *slot=&slots[i];
        if(slot->motion.valid && slot->motion.pid==(unsigned)pid &&
           now-slot->motion_received<=.1 && slot->motion_received>newest) {
            *speed=slot->motion.speed;newest=slot->motion_received;found=1;
        }
    }
    pthread_mutex_unlock(&touch_mutex);return found;
}
