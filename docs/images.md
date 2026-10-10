# 图片剪贴板同步

支持 Mac ↔ Windows 双向复制截图或图片。图片以 PNG 无损传输；如果同一次复制还带有纯文本，文字与图片一起写入目标剪贴板。目标应用决定粘贴哪种格式。例如文本编辑器通常选文字，聊天输入框通常选图片。这不等于保留网页/WPS 的图文排版：目前没有 HTML、RTF 富文本同步。

## 更新与启动

两端都需要更新并重新编译，旧程序不能接收新图片消息。编译前退出运行中的程序；Windows 等滚动守护进程退出后再编译。配对密钥、端口和启动参数保持原样，不需要重新配对。

Mac：

```sh
bash scripts/build_mac.sh
CB_THREE_FINGER=1 CB_SCROLL_LINES=1 ./build/clipboard-bridge \
  --connect 100.87.202.121 \
  --key-file "$HOME/.config/clipboard-bridge/pairing.key" --keyboard
```

Windows，在 Visual Studio x64 Native Tools Command Prompt 编译：

```bat
scripts\build_windows.bat
```

随后在 PowerShell 启动：

```powershell
.\build\clipboard-bridge.exe --listen 100.87.202.121 `
  --key-file "$env:USERPROFILE\.config\clipboard-bridge\pairing.key" --scroll-lines
```

图片功能默认开启，无需额外开关；键盘、三指与系统滚动行数配置保持现有行为。

## 限制与格式

- PNG 编码最大 20 MiB（20,971,520 字节）；转换后的 PNG 计入此限制，和图片文件在磁盘上的大小不一定相同。
- 最多 33,554,432 像素，单边不超过 16,384；解码后的四通道像素约最多 128 MiB。转码时还有临时内存开销。
- 文本仍最大 1 MiB UTF-8，允许中文、Emoji、多行和空文本。
- Mac 读取 PNG 或 TIFF；写入 PNG 与 TIFF，提高目标应用兼容性。
- Windows 优先读取注册的 PNG，其次 24/32 位 DIB/DIBV5 或位图；写入 PNG 与带透明度的 DIBV5。不支持的 DIB 压缩、色彩配置或像素格式会明确拒绝。
- 超限或损坏图片不会压缩降质，也不会默默仅同步附带文字；日志报告拒绝。日志不打印剪贴板内容。
- 不传输文件或文件夹；复制一张图片文件本身和在应用内“复制图片”是不同操作。
- 启动或重连只记录当前剪贴板版本。重新复制才会同步；中断时未完成的旧传输被丢弃。

## 传输与确认

图像及文本属于同一个快照。发送图片时先给出长度，再逐块传输（每块最多 16 KiB）。每个非末块收到接收确认才继续发送，避免双向大传输占满网络缓冲区。只有完整内容校验、解码和写入成功后才发送最终 ACK。⌘V 会等待排队内容的最终确认后再触发 Windows 粘贴；Windows 原有文件复制粘贴逻辑不受影响。

这会让较大图片的同步耗时比纯文本长，外网高延迟连接尤其明显。整个过程使用既有 Tailscale 连接；没有新增公网端口。

## 验证

Mac 自动测试：`bash scripts/test_mac.sh`。图像测试使用单独的测试剪贴板，不清空用户通用剪贴板。包含 PNG 长度/CRC/尺寸、图片附带文本、空文本、转换失败、双向同时发送、最终 ACK、断线与恶意消息等检查。

Windows 构建脚本会运行协议、DIB 边界及 WIC 内存编解码/透明度测试；这些测试不访问系统剪贴板。Windows 原生编译与运行需要在 Windows 完成。

手动检查两种方向：

1. Mac 截图后复制，在 Windows 微信或 WPS 粘贴；反向复制 Windows 截图，在 Mac 支持图片的应用粘贴。
2. 带透明度的图片，检查颜色与边缘；普通图片复制时确认日志出现 image 而非只有 text。
3. 连续复制文字、图片、文字，确认最新一次复制生效；不要只看截图中的文字是否能粘贴。
4. 两端同时复制图片；传输期间断开/重启 Windows，重连后重新复制再测试。

图片和文字同时保留的是源剪贴板实际提供的两种格式，程序不识别图片中的文字，也不读取网页图片的网络地址。
