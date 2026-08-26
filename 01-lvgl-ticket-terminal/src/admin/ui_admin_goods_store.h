/*
 * 文件名：ui_admin_goods_store.h
 * 版本说明：中文注释。
 * 文件作用：管理员商品数据操作头文件。声明新增、删除、修改、查询、保存接口。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#ifndef UI_ADMIN_GOODS_STORE_H
#define UI_ADMIN_GOODS_STORE_H

#include <stddef.h>

/*
 * Product storage helpers for administrator CRUD operations.
 * These helpers update the global goods list and persist it to goods_utf8.txt.
 */

int ui_admin_goods_find_by_name(const char *name);
int ui_admin_goods_add(const char *name, float price, int stock, const char *img_name);
int ui_admin_goods_delete(const char *name);
int ui_admin_goods_modify(const char *name, float price, int stock, const char *img_name);
int ui_admin_goods_save_all(void);
int ui_admin_parse_price(const char *text, float *out_value);
int ui_admin_parse_stock(const char *text, int *out_value);
int ui_admin_goods_get_capacity(void);
void ui_admin_goods_update_pages(void);
int ui_admin_goods_text_is_valid(const char *text);

#endif /* UI_ADMIN_GOODS_STORE_H */
