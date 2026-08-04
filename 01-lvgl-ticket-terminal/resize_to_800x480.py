#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把任意图片裁剪/缩放成 800x480，适合作为 LVGL 登录背景图。

用法：
    python3 resize_to_800x480.py 原图.jpg 输出图.png

示例：
    python3 resize_to_800x480.py bg.jpg bg_800x480.png

说明：
    - 默认使用“居中裁剪”方式，保证输出正好是 800x480，不变形。
    - 如果图片比例不是 5:3，会先放大到覆盖 800x480，再从中间裁剪。
"""

import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("缺少 Pillow。请先安装：")
    print("sudo apt install python3-pil")
    print("或者：pip3 install pillow")
    sys.exit(1)

TARGET_W = 800
TARGET_H = 480

def resize_cover(src_path, out_path):
    img = Image.open(src_path).convert("RGB")
    src_w, src_h = img.size

    # cover：按较大的缩放比例缩放，保证能覆盖 800x480
    scale = max(TARGET_W / src_w, TARGET_H / src_h)
    new_w = int(src_w * scale + 0.5)
    new_h = int(src_h * scale + 0.5)

    img = img.resize((new_w, new_h), Image.LANCZOS)

    # 居中裁剪
    left = (new_w - TARGET_W) // 2
    top = (new_h - TARGET_H) // 2
    img = img.crop((left, top, left + TARGET_W, top + TARGET_H))

    img.save(out_path)
    print(f"已生成: {out_path}  尺寸: {TARGET_W}x{TARGET_H}")

def main():
    if len(sys.argv) != 3:
        print("用法: python3 resize_to_800x480.py 原图 输出图.png")
        sys.exit(1)

    src = Path(sys.argv[1])
    out = Path(sys.argv[2])

    if not src.exists():
        print(f"找不到原图: {src}")
        sys.exit(1)

    resize_cover(src, out)

if __name__ == "__main__":
    main()
