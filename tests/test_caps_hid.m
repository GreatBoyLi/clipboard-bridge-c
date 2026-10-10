#import <Foundation/Foundation.h>
#import <IOKit/hid/IOHIDManager.h>
#import <IOKit/hidsystem/IOHIDLib.h>
#include <assert.h>
#include <mach/mach_time.h>
static int access_ok=1,open_ok=1,creates,scheduled,closed,delivered;
static uint32_t usage=0x39,page=7;
static CFIndex value=1;
static IOHIDValueCallback value_callback;
static IOHIDDeviceCallback remove_callback;
static IOHIDManagerRef fake_create(CFAllocatorRef a,IOOptionBits o) {(void)a;(void)o;creates++;return (IOHIDManagerRef)CFDictionaryCreate(NULL,NULL,NULL,0,NULL,NULL);}
static void fake_match(IOHIDManagerRef m,CFDictionaryRef d) {(void)m;assert(d);}
static void fake_values(IOHIDManagerRef m,IOHIDValueCallback cb,void *ctx) {(void)m;(void)ctx;value_callback=cb;}
static void fake_remove(IOHIDManagerRef m,IOHIDDeviceCallback cb,void *ctx) {(void)m;(void)ctx;remove_callback=cb;}
static IOReturn fake_open(IOHIDManagerRef m,IOOptionBits o) {(void)m;assert(o==kIOHIDOptionsTypeNone);return open_ok?kIOReturnSuccess:kIOReturnNotPermitted;}
static IOReturn fake_close(IOHIDManagerRef m,IOOptionBits o) {(void)m;(void)o;closed++;return kIOReturnSuccess;}
static void fake_schedule(IOHIDManagerRef m,CFRunLoopRef l,CFStringRef mode) {(void)m;assert(l && mode);scheduled++;}
static void fake_unschedule(IOHIDManagerRef m,CFRunLoopRef l,CFStringRef mode) {(void)m;(void)l;(void)mode;scheduled--;}
static IOHIDAccessType fake_access(IOHIDRequestType t) {(void)t;return access_ok?kIOHIDAccessTypeGranted:kIOHIDAccessTypeDenied;}
static IOHIDElementRef fake_element(IOHIDValueRef v) {(void)v;return (IOHIDElementRef)(uintptr_t)1;}
static uint32_t fake_usage(IOHIDElementRef e) {(void)e;return usage;}
static uint32_t fake_page(IOHIDElementRef e) {(void)e;return page;}
static IOHIDDeviceRef fake_device(IOHIDElementRef e) {(void)e;return (IOHIDDeviceRef)(uintptr_t)2;}
static CFIndex fake_value(IOHIDValueRef v) {(void)v;return value;}
static uint64_t fake_time(IOHIDValueRef v) {(void)v;return mach_absolute_time();}
#define IOHIDManagerCreate fake_create
#define IOHIDManagerSetDeviceMatching fake_match
#define IOHIDManagerRegisterInputValueCallback fake_values
#define IOHIDManagerRegisterDeviceRemovalCallback fake_remove
#define IOHIDManagerOpen fake_open
#define IOHIDManagerClose fake_close
#define IOHIDManagerScheduleWithRunLoop fake_schedule
#define IOHIDManagerUnscheduleFromRunLoop fake_unschedule
#define IOHIDCheckAccess fake_access
#define IOHIDValueGetElement fake_element
#define IOHIDElementGetUsage fake_usage
#define IOHIDElementGetUsagePage fake_page
#define IOHIDElementGetDevice fake_device
#define IOHIDValueGetIntegerValue fake_value
#define IOHIDValueGetTimeStamp fake_time
#include "../src/caps_hid_macos.m"
static void received(int down,uintptr_t device,uint64_t now) {assert(device==2 && now>0 && down>=-1 && down<=1);delivered++;}
int main(void) { @autoreleasepool {
    access_ok=0;assert(cb_caps_hid_start(received)<0 && !creates);
    access_ok=1;open_ok=0;assert(cb_caps_hid_start(received)<0 && !scheduled);
    open_ok=1;assert(cb_caps_hid_start(received)==0 && scheduled==1 && value_callback && remove_callback);
    value_callback(NULL,kIOReturnSuccess,NULL,(IOHIDValueRef)(uintptr_t)3);assert(delivered==1);
    usage=4;value_callback(NULL,kIOReturnSuccess,NULL,(IOHIDValueRef)(uintptr_t)3);assert(delivered==1);
    usage=0x39;page=1;value_callback(NULL,kIOReturnSuccess,NULL,(IOHIDValueRef)(uintptr_t)3);assert(delivered==1);
    page=7;value=0;value_callback(NULL,kIOReturnSuccess,NULL,(IOHIDValueRef)(uintptr_t)3);assert(delivered==2);
    remove_callback(NULL,kIOReturnSuccess,NULL,(IOHIDDeviceRef)(uintptr_t)2);assert(delivered==3);
    cb_caps_hid_stop();assert(!scheduled && !value_callback && !remove_callback && closed>=1);
    cb_caps_hid_stop();
    puts("Caps HID permission, filtering and cleanup tests passed");
} }
