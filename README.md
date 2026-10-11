# Clipboard Bridge C

通过 Tailscale 在 macOS 与 Windows 之间双向同步纯文本、图片、普通文件和文件夹。Mac 端可以为官方 Moonlight 提供 Command 快捷键映射、三指前进/后退和两指速度对应的 Windows 临时滚动行数。

- [菜单栏、托盘与多台 Mac 配对](docs/desktop.md)
- [图片同步、编译与启动](docs/images.md)
- [文件与文件夹同步](docs/files.md)
- [键盘映射](docs/keyboard.md)
- [Caps Lock 短按/长按](docs/caps-lock.md)
- [妙控板设置](docs/trackpad.md)

Mac 使用 `bash scripts/build_mac.sh` 编译；Windows 在 Visual Studio x64 Native Tools Command Prompt 运行 `scripts\build_windows.bat`。两个端点需要使用同一份配对密钥，正式连接使用 Tailscale 地址。密钥不要加入 Git。
