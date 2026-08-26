/*
 * 文件名：ui_main.h
 * 版本说明：中文注释。
 * 文件作用：主页面入口头文件。声明主页面创建函数。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#ifndef UI_MAIN_H
#define UI_MAIN_H

void ui_main_create(void);

#endif /* UI_MAIN_H */
