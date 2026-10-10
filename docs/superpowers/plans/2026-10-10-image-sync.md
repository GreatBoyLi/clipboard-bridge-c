# Image clipboard sync implementation plan

**Goal:** 双向同步 PNG 与同一次复制的可选纯文本，20 MiB 编码限制、32 Mi 像素限制。
**Architecture:** 保留 TEXT 消息。新增 BEGIN+16 KiB CHUNK，共用递增序号及 ACK；完整接收和原生解码后才写入。两端同时发送时每轮处理接收再发送一块，主循环继续服务键盘及滚动租约。断线释放全部快照，不补发旧内容。
**Tech Stack:** C11、AppKit/ImageIO、Windows WIC 原生 C 接口。

- [x] 公共 content 模块：组合快照、PNG 结构/CRC/尺寸校验；先测边界、空文本、文本图片组合、截断及超限。
- [x] Mac 接口：统一读取版本、PNG/TIFF 转换、准备 PNG/TIFF/文本后单次写入；用独立测试剪贴板验证，不操作用户剪贴板。
- [x] Windows 接口：WIC 编解码、PNG/位图格式读取、PNG+DIBV5+文本写入，生命周期清理；构建脚本添加原生库。
- [x] 会话：16 KiB 双向分块，连续序号、进度超时、ACK 和粘贴门控。先写会话模拟测试再实现；旧文本回归完整运行。
- [x] 检查全部测试和 Mac 编译，独立复核，说明 Windows 需在用户机器编译验证。保留 ASCII/CRLF 批处理。安装前检查原始文件未变，提交本地，不推送。
