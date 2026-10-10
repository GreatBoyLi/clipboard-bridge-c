#import <Foundation/Foundation.h>
#import <IOKit/hid/IOHIDManager.h>
#import <IOKit/hidsystem/IOHIDLib.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <pthread.h>
#include "caps_hid.h"
/* 此模块只监听 Caps Lock 的物理边沿，不抓取普通键内容、不独占键盘。 */
static IOHIDManagerRef caps_manager;
static CFRunLoopRef caps_loop;
static cb_caps_hid_callback caps_callback;
static mach_timebase_info_data_t caps_timebase;
static int caps_scheduled;
static pthread_once_t caps_clock_once=PTHREAD_ONCE_INIT;
static void caps_clock_init(void) { mach_timebase_info(&caps_timebase); }
static uint64_t ticks_ms(uint64_t ticks)
{
    pthread_once(&caps_clock_once,caps_clock_init);
    /* 先拆整除部分，避免绝对时间乘换算比例后溢出。 */
    uint64_t nanos=(ticks/caps_timebase.denom)*caps_timebase.numer+
        (ticks%caps_timebase.denom)*caps_timebase.numer/caps_timebase.denom;
    return nanos/1000000u;
}
uint64_t cb_caps_hid_now(void) {return ticks_ms(mach_absolute_time());}
static void caps_value(void *context,IOReturn result,void *sender,IOHIDValueRef value)
{
    (void)context;(void)sender;
    if(result!=kIOReturnSuccess || !value || !caps_callback) return;
    IOHIDElementRef element=IOHIDValueGetElement(value);
    if(!element || IOHIDElementGetUsagePage(element)!=7 || IOHIDElementGetUsage(element)!=0x39) return;
    IOHIDDeviceRef device=IOHIDElementGetDevice(element);
    uint64_t time=IOHIDValueGetTimeStamp(value);
    caps_callback(IOHIDValueGetIntegerValue(value)!=0,(uintptr_t)device,time?ticks_ms(time):cb_caps_hid_now());
}
static void caps_removed(void *context,IOReturn result,void *sender,IOHIDDeviceRef device)
{
    (void)context;(void)sender;
    if(result==kIOReturnSuccess && caps_callback) caps_callback(-1,(uintptr_t)device,cb_caps_hid_now());
}
void cb_caps_hid_stop(void)
{
    if(caps_manager) {
        IOHIDManagerRegisterInputValueCallback(caps_manager,NULL,NULL);
        IOHIDManagerRegisterDeviceRemovalCallback(caps_manager,NULL,NULL);
        if(caps_scheduled) IOHIDManagerUnscheduleFromRunLoop(caps_manager,caps_loop,kCFRunLoopCommonModes);
        IOHIDManagerClose(caps_manager,kIOHIDOptionsTypeNone);
        CFRelease(caps_manager);caps_manager=NULL;
    }
    if(caps_loop) {CFRelease(caps_loop);caps_loop=NULL;}
    caps_callback=NULL;caps_scheduled=0;
}
int cb_caps_hid_start(cb_caps_hid_callback callback)
{
    if(caps_manager || !callback) return -1;
    if(@available(macOS 10.15,*)) {
        if(IOHIDCheckAccess(kIOHIDRequestTypeListenEvent)!=kIOHIDAccessTypeGranted) {
            fprintf(stderr,"Caps Lock mapping needs macOS Input Monitoring permission for the terminal.\n");return -1;
        }
    }
    caps_manager=IOHIDManagerCreate(kCFAllocatorDefault,kIOHIDOptionsTypeNone);
    if(!caps_manager) return -1;
    NSDictionary *match=@{@kIOHIDDeviceUsagePageKey:@1,@kIOHIDDeviceUsageKey:@6};
    IOHIDManagerSetDeviceMatching(caps_manager,(__bridge CFDictionaryRef)match);
    caps_callback=callback;
    IOHIDManagerRegisterInputValueCallback(caps_manager,caps_value,NULL);
    IOHIDManagerRegisterDeviceRemovalCallback(caps_manager,caps_removed,NULL);
    if(IOHIDManagerOpen(caps_manager,kIOHIDOptionsTypeNone)!=kIOReturnSuccess) {
        fprintf(stderr,"Cannot open Caps Lock HID listener; check Input Monitoring permission.\n");
        cb_caps_hid_stop();return -1;
    }
    caps_loop=(CFRunLoopRef)CFRetain(CFRunLoopGetCurrent());
    IOHIDManagerScheduleWithRunLoop(caps_manager,caps_loop,kCFRunLoopCommonModes);
    caps_scheduled=1;return 0;
}
