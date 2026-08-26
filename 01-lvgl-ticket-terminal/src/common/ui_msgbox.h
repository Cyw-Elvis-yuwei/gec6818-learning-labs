/*
 * 文件名：ui_msgbox.h
 * 版本说明：中文注释。
 * 文件作用：提示框模块头文件。声明统一弹窗提示函数。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#ifndef UI_MSGBOX_H
#define UI_MSGBOX_H

void show_msg_box(const char *title, const char *msg);

#endif /* UI_MSGBOX_H */
