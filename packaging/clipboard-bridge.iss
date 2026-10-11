; 用 Inno Setup 打开此文件，按 F9 编译。先运行 scripts\build_windows.bat。
; 路径相对本文件所在的 packaging 目录，不依赖当前工作目录。
#ifndef MyAppVersion
  #define MyAppVersion "1.1.0"
#endif
#define MyAppName "Clipboard Bridge"

[Setup]
; 固定安装标识，后续版本不要改动，才能正常覆盖升级与卸载。
AppId={{B8F50D92-C091-4B46-A27D-F84B8D09F1BA}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=GreatBoyLi
AppPublisherURL=https://github.com/GreatBoyLi/clipboard-bridge-c
AppSupportURL=https://github.com/GreatBoyLi/clipboard-bridge-c/issues
DefaultDirName={localappdata}\Programs\Clipboard Bridge
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; 安装到当前用户目录，无需管理员权限。不添加开机自启。
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=ClipboardBridge-v{#MyAppVersion}-Windows-x64-Setup
SetupIconFile=..\assets\clipboard-bridge.ico
UninstallDisplayIcon={app}\clipboard-bridge-tray.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; 运行时等待用户正常退出，让后台程序完成滚动设置恢复；不强杀程序。
AppMutex=Local\ClipboardBridge.NativeTray.v1
CloseApplications=no
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "快捷方式："; Flags: unchecked

[Files]
; 两个程序必须装在同一目录；不复制测试程序、个人配置或密钥。
Source: "..\build\clipboard-bridge-tray.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\clipboard-bridge.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; 所有启动快捷方式都指向托盘程序，后台程序由它管理。
Name: "{group}\{#MyAppName}"; Filename: "{app}\clipboard-bridge-tray.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\clipboard-bridge-tray.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\clipboard-bridge-tray.exe"; Description: "启动 Clipboard Bridge"; Flags: nowait postinstall skipifsilent unchecked

; 不添加 UninstallDelete：卸载仅删除已安装文件，保留用户配对与设置。
