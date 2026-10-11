#!/usr/bin/env bash
# 仅测试新桌面凭据/控制管道和真实 loopback；剪贴板、按键全部模拟。
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
flags=(-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc)
clang "${flags[@]}" tests/test_desktop_pairing.c -o build/test_desktop_pairing
./build/test_desktop_pairing
clang "${flags[@]}" tests/test_desktop_control.c -o build/test_desktop_control
./build/test_desktop_control
clang "${flags[@]}" src/protocol.c src/content.c src/files.c src/net.c \
  tests/test_desktop_worker.c -o build/test_desktop_worker
python3 tests/test_desktop_worker.py build/test_desktop_worker

clang "${flags[@]}" -fobjc-arc -framework AppKit -framework ApplicationServices \
  tests/test_desktop_key_macos.m -o build/test_desktop_key_macos
./build/test_desktop_key_macos

clang "${flags[@]}" -fobjc-arc -framework AppKit -framework ApplicationServices \
  tests/test_menu_settings.m -o build/test_menu_settings
./build/test_menu_settings

# 保持真实 .app 身份，防止普通 CLI 测试漏掉自身 bundle ID 的偏好域错误。
bundle_test_dir=$(mktemp -d /tmp/cb-menu-bundle-XXXXXX)
trap 'rm -rf "$bundle_test_dir"' EXIT
bundle_test_app="$bundle_test_dir/Test.app"
mkdir -p "$bundle_test_app/Contents/MacOS"
bundle_test_id="net.greatboyli.clipboardbridge.test.bundle.$(uuidgen)"
cat > "$bundle_test_app/Contents/Info.plist" <<PLIST
<?xml version="1.0"?><plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>$bundle_test_id</string>
<key>CFBundleExecutable</key><string>test</string>
<key>CFBundlePackageType</key><string>APPL</string>
</dict></plist>
PLIST
clang "${flags[@]}" -fobjc-arc -framework AppKit -framework ApplicationServices \
  tests/test_menu_bundle_macos.m -o "$bundle_test_app/Contents/MacOS/test"
"$bundle_test_app/Contents/MacOS/test"
