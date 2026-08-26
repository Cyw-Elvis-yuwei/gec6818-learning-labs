/*
 * 文件名：ui_admin_query_dialog.h
 * 版本说明：中文注释。
 * 文件作用：查询商品窗口头文件。声明查询商品弹窗。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#ifndef UI_ADMIN_QUERY_DIALOG_H
#define UI_ADMIN_QUERY_DIALOG_H

#include "lvgl/lvgl.h"

/* Query-product operation window. */
void ui_admin_query_dialog_show(lv_event_t *e);

#endif /* UI_ADMIN_QUERY_DIALOG_H */
