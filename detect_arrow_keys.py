#!/usr/bin/env python3
"""检测键盘方向键（零依赖脚本）。

在终端中实时检测并打印四个方向键的按下事件。
只使用 Python 标准库 curses，无需安装任何额外包。

用法:
    python3 detect_arrow_keys.py

退出: 按 Esc 或 Ctrl+C。

说明:
    - 此脚本依赖终端焦点，需要在终端窗口激活时按键才有效。
    - curses 只上报"按下"事件，不区分按住/松开。如果需要全局捕获
      按下与松开（用于点动控制），本仓库已提供基于 Linux evdev 的
      诊断程序 keyboard_input_test，可参考 README 运行。
"""

import os

# 缩短 Esc 与转义序列（方向键）的区分等待时间，让 Esc 立即生效
os.environ.setdefault("ESCDELAY", "25")

import curses

# 方向键 -> 显示名称
KEY_NAMES = {
    curses.KEY_UP: "上 (Up)",
    curses.KEY_DOWN: "下 (Down)",
    curses.KEY_LEFT: "左 (Left)",
    curses.KEY_RIGHT: "右 (Right)",
}


def main(stdscr):
    curses.curs_set(0)      # 隐藏光标
    stdscr.keypad(True)     # 让方向键以 KEY_* 常量返回
    stdscr.clear()
    stdscr.addstr(0, 0, "方向键检测运行中...")
    stdscr.addstr(1, 0, "按方向键检测；按 Esc 或 Ctrl+C 退出。")
    stdscr.refresh()

    while True:
        key = stdscr.getch()
        if key == -1:
            continue

        name = KEY_NAMES.get(key)
        if name:
            stdscr.addstr(3, 0, f"检测到: {name}")
        elif key == 27:  # Esc
            break
        else:
            # 打印未知按键的原始码，方便后续扩展
            stdscr.addstr(3, 0, f"其他按键: {key!r}")
        stdscr.clrtoeol()
        stdscr.refresh()


if __name__ == "__main__":
    try:
        curses.wrapper(main)
    except KeyboardInterrupt:
        pass
