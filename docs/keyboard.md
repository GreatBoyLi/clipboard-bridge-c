# Moonlight 键盘映射（Mac 可选功能）

Windows 继续运行现有剪贴板程序即可，通信协议没有变化。Mac 端按键转换后由官方 Moonlight 转发给 Windows，程序没有接管鼠标、妙控板、滚动或视频。

## 启用

关闭原来的键盘映射软件，退出旧的 Mac 桥接进程，然后重新编译：

```sh
bash scripts/build_mac.sh
```

在 macOS「系统设置 → 隐私与安全性 → 辅助功能」中，允许启动此程序的终端应用控制电脑，例如 Terminal 或 iTerm；若系统要求授予程序本身权限，按系统提示授权。授权后重启终端和程序。程序不会自动修改权限。

```sh
./build/clipboard-bridge \
  --connect 100.87.202.121 \
  --key-file "$HOME/.config/clipboard-bridge/pairing.key" \
  --keyboard
```

不加 `--keyboard` 时只同步剪贴板，不安装键盘监听。只有桥接已经认证且官方 Moonlight 位于前台时才映射，其他 Mac 应用沿用原来的按键。

## 规则

| Mac 实际按键 | Windows 目标按键 |
| --- | --- |
| Control | Ctrl |
| Option | Win |
| Command | Alt |
| Shift | Shift |
| Command+A | Ctrl+A（全选） |
| Command+S | Ctrl+S（保存） |
| Command+F | Ctrl+F（查找） |
| Command+X | Ctrl+X（剪切） |
| Command+T | Ctrl+T（浏览器新建标签页） |
| Command+R | Ctrl+R（浏览器刷新） |
| Command+C | Ctrl+Insert（复制，取决于目标应用支持） |
| Command+V | Shift+Insert（粘贴，取决于目标应用支持） |
| Command+Z | Ctrl+Z（撤销） |
| Command+Shift+Z | Ctrl+Y（重做） |
| Control+C | Ctrl+C（保留终端中断行为） |

为防止 Windows 的 Alt 菜单让输入框失去焦点，Command 按下时先暂存；遇到普通按键后才发送 Alt 组合，单独按下再松开则发送一次 Alt。上述专用组合从头到尾不发送 Alt，也不会在松开后补发 Alt。

其他组合使用基础修饰键映射，例如 Command+B 发送 Alt+B；这里没有将所有 Command 组合都替换为 Ctrl。某些编辑器使用 Ctrl+Shift+Z 重做，可在启动参数末尾添加 `--redo ctrl-shift-z`，默认是 `--redo ctrl-y`。

⌘V 使用 Windows 当前剪贴板，不会强制重发 Mac 的旧文字。Mac 有新复制的文字或图片待同步时，先等全部待发送内容确认写入，再执行 Shift+Insert；没有新内容时直接执行。这允许在 Windows 内部使用 ⌘C/V 复制和粘贴文件（目标应用需支持 Insert 组合）。Mac 与 Windows 之间已支持[图片及附带纯文本同步](images.md)，文件跨设备传输仍未实现。

连接建立前已有的 Mac 文字不会自动发送，需要重新复制。断线、确认超时、剪贴板读取失败或离开 Moonlight 会取消尚未执行的粘贴，不在重连后重放。

## 验证与边界

先进入 Moonlight 串流画面，松开全部修饰键再测试。确认 Moonlight 允许捕获系统快捷键，否则 Option 转换出的 Win 键可能无法转发。安全输入模式和其他键盘拦截工具可能阻止事件监听。

建议用 Windows 文本编辑器验证复制、粘贴、撤销、重做，再在终端验证 Control+C；Ctrl+Insert/Shift+Insert 的支持由应用决定。切换回 Mac 应用，确认 Command+C/V 仍正常；粘贴等待期间断开连接，确认不会迟到执行。左右修饰键目前合并处理。

```sh
bash scripts/test_mac.sh
```

自动测试覆盖协议、回环网络、修饰键和组合键按下/松开状态、保留 Windows 原生剪贴板、等待所有待同步内容的正确 ACK、错误 ACK/断线取消。它不模拟真实 Moonlight 或 Windows 应用，最终输入手感及应用兼容性需要两端实机验证。

前台识别通过 macOS 进程管理器直接查询当前前台 PID，不依赖终端程序未运行的 AppKit 主事件循环，也不依赖 AXFocusedApplication 查询。系统查询失败时暂停映射，避免使用过期焦点影响其他应用。使用的进程接口已被苹果标为弃用，但在当前 macOS 上已验证可用；未来系统兼容性需继续检查。新增的模拟焦点测试覆盖终端 → Moonlight → 其他应用以及断线和查询失败。

当前尚未映射 Command+W。Windows 浏览器通常使用 Ctrl+W 关闭当前标签页，而 Alt+F4 关闭当前窗口；两者不应直接混为一谈。
