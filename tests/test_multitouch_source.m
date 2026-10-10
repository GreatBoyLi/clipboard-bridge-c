#import <CoreFoundation/CoreFoundation.h>
#include <assert.h>
#include <dlfcn.h>
#include <string.h>
static void *fake_open(const char *path, int mode);
static void *fake_symbol(void *handle, const char *name);
#define dlopen fake_open
#define dlsym fake_symbol
#include "../src/multitouch_macos.m"
static int registrations, removals, starts, stops, fail_symbol, fail_start, emitted;
static unsigned short last_key;
static pid_t context_pid = 200;
static void *fake_open(const char *path, int mode) { assert(path && mode); return (void *)1; }
static CFArrayRef fake_list(void) {
    const void *items[] = {CFSTR("device-one"), CFSTR("device-two")};
    return CFArrayCreate(NULL, items, 2, &kCFTypeArrayCallBacks);
}
static void fake_register(mt_device device, mt_callback callback) { assert(device && callback); ++registrations; }
static void fake_unregister(mt_device device, mt_callback callback) { assert(device && callback); ++removals; }
static int fake_start(mt_device device, int flags) { assert(device && flags == 0); ++starts; return fail_start && starts == 2 ? -1 : 0; }
static int fake_stop(mt_device device) { assert(device); ++stops; return 0; }
static void *fake_symbol(void *handle, const char *name) {
    assert(handle);
    if (fail_symbol) return NULL;
    if (!strcmp(name,"MTDeviceCreateList")) return (void *)fake_list;
    if (!strcmp(name,"MTRegisterContactFrameCallback")) return (void *)fake_register;
    if (!strcmp(name,"MTUnregisterContactFrameCallback")) return (void *)fake_unregister;
    if (!strcmp(name,"MTDeviceStart")) return (void *)fake_start;
    if (!strcmp(name,"MTDeviceStop")) return (void *)fake_stop;
    return NULL;
}
static pid_t context(void) { return context_pid; }
static void navigate(unsigned short key, pid_t pid) { assert(pid == 200); last_key = key; ++emitted; }
static void frame_at(int n, float x, double time) {
    mt_contact contacts[4] = {0};
    for (int i=0;i<n;++i) {
        contacts[i].identifier=i+1; contacts[i].state=4;
        contacts[i].normalized.position=(mt_xy){x+0.1f*i,0.4f};
    }
    contact_frame(slots[0].device, contacts, (size_t)n, time, 0);
}
int main(void) {
    fail_symbol=1;
    assert(cb_touch_start(context,navigate,0) == -1 && !devices && !accepting);
    fail_symbol=0; fail_start=1;
    assert(cb_touch_start(context,navigate,0) == -1);
    assert(!devices && !slots && !accepting);
    assert(registrations==2 && removals==2 && stops==1);
    registrations=removals=starts=stops=0; fail_start=0;
    assert(cb_touch_start(context,navigate,0) == 0);
    frame_at(2,.2f,.9);
    mt_contact two[2]={0};
    for(int i=0;i<2;++i) {two[i].identifier=i+1;two[i].state=4;two[i].normalized.position=(mt_xy){.2f+.1f*i,.45f};}
    contact_frame(slots[0].device,two,2,1,0);
    double speed=0;
    assert(cb_touch_speed(200,&speed) && fabs(speed-.5)<.0001);
    assert(!cb_touch_speed(201,&speed));
    CFAbsoluteTime received=slots[0].motion_received;
    slots[0].motion_received-=1;
    assert(!cb_touch_speed(200,&speed));
    slots[0].motion_received=received;
    frame_at(3,.2f,1.01);
    assert(!cb_touch_speed(200,&speed)); frame_at(3,.27f,1.04); frame_at(3,.35f,1.07);
    assert(emitted==1 && last_key==124);
    frame_at(3,.5f,1.1); assert(emitted==1);
    frame_at(0,0,1.11); frame_at(4,.2f,2); frame_at(3,.2f,2.03); frame_at(3,.5f,2.06);
    assert(emitted==1);
    frame_at(0,0,2.1); context_pid=0;
    frame_at(3,.2f,3); frame_at(3,.5f,3.06); assert(emitted==1);
    context_pid=200;
    cb_touch_stop(); cb_touch_stop();
    assert(!slots && !devices && !accepting && stops==2 && removals==2);
    puts("Multitouch startup, frame conversion and cleanup tests passed");
}
