/*
 * 文件名：ui_main_state.h
 * 版本说明：答辩版中文注释。
 * 文件作用：主界面共享状态声明文件。保存商品列表、购物车、页面控件等全局变量声明。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#ifndef UI_MAIN_STATE_H
#define UI_MAIN_STATE_H

#include "file_goods.h"
#include "lvgl/lvgl.h"

typedef struct {
    char scenic[40];
    float price;
    int qty;
} cart_item_t;

extern cart_item_t cart[20];
extern int cart_count;

extern goods_list_t goods;
extern int current_page;
extern int total_pages;

extern lv_obj_t *main_screen;
extern lv_obj_t *page_label;
extern lv_obj_t *frames[4];
extern lv_obj_t *frame_imgs[4];
extern lv_obj_t *frame_names[4];
extern lv_obj_t *frame_prices[4];
extern lv_obj_t *frame_stocks[4];

#endif /* UI_MAIN_STATE_H */
