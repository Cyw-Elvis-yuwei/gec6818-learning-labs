/*
 * 文件名：ui_admin_query_dialog.c
 * 版本说明：中文注释。
 * 文件作用：查询商品窗口实现文件。按商品名查询售价、库存、图片名。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_admin_query_dialog.h"
#include "ui_admin_goods_store.h"
#include "ui_font.h"
#include "ui_keyboard.h"
#include "ui_main_state.h"
#include "ui_msgbox.h"
#include "ui_text.h"
#include "lvgl/lvgl.h"
#include <stdio.h>

static lv_obj_t *query_dialog = NULL;
static lv_obj_t *query_name_ta = NULL;
static lv_obj_t *query_result_label = NULL;

static void query_dialog_close_cb(lv_event_t *e)
{
    (void)e;
    hide_input_keyboard();
    if (query_dialog)
    {
        lv_obj_add_flag(query_dialog, LV_OBJ_FLAG_HIDDEN);
    }
}

static void query_dialog_confirm_cb(lv_event_t *e)
{
    (void)e;

    const char *name = lv_textarea_get_text(query_name_ta);
    int index = ui_admin_goods_find_by_name(name);
    char buf[220];

    if (index < 0)
    {
        lv_label_set_text(query_result_label, TXT_GOODS_NOT_FOUND);
        return;
    }

    snprintf(buf,
             sizeof(buf),
             "%s:%s\n%s:%.2f%s\n%s:%d\n%s:%s",
             TXT_GOODS_NAME,
             goods.items[index].scenic,
             TXT_GOODS_PRICE,
             (double)goods.items[index].price,
             TXT_YUAN,
             TXT_STOCK,
             goods.items[index].stock,
             TXT_IMAGE_ID,
             goods.items[index].img_name);
    lv_label_set_text(query_result_label, buf);
}

void ui_admin_query_dialog_show(lv_event_t *e)
{
    (void)e;
    hide_input_keyboard();

    if (query_dialog)
    {
        lv_obj_clear_flag(query_dialog, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(query_name_ta, "");
        lv_label_set_text(query_result_label, "");
        return;
    }

    query_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(query_dialog, 520, 210);
    lv_obj_set_pos(query_dialog, 140, 5);
    lv_obj_clear_flag(query_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(query_dialog, lv_color_hex(0x2C2C3A), 0);
    lv_obj_set_style_border_width(query_dialog, 2, 0);
    lv_obj_set_style_border_color(query_dialog, lv_color_hex(0x5A7FFF), 0);
    lv_obj_set_style_radius(query_dialog, 10, 0);

    lv_obj_t *title = lv_label_create(query_dialog);
    set_ft(title, 22);
    lv_label_set_text(title, TXT_QUERY_GOODS_TITLE);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *name_label = lv_label_create(query_dialog);
    set_ft(name_label, 18);
    lv_label_set_text(name_label, TXT_GOODS_NAME);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(name_label, 40, 58);

    query_name_ta = lv_textarea_create(query_dialog);
    lv_obj_set_size(query_name_ta, 260, 36);
    lv_obj_set_pos(query_name_ta, 135, 48);
    lv_textarea_set_one_line(query_name_ta, true);
    lv_textarea_set_placeholder_text(query_name_ta, TXT_GOODS_NAME);
    lv_obj_add_event_cb(query_name_ta, ta_focus_cb, LV_EVENT_CLICKED, NULL);
    set_ft(query_name_ta, 16);

    lv_obj_t *query_btn = lv_btn_create(query_dialog);
    lv_obj_set_size(query_btn, 80, 36);
    lv_obj_set_pos(query_btn, 410, 48);
    lv_obj_add_event_cb(query_btn, query_dialog_confirm_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *query_label = lv_label_create(query_btn);
    set_ft(query_label, 17);
    lv_label_set_text(query_label, TXT_QUERY);
    lv_obj_center(query_label);

    query_result_label = lv_label_create(query_dialog);
    set_ft(query_result_label, 17);
    lv_obj_set_size(query_result_label, 420, 80);
    lv_obj_set_pos(query_result_label, 50, 100);
    lv_obj_set_style_text_color(query_result_label, lv_color_hex(0xFFD700), 0);
    lv_label_set_text(query_result_label, "");

    lv_obj_t *close_btn = lv_btn_create(query_dialog);
    lv_obj_set_size(close_btn, 120, 34);
    lv_obj_set_pos(close_btn, 200, 168);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x664444), 0);
    lv_obj_add_event_cb(close_btn, query_dialog_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_btn);
    set_ft(close_label, 17);
    lv_label_set_text(close_label, TXT_CLOSE);
    lv_obj_center(close_label);

    /* 统一给当前管理员界面及其子控件设置 FreeType 中文字体，避免中文显示成方框。 */
    ui_apply_font_recursive(query_dialog, 18);
}
