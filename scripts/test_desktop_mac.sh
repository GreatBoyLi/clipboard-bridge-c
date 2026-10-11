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
