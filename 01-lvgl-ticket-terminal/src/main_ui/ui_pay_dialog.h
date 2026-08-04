/*
 * 文件名：ui_pay_dialog.h
 * 版本说明：答辩版中文注释。
 * 文件作用：付款弹窗模块头文件。声明付款窗口显示函数。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#ifndef UI_PAY_DIALOG_H
#define UI_PAY_DIALOG_H

#include "lvgl/lvgl.h"

void show_pay_dialog(lv_event_t *e);

#endif /* UI_PAY_DIALOG_H */
