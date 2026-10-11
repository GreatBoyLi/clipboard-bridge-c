#!/usr/bin/env bash
# 在临时目录编译和打包，不替换正在使用的 build 应用或复制个人配置。
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION="${1:-1.1.0}"
if [[ $# -gt 1 || ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "用法：bash scripts/package_mac.sh [版本号，例如 1.1.0]" >&2
  exit 1
fi

# 只复制编译所需的源码和图标；绝不打包主机地址、密钥或缓存。
PACKAGE_WORK=$(mktemp -d "${TMPDIR:-/tmp}/cb-package.XXXXXX")
trap 'rm -rf "$PACKAGE_WORK"' EXIT
mkdir -p "$PACKAGE_WORK/project/scripts" "$PACKAGE_WORK/image" "$PROJECT_DIR/dist"
ditto "$PROJECT_DIR/src" "$PACKAGE_WORK/project/src"
ditto "$PROJECT_DIR/assets" "$PACKAGE_WORK/project/assets"
cp "$SCRIPT_DIR/build_mac.sh" "$PACKAGE_WORK/project/scripts/"
bash "$PACKAGE_WORK/project/scripts/build_mac.sh"
APP="$PACKAGE_WORK/project/build/Clipboard Bridge.app"

# 安装包中的版本号与发布版本一致；签名必须在修改 Info.plist 之后进行。
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $VERSION" "$APP/Contents/Info.plist"
codesign --force --deep --sign "${CB_SIGN_IDENTITY:--}" "$APP"
codesign --verify --deep --strict "$APP"

# 根据实际编译产物命名，防止把 Apple Silicon 包误标成 Intel 或通用版本。
ARCH=$(lipo -archs "$APP/Contents/MacOS/ClipboardBridge")
WORKER_ARCH=$(lipo -archs "$APP/Contents/MacOS/clipboard-bridge")
if [[ "$ARCH" != "$WORKER_ARCH" ]]; then
  echo "界面程序与后台程序架构不一致，停止打包。" >&2; exit 1
fi
case "$ARCH" in
  arm64|x86_64) ;;
  *) echo "暂不支持该打包架构：$ARCH" >&2; exit 1 ;;
esac
NAME="ClipboardBridge-v${VERSION}-macOS-${ARCH}.dmg"
# 用 /Applications 快捷入口提供标准的拖拽安装方式。
ditto "$APP" "$PACKAGE_WORK/image/Clipboard Bridge.app"
ln -s /Applications "$PACKAGE_WORK/image/Applications"
cat > "$PACKAGE_WORK/image/安装说明.txt" <<'TEXT'
Clipboard Bridge

1. 将 Clipboard Bridge.app 拖入 Applications（应用程序）。
2. 更新前从旧应用菜单选择「退出」，再替换应用；已有配对和设置保留。
3. 从应用程序目录启动，在菜单栏设置主机地址和端口、生成并导出配对密钥。
4. Windows 托盘添加本 Mac 并导入密钥，选中本 Mac 后在 Mac 菜单点击「连接」。
5. 输入映射需要辅助功能权限；Caps Lock 等功能还需要输入监控权限。

支持 macOS 12 或更新版本。此安装包不包含个人密钥或主机配置，也不设置开机自启。
当前默认构建使用本地签名，未经 Apple 公证；公开正式分发需另行配置 Developer ID 签名与公证。
TEXT
hdiutil create -volname "Clipboard Bridge" -srcfolder "$PACKAGE_WORK/image" \
  -format UDZO -ov "$PACKAGE_WORK/$NAME"
hdiutil verify "$PACKAGE_WORK/$NAME"
# 验证后才放入 dist；校验文件仅包含文件名，便于下载后核对。
mv -f "$PACKAGE_WORK/$NAME" "$PROJECT_DIR/dist/$NAME"
(cd "$PROJECT_DIR/dist" && shasum -a 256 "$NAME" > "$NAME.sha256")
echo "Mac 安装包：$PROJECT_DIR/dist/$NAME"
echo "SHA-256 校验：$PROJECT_DIR/dist/$NAME.sha256"
