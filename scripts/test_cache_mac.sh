#!/usr/bin/env bash
# 仅验证缓存维护和剪贴板文件引用；使用独立目录与独立剪贴板。
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
flags=(-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc)
clang "${flags[@]}" src/protocol.c tests/test_cache.c -o build/test_cache
./build/test_cache
clang "${flags[@]}" -fobjc-arc -framework AppKit -framework ImageIO \
  src/protocol.c src/content.c src/files.c tests/test_files_macos.m -o build/test_files_macos
./build/test_files_macos
