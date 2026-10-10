# 妙控板：三指导航与 Windows 滚动行数

两指滚动由 Moonlight 原样传递。旧的滚动事件缩放、像素转行、量化、余量累计代码及对应测试已删除。

## 构建和启动

两端都更新代码、重新编译，先关闭旧程序。Windows 在 Visual Studio 的 x64 Native Tools Command Prompt 运行：

```bat
scripts\build_windows.bat
```

随后在 PowerShell 启动（新增 `--scroll-lines`）：

```powershell
.\build\clipboard-bridge.exe --listen 100.87.202.121 --key-file "$env:USERPROFILE\.config\clipboard-bridge\pairing.key" --scroll-lines
```

Mac：

```bash
bash scripts/build_mac.sh
CB_THREE_FINGER=1 CB_SCROLL_LINES=1 CB_KEYBOARD_DEBUG=1 \
./build/clipboard-bridge \
  --connect 100.87.202.121 \
  --key-file "$HOME/.config/clipboard-bridge/pairing.key" \
  --keyboard
```

`CB_SMOOTH_SCROLL` 已移除，改用 `CB_SCROLL_LINES`。只有行数控制/三指至少一项启用才加载 MultitouchSupport；键盘映射需要终端的 macOS 辅助功能权限。

## 线性速度设置

读取两指中心纵向距离除以帧间隔（触控板高度/秒），沿用 25ms 速度估计滤波。

- 速度 <= 0.25：1 行。
- 速度 >= 0.8：5 行。
- 中间按 `1 + 4 × (速度 - 0.25) / 0.55` 线性计算，四舍五入为 Windows 的整数行数。
- 取整后，首次采样约低于 0.319 都为 1 行；已处于 1 行时，需超过约 0.333 才升至 2 行。日志中常见的 0.2～0.3 慢滑仍保持 1 行。
- 行数变化有小幅滞回，最短切换间隔 200ms，避免临界点抖动。设置更新经独立网络通道，初始手势可能有约一轮主循环及网络延迟，不保证赶在 Moonlight 第一条滚动消息之前。

两指事件的数值、时序和系统惯性全部保持原样。只发送认证后的行数控制，不注入额外鼠标事件。前台必须是官方 Moonlight 且无修饰键；离手短暂保留最后行数 500ms，避免立即加速惯性；离开 Moonlight则释放。三指右滑后退、左滑前进；四指保持 Mac 系统行为。

## 恢复与范围

Windows `--scroll-lines` 是明确启用的开关；未启用时不修改系统行数。启用后创建独立无窗口守护进程，记录当前原始值（包括 0 或整页滚动），不写入永久用户配置。只允许一个守护实例控制设置。

- 正常退出、离开 Moonlight、会话断开：请求恢复原值。
- 超过 1.2 秒没有收到活跃控制续租：守护进程独立恢复，覆盖网络卡住/主循环阻塞。
- Windows 主程序被终止：守护进程检测真实进程句柄退出并恢复。
- 若守护进程也被强制终止、整个进程组被结束或系统异常，无法保证立即恢复；重新登录可重新加载未被改写的用户配置。系统接口失败时会报告，不声称恢复成功。

该设置作用于同一 Windows 用户的全部鼠标滚动，不限定某个窗口。部分应用可能忽略/缓存系统行数，线性调节不能保证每个应用都响应，更不能承诺与 Mac 像素滚动完全一致。使用期间避免同时手动改 Windows 的滚动行数，以免恢复启动时原值覆盖手动调整。

Foundation Sunshine 保持高分辨率滚动开启。驱动选择保留当前测试设置，程序不修改 Sunshine。Windows 声明支持后 Mac 才发送扩展消息；旧版本不支持此功能，两端都应升级。新 Mac 配合旧 Windows（未启用新功能）仍只运行原有功能。

## 验证

`bash scripts/test_mac.sh`：检查 1~5 线性计算、防抖、续租过期、准确恢复、恢复失败重试、协议长度和序号、能力协商及释放，模拟事件序列化前后完全相同。测试不访问真实剪贴板、不注入真实输入、不修改系统设置。

Windows 构建脚本增加纯行数/恢复测试。实际 Windows 守护进程和跨平台手感需在 Windows 验收：慢滑、快滑、切换回 Mac、断开网络、Ctrl+C 停止任一端、终止 Windows 主程序（不要同时终止守护进程），确认行数返回启动前值。

禁用：Windows 去掉 `--scroll-lines`，Mac 去掉 `CB_SCROLL_LINES=1`；三指和剪贴板功能可单独保留。
