# 安装包与 Release

## Mac

在 Mac 运行：

```sh
bash scripts/package_mac.sh 1.1.0
```

脚本在临时目录从源码编译，输出 `dist/ClipboardBridge-v1.1.0-macOS-arm64.dmg` 和对应 `.sha256`（Intel Mac 编译时名称为 `x86_64`）。不会覆盖正在使用的 `build/Clipboard Bridge.app`。只打包完整应用、Applications 快捷入口与安装说明，不含个人密钥、IP、缓存或测试程序。

双击 DMG，将应用拖入「应用程序」。更新前从原应用菜单退出，再替换；配置和配对位于用户目录，会保留。不添加开机自启。移到新位置或更新后按 macOS 提示重新授权输入权限。

默认本地签名不等于 Developer ID 分发签名和 Apple 公证；下载后的系统拦截仍需按 macOS 提示处理。正式公共分发参见 [Apple Developer ID](https://developer.apple.com/developer-id/)；本脚本没有自动公证流程。

## Windows（Inno Setup 图形界面）

1. 在 Mac 提交并 push 打包配置，再在 Windows 项目目录运行 `git pull origin main`。
2. 从 Windows 托盘菜单退出旧程序；若还有终端版，也正常停止，等待滚动行数恢复。
3. 在 Visual Studio x64 Native Tools Command Prompt 运行 `scripts\build_windows.bat`。
4. 打开 Inno Setup Compiler，选择 File → Open，打开 `packaging\clipboard-bridge.iss`。
5. 选择 Build → Compile（F9），生成 `dist\ClipboardBridge-v1.1.0-Windows-x64-Setup.exe`。
6. 双击安装包，安装后从开始菜单启动 Clipboard Bridge。桌面快捷方式与安装后立即启动均可选。不添加开机自启。

默认仅安装给当前用户，无需管理员权限；目录为 `%LOCALAPPDATA%\Programs\Clipboard Bridge`。托盘程序与后台程序安装在一起。运行中的托盘会阻止安装/卸载，需先从菜单正常退出。卸载入口位于 Windows 的「设置 → 应用」，卸载保留 `%LOCALAPPDATA%\ClipboardBridge` 中的配对与配置。

发布前在 Windows 验证编译、安装、快捷方式启动、连接、退出与卸载。Mac 上无法验证 Inno Setup 原生编译；目前安装包未配置代码签名。

生成匹配 Mac 包的 SHA-256 附件，可在项目目录用 PowerShell 运行：

```powershell
$asset = "ClipboardBridge-v1.1.0-Windows-x64-Setup.exe"
$digest = (Get-FileHash ".\dist\$asset" -Algorithm SHA256).Hash.ToLowerInvariant()
"$digest  $asset" | Set-Content ".\dist\$asset.sha256" -Encoding ASCII
```

发布新版本时修改 `.iss` 顶部的 `MyAppVersion`，保持 AppId 不变。

## GitHub Release

提交源码并推送后，在仓库 Releases 创建对应标签（例如 `v1.1.0`）的发布，将 DMG、Windows 安装包及校验文件作为附件上传。不要把安装包提交到源码目录，不要上传配对密钥。发布前分别测试从安装包安装后的应用。

当前 Mac 包仅包含本机架构，不是 Intel/Apple Silicon 通用包。
