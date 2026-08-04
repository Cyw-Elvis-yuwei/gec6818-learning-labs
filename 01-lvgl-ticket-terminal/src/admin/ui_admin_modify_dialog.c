/*
 * 文件名：ui_admin_modify_dialog.c
 * 版本说明：答辩版中文注释。
 * 文件作用：修改商品窗口实现文件。查询商品后修改售价、库存、图片名。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_admin_modify_dialog.h"
#include "ui_admin_goods_store.h"
#include "ui_admin_page.h"
#include "ui_font.h"
#include "ui_goods_view.h"
#include "ui_keyboard.h"
#include "ui_main_state.h"
#include "ui_msgbox.h"
#include "ui_text.h"
#include "lvgl/lvgl.h"
#include <stdio.h>

static lv_obj_t *modify_dialog = NULL;
static lv_obj_t *modify_name_ta = NULL;
static lv_obj_t *modify_price_ta = NULL;
static lv_obj_t *modify_stock_ta = NULL;
static lv_obj_t *modify_img_ta = NULL;

static lv_obj_t *create_modify_label(lv_obj_t *parent, const char *text, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    set_ft(label, 17);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *create_modify_ta(lv_obj_t *parent, int x, int y, int w, const char *placeholder)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, w, 34);
    lv_obj_set_pos(ta, x, y);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_CLICKED, NULL);
    set_ft(ta, 16);
    return ta;
}

static void modify_dialog_close_cb(lv_event_t *e)
{
    (void)e;
    hide_input_keyboard();
    if (modify_dialog)
    {
        lv_obj_add_flag(modify_dialog, LV_OBJ_FLAG_HIDDEN);
    }
}

static void modify_dialog_load_current_cb(lv_event_t *e)
{
    (void)e;

    const char *name = lv_textarea_get_text(modify_name_ta);
    int index = ui_admin_goods_find_by_name(name);
    char buf[64];

    if (index < 0)
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_NOT_FOUND);
        return;
    }

    snprintf(buf, sizeof(buf), "%.2f", (double)goods.items[index].price);
    lv_textarea_set_text(modify_price_ta, buf);

    snprintf(buf, sizeof(buf), "%d", goods.items[index].stock);
    lv_textarea_set_text(modify_stock_ta, buf);

    lv_textarea_set_text(modify_img_ta, goods.items[index].img_name);
}

static void modify_dialog_save_cb(lv_event_t *e)
{
    (void)e;

    const char *name = lv_textarea_get_text(modify_name_ta);
    const char *price_text = lv_textarea_get_text(modify_price_ta);
    const char *stock_text = lv_textarea_get_text(modify_stock_ta);
    const char *img_name = lv_textarea_get_text(modify_img_ta);
    float price = 0.0f;
    int stock = 0;
    int ret;

    if (!ui_admin_goods_text_is_valid(name))
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_NAME_EMPTY);
        return;
    }

    if (ui_admin_parse_price(price_text, &price) != 0)
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_PRICE_ERR);
        return;
    }

    if (ui_admin_parse_stock(stock_text, &stock) != 0)
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_STOCK_ERR);
        return;
    }

    ret = ui_admin_goods_modify(name, price, stock, img_name);
    if (ret == -2)
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_NOT_FOUND);
        return;
    }
    if (ret == -3)
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_BAD_CHAR);
        return;
    }
    if (ret != 0)
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_SAVE_FAILED);
        return;
    }

    hide_input_keyboard();
    ui_admin_page_refresh_list();
    refresh_page();
    show_msg_box(TXT_SUCCESS, TXT_GOODS_MODIFY_SUCCESS);
}

void ui_admin_modify_dialog_show(lv_event_t *e)
{
    (void)e;
    hide_input_keyboard();

    if (modify_dialog)
    {
        lv_obj_clear_flag(modify_dialog, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(modify_name_ta, "");
        lv_textarea_set_text(modify_price_ta, "");
        lv_textarea_set_text(modify_stock_ta, "");
        lv_textarea_set_text(modify_img_ta, "");
        return;
    }

    modify_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(modify_dialog, 720, 210);
    lv_obj_set_pos(modify_dialog, 40, 5);
    lv_obj_clear_flag(modify_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(modify_dialog, lv_color_hex(0x2C2C3A), 0);
    lv_obj_set_style_border_width(modify_dialog, 2, 0);
    lv_obj_set_style_border_color(modify_dialog, lv_color_hex(0x5A7FFF), 0);
    lv_obj_set_style_radius(modify_dialog, 10, 0);

    lv_obj_t *title = lv_label_create(modify_dialog);
    set_ft(title, 22);
    lv_label_set_text(title, TXT_MODIFY_GOODS_TITLE);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    create_modify_label(modify_dialog, TXT_GOODS_NAME, 40, 55);
    modify_name_ta = create_modify_ta(modify_dialog, 125, 45, 210, TXT_GOODS_NAME);

    create_modify_label(modify_dialog, TXT_GOODS_PRICE, 375, 55);
    modify_price_ta = create_modify_ta(modify_dialog, 460, 45, 170, TXT_GOODS_PRICE);
    lv_textarea_set_accepted_chars(modify_price_ta, "0123456789.");

    create_modify_label(modify_dialog, TXT_STOCK, 40, 105);
    modify_stock_ta = create_modify_ta(modify_dialog, 125, 95, 210, TXT_STOCK);
    lv_textarea_set_accepted_chars(modify_stock_ta, "0123456789");

    create_modify_label(modify_dialog, TXT_IMAGE_ID, 375, 105);
    modify_img_ta = create_modify_ta(modify_dialog, 460, 95, 170, TXT_DEFAULT_IMAGE_HINT);

    lv_obj_t *load_btn = lv_btn_create(modify_dialog);
    lv_obj_set_size(load_btn, 100, 36);
    lv_obj_set_pos(load_btn, 150, 160);
    lv_obj_add_event_cb(load_btn, modify_dialog_load_current_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *load_label = lv_label_create(load_btn);
    set_ft(load_label, 17);
    lv_label_set_text(load_label, TXT_QUERY);
    lv_obj_center(load_label);

    lv_obj_t *save_btn = lv_btn_create(modify_dialog);
    lv_obj_set_size(save_btn, 100, 36);
    lv_obj_set_pos(save_btn, 310, 160);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x226644), 0);
    lv_obj_add_event_cb(save_btn, modify_dialog_save_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *save_label = lv_label_create(save_btn);
    set_ft(save_label, 17);
    lv_label_set_text(save_label, TXT_SAVE);
    lv_obj_center(save_label);

    lv_obj_t *close_btn = lv_btn_create(modify_dialog);
    lv_obj_set_size(close_btn, 100, 36);
    lv_obj_set_pos(close_btn, 470, 160);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x664444), 0);
    lv_obj_add_event_cb(close_btn, modify_dialog_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_btn);
    set_ft(close_label, 17);
    lv_label_set_text(close_label, TXT_CANCEL);
    lv_obj_center(close_label);

    /* 统一给当前管理员界面及其子控件设置 FreeType 中文字体，避免中文显示成方框。 */
    ui_apply_font_recursive(modify_dialog, 18);
}
