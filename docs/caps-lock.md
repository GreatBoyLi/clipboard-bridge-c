# Caps Lock 短按与长按

此功能仅在 Moonlight 位于前台、连接已认证且 Windows 端已就绪时启用。判断依据是 Windows 的大写锁定状态，Mac 键盘指示灯不代表 Windows 状态。

- 小写状态短按（不足 1 秒）：发送 Win+空格切换输入法。
- 大写状态短按：关闭大写锁定，不切换输入法。
- 按住达到 1 秒：切换大写锁定一次，松开不再执行短按。

Win+空格需要 Windows 已安装多个输入法或键盘布局。离开 Moonlight、断线、移除键盘或按下其它键会取消尚未完成的长按。

## 编译与启动

两端均需更新源码并重新编译。先停止旧程序，Windows 需等待滚动设置守护程序退出，再运行 `scripts\build_windows.bat`。Windows 启动参数保持原样。

Mac 使用 `bash scripts/build_mac.sh`，然后启动：

```bash
CB_CAPS_KEY=1 CB_THREE_FINGER=1 CB_SCROLL_LINES=1 \
./build/clipboard-bridge \
  --connect 100.87.202.121 \
  --key-file "$HOME/.config/clipboard-bridge/pairing.key" \
  --keyboard
```

终端除已有的辅助功能权限外，还需在「系统设置 → 隐私与安全性 → 输入监控」中启用。更改权限后退出并重新打开终端。连接后应显示 `Windows Caps Lock mapping ready.`。

删除 `CB_CAPS_KEY=1` 即可关闭新功能。未开启时保持原有 Caps Lock 行为，不需要新监听权限。

## 实机验证

先在 Windows 确认大写锁定关闭，测试短按是否切换输入法；长按 1 秒后测试字母是否为大写，再短按确认回到小写。重复长按应每次只切换一次。连接前 Windows 已为大写、Windows 本机键盘改变大写状态的情况也需测试。

Mac 自动测试及 Windows 动作模拟测试已通过；Windows 原生编译、键盘状态初始读取与 Moonlight 实际输入路径仍需 Windows 实机验证。按住其它键时 Windows 会保守拒绝动作，避免干扰正在输入的组合键。
