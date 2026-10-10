#!/usr/bin/env bash
# 仅新增文件路径和受影响的协议/快照测试；不监听真实按键、不操作通用剪贴板。
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
flags=(-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc)
clang "${flags[@]}" src/protocol.c tests/test_folders.c -o build/test_folders
./build/test_folders
clang "${flags[@]}" src/protocol.c src/files.c tests/test_files.c -o build/test_files
./build/test_files
clang "${flags[@]}" src/protocol.c src/content.c src/files.c src/net.c tests/test_files_session.c -o build/test_files_session
./build/test_files_session
clang "${flags[@]}" -fobjc-arc -framework AppKit -framework ImageIO \
 src/protocol.c src/content.c src/files.c tests/test_files_macos.m -o build/test_files_macos
./build/test_files_macos
clang "${flags[@]}" src/protocol.c tests/test_protocol.c -o build/test_protocol
./build/test_protocol
clang "${flags[@]}" src/protocol.c src/content.c tests/test_content.c -o build/test_content
./build/test_content
# 同一剪贴板接口有修改，仅检查已有文字/图片写入保持正常。
clang "${flags[@]}" -fobjc-arc -framework AppKit -framework ImageIO \
 src/protocol.c src/content.c src/files.c tests/test_image_macos.m -o build/test_image_macos
./build/test_image_macos
