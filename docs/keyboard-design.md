# Moonlight 键盘映射设计与验证计划

已确认规则：Control→Ctrl、Option→Win、Command→Alt、Shift→Shift；⌘A→Ctrl+A、⌘C→Ctrl+Insert、⌘V→Shift+Insert、⌘Z→Ctrl+Z、⌘⇧Z→Ctrl+Y。Control+C 保持原有行为。只在官方 Moonlight 前台且桥接已认证时启用。

实现选择：在 Mac 上使用独立事件线程，把按键转换后交给现有 Moonlight 键盘链路，而不是再通过第二条网络输入链路注入 Windows。Windows 剪贴板服务与通信协议保持兼容。事件流中先释放不需要的修饰键，再按下需要的修饰键，最后发送转换后的普通键；对应 key-up 使用 key-down 时保存的规则。离开前台、断线和退出时释放本程序转换的按键。

粘贴：吞掉 ⌘V 的原始事件，由主线程先检查 Mac 是否有新复制的文字。无待同步文字时直接使用 Windows 当前剪贴板发送 Shift+Insert；有待发送或待确认文字时，等全部队列清空及 ACK 后再发送。请求本身不读取或强制重发 Mac 旧内容，防止 Windows 文件剪贴板被旧文字覆盖。切换前台、断线或超时取消，不重放旧请求。跨设备只同步文本；Windows 内部文件复制粘贴交给原应用处理。


边界：映射是显式 `--keyboard` 选项；需要 macOS 辅助功能权限。左右修饰键合并为同一种逻辑修饰键。进入 Moonlight 时若已有修饰键按住，等待全部松开后启用。安全输入、其他键盘重映射软件、Moonlight 的系统快捷键捕获设置会影响实际效果，需用户实机验证。重做默认为 Ctrl+Y，并提供 Ctrl+Shift+Z 选项。

步骤：先写并运行映射状态机测试，确认失败；实现纯 C 状态机；加入 Mac 事件适配、粘贴 ACK 接入和编译选项；运行原测试、新增测试、Mac 构建及 CLI 检查；自审并复制回用户项目，不提交 Git。真实 Moonlight/Windows 按键行为最后由用户验证。

## 输入焦点回归修正

用户在微信传输助手观察到按 Command 后输入框失焦。旧实现先发送 Alt，再在 C/V 到来时切换修饰键，会触发目标应用菜单行为。修正采用暂存 Command：普通按键确认后才发送 Alt；独立 Command 在松开时发送 Alt 点击；专用组合在整个按下/松开过程中禁止 Alt，也不恢复或补发 Alt。测试覆盖规则状态和真实事件适配输出，事件均截获，不投递真实应用。

## 全选

新增 Command+A → Ctrl+A，仅在专用组合匹配时使用 Ctrl，物理 Control+A 保持原状。沿用专用组合的按下/松开状态及 Alt 屏蔽；测试覆盖重复按下、先松 A、先松 Command 和事件适配层实际生成的 Ctrl+A 顺序。

## 常用 Ctrl 组合扩展

新增 Command+S/F/X/T/R → Ctrl+S/F/X/T/R。只匹配单独 Command 修饰，保留物理 Control 行为及未指定的其他组合。新增纯 C 测试覆盖每组按下、重复、松开顺序，Mac 事件适配测试验证输出不包含 Alt。Command+W 本次仅讨论对应关系，未加入映射。
