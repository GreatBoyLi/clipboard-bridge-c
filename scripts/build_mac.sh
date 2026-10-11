#!/usr/bin/env bash

# 任意命令失败时立即停止；禁止使用未定义的变量。
set -euo pipefail

# 根据脚本所在位置定位项目根目录，
# 不要求用户必须先切换到某个目录。
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_DIR"
mkdir -p build

# C 模块使用 C11。
# Mac 原生接口使用 Objective-C + ARC，并链接 AppKit。
# 开启编译警告，把警告当作错误处理。
clang \
  -std=c11 \
  -mmacosx-version-min=12.0 \
  -Wall -Wextra -Werror \
  -O2 \
  -fobjc-arc \
  -framework AppKit \
  -framework ImageIO \
  -framework ApplicationServices \
  -framework IOKit \
  -pthread \
  -Isrc \
  src/protocol.c \
  src/content.c \
  src/files.c \
  src/net.c \
  src/platform_macos.m \
  src/keyboard_map.c \
  src/keyboard_macos.m \
  src/multitouch_macos.m \
  src/caps_hid_macos.m \
  src/main.c \
  -o build/clipboard-bridge

echo "Mac 程序已生成：$PROJECT_DIR/build/clipboard-bridge"


# 打包原生菜单栏与原 worker；日常双击 .app 即可，不注册登录启动。
APP="$PROJECT_DIR/build/Clipboard Bridge.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp assets/clipboard-bridge.icns assets/clipboard-bridge.png "$APP/Contents/Resources/"
clang -std=c11 -mmacosx-version-min=12.0 -Wall -Wextra -Werror -O2 -fobjc-arc \
  -framework AppKit -framework ApplicationServices \
  src/desktop_macos.m -o "$APP/Contents/MacOS/ClipboardBridge"
cp build/clipboard-bridge "$APP/Contents/MacOS/clipboard-bridge"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>net.greatboyli.clipboardbridge</string>
<key>CFBundleIconFile</key><string>clipboard-bridge.icns</string>
<key>CFBundleName</key><string>Clipboard Bridge</string>
<key>CFBundleDisplayName</key><string>Clipboard Bridge</string>
<key>CFBundleExecutable</key><string>ClipboardBridge</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>1.1</string>
<key>CFBundleVersion</key><string>1</string>
<key>LSMinimumSystemVersion</key><string>12.0</string>
<key>LSUIElement</key><true/>
<key>NSHighResolutionCapable</key><true/>
<key>NSPrincipalClass</key><string>NSApplication</string>
</dict></plist>
PLIST
# 有本机签名身份时可通过 CB_SIGN_IDENTITY 指定，否则使用本地 ad-hoc 签名。
codesign --force --deep --sign "${CB_SIGN_IDENTITY:--}" "$APP"
# 更新时间让 Finder 重新读取应用图标。
touch "$APP"
echo "Mac 菜单栏程序：$APP"
