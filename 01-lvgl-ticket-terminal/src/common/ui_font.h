/*
 * 文件名：ui_font.h
 * 版本说明：中文注释。
 * 文件作用：FreeType 字体工具头文件。声明中文字体设置函数。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#ifndef UI_FONT_H
#define UI_FONT_H

#include "lvgl/lvgl.h"

/*
 * 文件作用：FreeType 字体工具模块。
 * 说明：
 * 1. set_ft() 用于给某个 LVGL 控件设置中文字体。
 * 2. ui_apply_font_recursive() 用于给某个页面及其所有子控件统一设置中文字体。
 * 3. 讲解要点：中文能显示，关键靠 FreeType 加载 simkai.ttf 字库。
 */

/* 给单个控件设置 FreeType 中文字体，size 表示字号。 */
void set_ft(lv_obj_t *obj, int size);

/* 递归设置字体：root 本身和它下面所有子控件都会被设置字体。 */
void ui_apply_font_recursive(lv_obj_t *root, int size);

#endif /* UI_FONT_H */
