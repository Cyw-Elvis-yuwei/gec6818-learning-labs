/*
 * 文件名：ui_admin_page.h
 * 版本说明：中文注释。
 * 文件作用：管理员商品管理页面头文件。声明管理首页显示、关闭、刷新列表函数。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#ifndef UI_ADMIN_PAGE_H
#define UI_ADMIN_PAGE_H

#include "lvgl/lvgl.h"

/* Administrator product management home page. */
void ui_admin_page_show(void);
void ui_admin_page_refresh_list(void);
void ui_admin_page_close(void);

#endif /* UI_ADMIN_PAGE_H */
