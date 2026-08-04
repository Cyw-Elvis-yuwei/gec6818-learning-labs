#!/bin/bash
# 把两张登录背景图统一处理成 800x480
# 用法：
#   bash 一键生成800x480背景.sh classic.jpg vintage.jpg

set -e

if [ $# -ne 2 ]; then
    echo "用法: bash 一键生成800x480背景.sh 经典背景图 复古背景图"
    echo "示例: bash 一键生成800x480背景.sh bg1.jpg bg2.jpg"
    exit 1
fi

python3 resize_to_800x480.py "$1" img_login_bg_classic_800x480.png
python3 resize_to_800x480.py "$2" img_login_bg_vintage_800x480.png

echo
echo "已生成："
echo "  img_login_bg_classic_800x480.png"
echo "  img_login_bg_vintage_800x480.png"
echo
echo "下一步：把这两个 PNG 拿去 LVGL Image Converter 转成 C array。"
echo "变量名分别填："
echo "  img_login_bg_classic"
echo "  img_login_bg_vintage"
