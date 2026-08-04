/*
 * 文件名：ui_admin_page.c
 * 版本说明：答辩版中文注释。
 * 文件作用：管理员商品管理页面实现文件。显示商品列表和四个操作按钮。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_admin_page.h"
#include "ui_admin_add_dialog.h"
#include "ui_admin_delete_dialog.h"
#include "ui_admin_modify_dialog.h"
#include "ui_admin_query_dialog.h"
#include "ui_font.h"
#include "ui_goods_view.h"
#include "ui_keyboard.h"
#include "ui_main_state.h"
#include "ui_text.h"
#include "lvgl/lvgl.h"
#include <stdio.h>

static lv_obj_t *admin_page = NULL;
static lv_obj_t *admin_list_cont = NULL;

static void admin_page_close_cb(lv_event_t *e);
static lv_obj_t *create_admin_button(lv_obj_t *parent, const char *text, int y, lv_event_cb_t cb);

/* Close administrator management page and hide any visible keyboard. */
static void admin_page_close_cb(lv_event_t *e)
{
    (void)e;
    ui_admin_page_close();
}

/* Common button factory used by the four administrator operation entries. */
static lv_obj_t *create_admin_button(lv_obj_t *parent, const char *text, int y, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 150, 42);
    lv_obj_set_pos(btn, 585, y);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x365A9C), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    set_ft(label, 19);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return btn;
}

/* Refresh the on-screen product list after add/delete/modify operations. */
void ui_admin_page_refresh_list(void)
{
    char buf[160];

    if (admin_list_cont == NULL)
    {
        return;
    }

    lv_obj_clean(admin_list_cont);

    lv_obj_t *header = lv_label_create(admin_list_cont);
    set_ft(header, 17);
    lv_label_set_text(header, TXT_ADMIN_GOODS_LIST);
    lv_obj_set_style_text_color(header, lv_color_hex(0xFFD700), 0);
    lv_obj_set_pos(header, 10, 6);

    for (int i = 0; i < goods.count; i++)
    {
        snprintf(buf,
                 sizeof(buf),
                 "%02d. %s  %.2f%s  %s:%d",
                 i + 1,
                 goods.items[i].scenic,
                 (double)goods.items[i].price,
                 TXT_YUAN,
                 TXT_STOCK,
                 goods.items[i].stock);

        lv_obj_t *row = lv_label_create(admin_list_cont);
        set_ft(row, 15);
        lv_label_set_text(row, buf);
        lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
        lv_obj_set_width(row, 515);
        lv_obj_set_style_text_color(row, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_pos(row, 10, 34 + i * 28);
    }
}

void ui_admin_page_close(void)
{
    hide_input_keyboard();

    if (admin_page)
    {
        lv_obj_add_flag(admin_page, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Show administrator management page after administrator login succeeds. */
void ui_admin_page_show(void)
{
    hide_input_keyboard();

    if (admin_page)
    {
        lv_obj_clear_flag(admin_page, LV_OBJ_FLAG_HIDDEN);
        ui_admin_page_refresh_list();
        return;
    }

    admin_page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(admin_page, 760, 430);
    lv_obj_set_pos(admin_page, 20, 20);
    lv_obj_clear_flag(admin_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(admin_page, lv_color_hex(0x202030), 0);
    lv_obj_set_style_border_width(admin_page, 2, 0);
    lv_obj_set_style_border_color(admin_page, lv_color_hex(0x5A7FFF), 0);
    lv_obj_set_style_radius(admin_page, 10, 0);

    lv_obj_t *title = lv_label_create(admin_page);
    set_ft(title, 24);
    lv_label_set_text(title, TXT_ADMIN_GOODS_TITLE);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    admin_list_cont = lv_obj_create(admin_page);
    lv_obj_set_size(admin_list_cont, 535, 345);
    lv_obj_set_pos(admin_list_cont, 25, 60);
    lv_obj_set_style_bg_color(admin_list_cont, lv_color_hex(0x151522), 0);
    lv_obj_set_style_border_color(admin_list_cont, lv_color_hex(0x404060), 0);
    lv_obj_set_style_border_width(admin_list_cont, 1, 0);
    lv_obj_set_style_radius(admin_list_cont, 6, 0);
    lv_obj_set_scroll_dir(admin_list_cont, LV_DIR_VER);

    create_admin_button(admin_page, TXT_ADMIN_BTN_ADD, 75, ui_admin_add_dialog_show);
    create_admin_button(admin_page, TXT_ADMIN_BTN_DELETE, 130, ui_admin_delete_dialog_show);
    create_admin_button(admin_page, TXT_ADMIN_BTN_MODIFY, 185, ui_admin_modify_dialog_show);
    create_admin_button(admin_page, TXT_ADMIN_BTN_QUERY, 240, ui_admin_query_dialog_show);

    lv_obj_t *close_btn = lv_btn_create(admin_page);
    lv_obj_set_size(close_btn, 150, 42);
    lv_obj_set_pos(close_btn, 585, 350);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x664444), 0);
    lv_obj_add_event_cb(close_btn, admin_page_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_btn);
    set_ft(close_label, 19);
    lv_label_set_text(close_label, TXT_CLOSE);
    lv_obj_center(close_label);

    ui_admin_page_refresh_list();

    /* 统一给当前管理员界面及其子控件设置 FreeType 中文字体，避免中文显示成方框。 */
    ui_apply_font_recursive(admin_page, 18);
}
