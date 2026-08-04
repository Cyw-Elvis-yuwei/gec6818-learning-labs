/*
 * 文件名：ui_goods_view.h
 * 版本说明：答辩版中文注释。
 * 文件作用：商品展示模块头文件。声明商品卡片、翻页和主页面刷新函数。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#ifndef UI_GOODS_VIEW_H
#define UI_GOODS_VIEW_H

#include "lvgl/lvgl.h"

void ui_goods_view_create(lv_obj_t *parent);
void refresh_page(void);

#endif /* UI_GOODS_VIEW_H */
