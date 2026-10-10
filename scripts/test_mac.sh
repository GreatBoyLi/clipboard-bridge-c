#!/usr/bin/env bash
# 测试不读取真实剪贴板，不安装全局键盘监听。
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
flags=(-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc)
clang "${flags[@]}" src/protocol.c tests/test_protocol.c -o build/test_protocol
clang "${flags[@]}" src/net.c tests/test_net.c -o build/test_net
clang "${flags[@]}" src/keyboard_map.c tests/test_keyboard_map.c -o build/test_keyboard_map
clang "${flags[@]}" src/protocol.c src/content.c src/net.c tests/test_paste_gate.c -o build/test_paste_gate
# 使用模拟系统焦点，不创建真实键盘监听或改变用户的前台应用。
clang "${flags[@]}" -fobjc-arc -framework AppKit -framework ApplicationServices -pthread \
  src/protocol.c src/keyboard_map.c src/multitouch_macos.m tests/test_keyboard_focus.m -o build/test_keyboard_focus
./build/test_protocol
./build/test_net
./build/test_keyboard_map
./build/test_paste_gate

./build/test_keyboard_focus

# 专用 Command 组合的整个生命周期不能含 Alt，避免目标输入框失焦。
clang "${flags[@]}" src/keyboard_map.c tests/test_command_focus.c -o build/test_command_focus
clang "${flags[@]}" -fobjc-arc -framework AppKit -framework ApplicationServices -pthread \
  src/protocol.c src/keyboard_map.c src/multitouch_macos.m tests/test_keyboard_relay.m -o build/test_keyboard_relay
./build/test_command_focus
./build/test_keyboard_relay

# 全选组合按下、重复和两种松开顺序均保持 Ctrl+A，不泄漏 Alt。
clang "${flags[@]}" src/keyboard_map.c tests/test_select_all.c -o build/test_select_all
./build/test_select_all

# 保存、查找、剪切、新标签页、刷新均使用对应 Ctrl 组合。
clang "${flags[@]}" src/keyboard_map.c tests/test_common_shortcuts.c -o build/test_common_shortcuts
./build/test_common_shortcuts

# 三指导航和连续滚动：仅模拟系统事件，输出全部由测试截获。
clang "${flags[@]}" -fobjc-arc -framework AppKit -framework ApplicationServices -pthread \
  src/protocol.c src/keyboard_map.c src/multitouch_macos.m tests/test_trackpad_relay.m -o build/test_trackpad_relay
./build/test_trackpad_relay

clang "${flags[@]}" tests/test_touch_swipe.c -o build/test_touch_swipe
./build/test_touch_swipe
# 私有 API 的启动/退出通过模拟设备验证，不启动真实底层监听。
clang "${flags[@]}" -fobjc-arc -framework CoreFoundation -pthread tests/test_multitouch_source.m -o build/test_multitouch_source
./build/test_multitouch_source

clang "${flags[@]}" tests/test_touch_motion.c -o build/test_touch_motion
./build/test_touch_motion

clang "${flags[@]}" tests/test_scroll_lines.c -o build/test_scroll_lines
./build/test_scroll_lines
clang "${flags[@]}" src/protocol.c src/content.c src/net.c tests/test_scroll_session.c -o build/test_scroll_session
./build/test_scroll_session

# 图片协议及独立测试剪贴板，不触碰用户通用剪贴板。
clang "${flags[@]}" src/protocol.c src/content.c tests/test_content.c -o build/test_content
./build/test_content
clang "${flags[@]}" -fobjc-arc -framework AppKit -framework ImageIO \
  src/protocol.c src/content.c tests/test_image_macos.m -o build/test_image_macos
./build/test_image_macos
clang "${flags[@]}" src/protocol.c src/content.c src/net.c tests/test_image_session.c -o build/test_image_session
./build/test_image_session
clang "${flags[@]}" tests/test_windows_dib.c -o build/test_windows_dib
./build/test_windows_dib
