/*
 * 文件名：ui_main_state.c
 * 版本说明：中文注释。
 * 文件作用：主界面共享状态定义文件。真正定义商品、购物车、页码和主界面控件变量。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_main_state.h"

cart_item_t cart[20];
int cart_count = 0;

goods_list_t goods;
int current_page = 0;
int total_pages = 2;

lv_obj_t *main_screen = NULL;
lv_obj_t *page_label = NULL;
lv_obj_t *frames[4];
lv_obj_t *frame_imgs[4];
lv_obj_t *frame_names[4];
lv_obj_t *frame_prices[4];
lv_obj_t *frame_stocks[4];
