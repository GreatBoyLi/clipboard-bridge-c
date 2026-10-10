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
  -Wall -Wextra -Werror \
  -O2 \
  -fobjc-arc \
  -framework AppKit \
  -Isrc \
  src/protocol.c \
  src/net.c \
  src/platform_macos.m \
  src/main.c \
  -o build/clipboard-bridge

echo "Mac 程序已生成：$PROJECT_DIR/build/clipboard-bridge"
