# 安装包与 Release

## Mac

在 Mac 运行：

```sh
bash scripts/package_mac.sh 1.1.0
```

脚本在临时目录从源码编译，输出 `dist/ClipboardBridge-v1.1.0-macOS-arm64.dmg` 和对应 `.sha256`（Intel Mac 编译时名称为 `x86_64`）。不会覆盖正在使用的 `build/Clipboard Bridge.app`。只打包完整应用、Applications 快捷入口与安装说明，不含个人密钥、IP、缓存或测试程序。

双击 DMG，将应用拖入「应用程序」。更新前从原应用菜单退出，再替换；配置和配对位于用户目录，会保留。不添加开机自启。移到新位置或更新后按 macOS 提示重新授权输入权限。

默认本地签名不等于 Developer ID 分发签名和 Apple 公证；下载后的系统拦截仍需按 macOS 提示处理。正式公共分发参见 [Apple Developer ID](https://developer.apple.com/developer-id/)；本脚本没有自动公证流程。

## GitHub Release

提交源码并推送后，在仓库 Releases 创建对应标签（例如 `v1.1.0`）的发布，将 DMG、Windows 安装包及校验文件作为附件上传。不要把安装包提交到源码目录，不要上传配对密钥。发布前分别测试从安装包安装后的应用。

Windows 安装包配置将在下一步加入；当前 Mac 包仅包含本机架构，不是 Intel/Apple Silicon 通用包。
