#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* 独立临时文件可避免上次意外退出留下 .new 导致后续导入永久失败。 */
static BOOL store_pairing(NSString *path,NSData *bytes) {
    NSString *template=[path stringByAppendingString:@".XXXXXX"];
    char *temporary=strdup(template.fileSystemRepresentation);if(!temporary) return NO;
    int fd=mkstemp(temporary);BOOL ok=fd>=0;
    if(ok) {
        (void)fcntl(fd,F_SETFD,FD_CLOEXEC);
        ok=write(fd,bytes.bytes,bytes.length)==(ssize_t)bytes.length && fsync(fd)==0;
        if(close(fd)!=0) ok=NO;
    }
    if(ok) ok=rename(temporary,path.fileSystemRepresentation)==0;
    if(!ok && fd>=0) unlink(temporary);
    free(temporary);return ok;
}

/* 菜单栏只负责设置和生命周期；剪贴板和输入仍在原 worker 主线程处理。 */
@interface BridgeMenu : NSObject <NSApplicationDelegate>
@property NSStatusItem *item;
@property NSMenu *menu;
@property NSUserDefaults *settings;
@property NSTask *worker;
@property NSPipe *control;
@property NSPipe *output;
@property NSMutableData *pending;
@property NSString *status;
@property NSString *detail;
@property NSString *configDirectory;
@property BOOL restartPending;
@property BOOL quitting;
@property int lockFD;
@end
@implementation BridgeMenu
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    self.lockFD=-1;
    self.configDirectory=[NSHomeDirectory() stringByAppendingPathComponent:@".config/clipboard-bridge"];
    NSError *error=nil;
    if(![[NSFileManager defaultManager] createDirectoryAtPath:self.configDirectory
        withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&error]) {
        [self alert:@"无法创建配置目录" message:error.localizedDescription];[NSApp terminate:nil];return;
    }
    NSString *lock=[self.configDirectory stringByAppendingPathComponent:@"menu.lock"];
    self.lockFD=open(lock.fileSystemRepresentation,O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(self.lockFD<0 || flock(self.lockFD,LOCK_EX|LOCK_NB)!=0) {
        [self alert:@"程序已经运行或配置目录不可用" message:@"请查看菜单栏中的 CB 图标。"];
        [NSApp terminate:nil];return;
    }
    self.settings=[[NSUserDefaults alloc] initWithSuiteName:@"net.greatboyli.clipboardbridge"];
    [self.settings registerDefaults:@{@"port":@"47321",@"keyboard":@YES,@"three":@YES,@"scroll":@YES,@"caps":@YES}];
    self.status=@"未连接";self.detail=@"";
    self.item=[[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    [self rebuildMenu];
    if([self.settings stringForKey:@"host"].length && [[NSFileManager defaultManager] fileExistsAtPath:[self keyPath]]) {
        [self connect:nil];
    } else {
        [self configure:nil];
    }
}
- (NSString *)keyPath {return [self.configDirectory stringByAppendingPathComponent:@"menu-pairing.key"];}
- (void)alert:(NSString *)title message:(NSString *)message {
    [NSApp activateIgnoringOtherApps:YES];NSAlert *alert=[NSAlert new];
    alert.messageText=title;alert.informativeText=message?:@"";[alert runModal];
}
- (void)add:(NSString *)title action:(SEL)action enabled:(BOOL)enabled {
    NSMenuItem *entry=[[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];
    entry.target=self;entry.enabled=enabled;[self.menu addItem:entry];
}
- (void)rebuildMenu {
    self.item.button.title=[self.status isEqualToString:@"已连接"]?@"CB ●":@"CB ○";
    self.item.button.toolTip=[NSString stringWithFormat:@"Clipboard Bridge：%@",self.status];
    self.menu=[NSMenu new];self.menu.autoenablesItems=NO;
    [self add:[NSString stringWithFormat:@"状态：%@",self.status] action:NULL enabled:NO];
    NSString *host=[self.settings stringForKey:@"host"];
    if(host.length) [self add:[NSString stringWithFormat:@"主机：%@",host] action:NULL enabled:NO];
    [self.menu addItem:[NSMenuItem separatorItem]];
    [self add:@"连接" action:@selector(connect:) enabled:self.worker==nil && !self.quitting];
    [self add:@"断开" action:@selector(disconnect:) enabled:self.worker!=nil && !self.quitting];
    [self add:@"主机与配对…" action:@selector(configure:) enabled:!self.quitting];
    [self.menu addItem:[NSMenuItem separatorItem]];
    NSArray *labels=@[@"键盘快捷键映射",@"Caps Lock 短按/长按",@"三指前进/后退",@"两指滚动行数控制"];
    NSArray *keys=@[@"keyboard",@"caps",@"three",@"scroll"];
    for(NSUInteger i=0;i<keys.count;i++) {
        NSMenuItem *entry=[[NSMenuItem alloc] initWithTitle:labels[i] action:@selector(toggle:) keyEquivalent:@""];
        entry.target=self;entry.representedObject=keys[i];entry.state=[self.settings boolForKey:keys[i]]?NSControlStateValueOn:NSControlStateValueOff;
        entry.enabled=!self.quitting;[self.menu addItem:entry];
    }
    [self.menu addItem:[NSMenuItem separatorItem]];
    [self add:@"查看连接信息…" action:@selector(information:) enabled:YES];
    [self add:@"输入权限设置…" action:@selector(permissions:) enabled:YES];
    [self add:@"退出" action:@selector(quit:) enabled:!self.quitting];
    self.item.menu=self.menu;
}
- (NSTextField *)field:(NSString *)value frame:(NSRect)frame {
    NSTextField *field=[[NSTextField alloc] initWithFrame:frame];field.stringValue=value?:@"";return field;
}
- (void)configure:(id)sender {
    (void)sender;[NSApp activateIgnoringOtherApps:YES];NSAlert *alert=[NSAlert new];
    alert.messageText=@"连接 Windows 主机";
    alert.informativeText=@"填写主机的 Tailscale IP 或设备域名。首次配对需要导入 Windows 托盘为这台 Mac 导出的密钥。";
    [alert addButtonWithTitle:@"保存"];[alert addButtonWithTitle:@"导入配对密钥…"];[alert addButtonWithTitle:@"取消"];
    NSView *view=[[NSView alloc] initWithFrame:NSMakeRect(0,0,360,80)];
    NSTextField *host=[self field:[self.settings stringForKey:@"host"] frame:NSMakeRect(80,45,270,24)];
    NSTextField *port=[self field:[self.settings stringForKey:@"port"] frame:NSMakeRect(80,10,100,24)];
    NSTextField *hostLabel=[NSTextField labelWithString:@"主机地址"];hostLabel.frame=NSMakeRect(0,45,75,24);
    NSTextField *portLabel=[NSTextField labelWithString:@"端口"];portLabel.frame=NSMakeRect(0,10,75,24);
    [view addSubview:hostLabel];[view addSubview:portLabel];[view addSubview:host];[view addSubview:port];alert.accessoryView=view;
    NSModalResponse result=[alert runModal];if(result==NSAlertThirdButtonReturn) return;
    NSString *address=[host.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSString *number=port.stringValue;
    NSCharacterSet *badPort=[[NSCharacterSet characterSetWithCharactersInString:@"0123456789"] invertedSet];
    if(!address.length || address.length>253 || [address rangeOfCharacterFromSet:NSCharacterSet.whitespaceAndNewlineCharacterSet].location!=NSNotFound ||
       [address containsString:@"/"] || [address containsString:@"\\"] || !number.length || number.length>5 ||
       [number rangeOfCharacterFromSet:badPort].location!=NSNotFound || number.integerValue<1 || number.integerValue>65535) {
        [self alert:@"地址或端口不正确" message:@"请填写 Tailscale IP/设备域名，端口范围为 1–65535。"];
        return;
    }
    /* 配置/密钥变更必须在旧 worker 已退出后生效。先停止旧 worker，避免导入覆盖读取中的凭据。 */
    if(result==NSAlertSecondButtonReturn) {
        if(self.worker) { [self alert:@"请先断开连接" message:@"断开后再导入新的配对密钥。当前设置未修改。"] ;return; }
        NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseDirectories=NO;panel.allowsMultipleSelection=NO;
        panel.message=@"选择 Windows 为这台 Mac 导出的配对密钥";
        if([panel runModal]!=NSModalResponseOK) return;
        NSError *error=nil;
        NSDictionary *attributes=[[NSFileManager defaultManager] attributesOfItemAtPath:panel.URL.path error:&error];
        if(!attributes || [attributes[NSFileSize] unsignedLongLongValue]>66 ||
           ![attributes[NSFileType] isEqualToString:NSFileTypeRegular]) {
            [self alert:@"密钥文件无效" message:@"请选择小于 67 字节的配对密钥文件。"] ;return;
        }
        NSData *data=[NSData dataWithContentsOfURL:panel.URL options:0 error:&error];
        NSString *text=data?[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]:nil;
        NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"\\A[0-9a-fA-F]{64}(?:\\r?\\n)?\\z" options:0 error:NULL];
        if(!text || data.length>66 || [pattern numberOfMatchesInString:text options:0 range:NSMakeRange(0,text.length)]!=1) {
            [self alert:@"密钥文件无效" message:@"请选择 Windows 托盘导出的 pairing.key 文件。"] ;return;
        }
        NSString *key=[[text substringToIndex:64] stringByAppendingString:@"\n"];
        /* 原子替换并设置仅当前用户可读写的权限。 */
        NSData *bytes=[key dataUsingEncoding:NSUTF8StringEncoding];
        if(!store_pairing([self keyPath],bytes)) {
            [self alert:@"无法保存配对密钥" message:@"请检查配置目录权限；现有密钥未替换。"] ;return;
        }
    }
    [self.settings setObject:address forKey:@"host"];[self.settings setObject:number forKey:@"port"];
    [self rebuildMenu];
    if(self.worker) {self.restartPending=YES;[self stopWorker];}
    else [self connect:nil];
}
- (void)connect:(id)sender {
    (void)sender;if(self.worker || self.quitting) return;
    NSString *host=[self.settings stringForKey:@"host"];
    if(!host.length || ![[NSFileManager defaultManager] fileExistsAtPath:[self keyPath]]) {
        [self alert:@"尚未完成配对" message:@"请打开「主机与配对」，填写地址并导入配对密钥。"] ;return;
    }
    NSString *executable=[NSBundle.mainBundle pathForAuxiliaryExecutable:@"clipboard-bridge"];
    if(!executable) {[self alert:@"同步程序缺失" message:@"请重新编译或完整复制 Clipboard Bridge.app。"] ;return;}
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:executable];
    NSMutableArray *arguments=[NSMutableArray arrayWithArray:@[@"--connect",host,@"--port",[self.settings stringForKey:@"port"],@"--key-file",[self keyPath],@"--desktop-control"]];
    if([self.settings boolForKey:@"keyboard"]) [arguments addObject:@"--keyboard"];
    task.arguments=arguments;
    NSMutableDictionary *environment=[NSProcessInfo.processInfo.environment mutableCopy];
    /* 明确覆盖终端遗留开关，菜单设置是唯一来源。 */
    environment[@"CB_THREE_FINGER"]=[self.settings boolForKey:@"three"]?@"1":@"0";
    environment[@"CB_SCROLL_LINES"]=[self.settings boolForKey:@"scroll"]?@"1":@"0";
    environment[@"CB_CAPS_KEY"]=[self.settings boolForKey:@"caps"]?@"1":@"0";
    environment[@"CB_KEYBOARD_DEBUG"]=@"0";task.environment=environment;
    self.control=[NSPipe pipe];self.output=[NSPipe pipe];self.pending=[NSMutableData data];
    task.standardInput=self.control;task.standardOutput=self.output;task.standardError=self.output;
    self.worker=task;self.status=@"连接中";self.detail=@"";[self rebuildMenu];
    __weak BridgeMenu *weakSelf=self;
    self.output.fileHandleForReading.readabilityHandler=^(NSFileHandle *handle) {
        NSData *data=[handle readDataUpToLength:65536 error:NULL];if(!data.length) {handle.readabilityHandler=nil;return;}
        dispatch_async(dispatch_get_main_queue(),^{BridgeMenu *menu=weakSelf;if(menu.worker==task) [menu consume:data];});
    };
    task.terminationHandler=^(NSTask *finished) {
        dispatch_async(dispatch_get_main_queue(),^{BridgeMenu *menu=weakSelf;if(menu.worker!=finished) return;
            menu.output.fileHandleForReading.readabilityHandler=nil;
            [menu.control.fileHandleForWriting closeAndReturnError:NULL];[menu.output.fileHandleForReading closeAndReturnError:NULL];
            menu.worker=nil;menu.control=nil;menu.output=nil;
            menu.status=finished.terminationStatus==0?@"未连接":@"连接已停止（查看连接信息）";
            [menu rebuildMenu];
            if(menu.quitting) [NSApp replyToApplicationShouldTerminate:YES];
            else if(menu.restartPending) {menu.restartPending=NO;[menu connect:nil];}
        });
    };
    NSError *error=nil;
    if(![task launchAndReturnError:&error]) {
        task.terminationHandler=nil;self.output.fileHandleForReading.readabilityHandler=nil;
        [self.control.fileHandleForWriting closeAndReturnError:NULL];[self.output.fileHandleForReading closeAndReturnError:NULL];
        self.worker=nil;self.control=nil;self.output=nil;self.status=@"启动失败";self.detail=error.localizedDescription;[self rebuildMenu];
    }
}
- (void)consume:(NSData *)data {
    [self.pending appendData:data];
    if(self.pending.length>65536) [self.pending setLength:0];
    for(;;) {
        const unsigned char *bytes=self.pending.bytes;NSUInteger n=self.pending.length,index=0;
        while(index<n && bytes[index]!='\n') index++;
        if(index==n) break;
        NSString *line=[[NSString alloc] initWithBytes:bytes length:index encoding:NSUTF8StringEncoding];
        [self.pending replaceBytesInRange:NSMakeRange(0,index+1) withBytes:NULL length:0];
        if(!line.length) continue;self.detail=line.length>512?[line substringToIndex:512]:line;
        if([line containsString:@"Connected and authenticated."]) self.status=@"已连接";
        else if([line containsString:@"Authentication failed"]) self.status=@"等待主机选择或配对确认";
        else if([line containsString:@"Connection failed"] || [line containsString:@"Disconnected."]) self.status=@"等待重连";
        else if([line containsString:@"initialization failed"]) self.status=@"启动失败（检查输入权限）";
    }
    [self rebuildMenu];
}
- (void)stopWorker {
    if(!self.worker) return;self.status=@"正在断开";[self rebuildMenu];
    /* 关闭写端让 worker 读取到 EOF，并走现有释放/恢复流程；不强杀进程。 */
    [self.control.fileHandleForWriting closeAndReturnError:NULL];
}
- (void)disconnect:(id)sender {(void)sender;self.restartPending=NO;[self stopWorker];}
- (void)toggle:(NSMenuItem *)sender {
    NSString *key=sender.representedObject;[self.settings setBool:![self.settings boolForKey:key] forKey:key];
    [self rebuildMenu];if(self.worker) {self.restartPending=YES;[self stopWorker];}
}
- (void)information:(id)sender {
    (void)sender;[self alert:[NSString stringWithFormat:@"连接状态：%@",self.status]
        message:[NSString stringWithFormat:@"主机：%@\n最近信息：%@\n\nWindows 必须选择本 Mac 对应的配对设备；切换后重新复制才同步。",
        [self.settings stringForKey:@"host"]?:@"未设置",self.detail?:@"暂无"]];
}
- (void)permissions:(id)sender {
    (void)sender;NSDictionary *options=@{(__bridge NSString *)kAXTrustedCheckOptionPrompt:@YES};
    (void)AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
    [self alert:@"输入权限" message:@"在系统设置 → 隐私与安全性中，给 Clipboard Bridge 授予辅助功能权限；Caps Lock/底层触点按系统提示授予输入监控权限。授权后先断开，再重新连接。仅同步剪贴板可关闭键盘映射。"];
}
- (void)quit:(id)sender {(void)sender;[NSApp terminate:nil];}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)app {
    (void)app;self.restartPending=NO;
    if(self.worker) {self.quitting=YES;[self stopWorker];return NSTerminateLater;}
    return NSTerminateNow;
}
- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;if(self.lockFD>=0) close(self.lockFD);
}
@end
int main(void) {
    @autoreleasepool {
        NSApplication *application=[NSApplication sharedApplication];
        application.activationPolicy=NSApplicationActivationPolicyAccessory;
        __attribute__((objc_precise_lifetime)) BridgeMenu *delegate=[BridgeMenu new];
        application.delegate=delegate;[application run];
    }
    return 0;
}
