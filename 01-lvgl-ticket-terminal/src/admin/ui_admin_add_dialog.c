/*
 * 文件名：ui_admin_add_dialog.c
 * 功能说明：管理员新增商品弹窗界面
 * 主要功能：输入商品名称、价格、库存和图片编号，并调用商品管理接口保存数据。
 * 说明：本文件只负责新增商品弹窗的界面创建、输入校验和事件处理。
 */

#include "ui_admin_add_dialog.h"
#include "ui_admin_goods_store.h"
#include "ui_admin_page.h"
#include "ui_font.h"
#include "ui_goods_view.h"
#include "ui_keyboard.h"
#include "ui_msgbox.h"
#include "ui_text.h"
#include "lvgl/lvgl.h"
#include <string.h>

static lv_obj_t *add_dialog = NULL;
static lv_obj_t *add_name_ta = NULL;
static lv_obj_t *add_price_ta = NULL;
static lv_obj_t *add_stock_ta = NULL;
static lv_obj_t *add_img_ta = NULL;

static lv_obj_t *create_label(lv_obj_t *parent, const char *text, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    set_ft(label, 17);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *create_textarea(lv_obj_t *parent, int x, int y, int w, const char *placeholder)
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

static void add_dialog_close_cb(lv_event_t *e)
{
    (void)e;
    hide_input_keyboard();
    if (add_dialog)
    {
        lv_obj_add_flag(add_dialog, LV_OBJ_FLAG_HIDDEN);
    }
}

static void add_dialog_save_cb(lv_event_t *e)
{
    (void)e;

    const char *name = lv_textarea_get_text(add_name_ta);
    const char *price_text = lv_textarea_get_text(add_price_ta);
    const char *stock_text = lv_textarea_get_text(add_stock_ta);
    const char *img_name = lv_textarea_get_text(add_img_ta);
    float price = 0.0f;
    int stock = 0;
    int ret;

    if (!ui_admin_goods_text_is_valid(name))
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_NAME_EMPTY);
        return;
    }

    if (img_name != NULL && img_name[0] != '\0' && !ui_admin_goods_text_is_valid(img_name))
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_BAD_CHAR);
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

    ret = ui_admin_goods_add(name, price, stock, img_name);
    if (ret == -3)
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_FULL);
        return;
    }
    if (ret == -4)
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_ALREADY_EXISTS);
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
    show_msg_box(TXT_SUCCESS, TXT_GOODS_ADD_SUCCESS);
}

void ui_admin_add_dialog_show(lv_event_t *e)
{
    (void)e;
    hide_input_keyboard();

    if (add_dialog)
    {
        lv_obj_clear_flag(add_dialog, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(add_name_ta, "");
        lv_textarea_set_text(add_price_ta, "");
        lv_textarea_set_text(add_stock_ta, "");
        lv_textarea_set_text(add_img_ta, "img_product_01");
        return;
    }

    add_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(add_dialog, 720, 210);
    lv_obj_set_pos(add_dialog, 40, 5);
    lv_obj_clear_flag(add_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(add_dialog, lv_color_hex(0x2C2C3A), 0);
    lv_obj_set_style_border_width(add_dialog, 2, 0);
    lv_obj_set_style_border_color(add_dialog, lv_color_hex(0x5A7FFF), 0);
    lv_obj_set_style_radius(add_dialog, 10, 0);

    lv_obj_t *title = lv_label_create(add_dialog);
    set_ft(title, 22);
    lv_label_set_text(title, TXT_ADD_GOODS_TITLE);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    create_label(add_dialog, TXT_GOODS_NAME, 40, 55);
    add_name_ta = create_textarea(add_dialog, 125, 45, 210, TXT_GOODS_NAME);

    create_label(add_dialog, TXT_GOODS_PRICE, 375, 55);
    add_price_ta = create_textarea(add_dialog, 460, 45, 170, TXT_GOODS_PRICE);
    lv_textarea_set_accepted_chars(add_price_ta, "0123456789.");

    create_label(add_dialog, TXT_STOCK, 40, 105);
    add_stock_ta = create_textarea(add_dialog, 125, 95, 210, TXT_STOCK);
    lv_textarea_set_accepted_chars(add_stock_ta, "0123456789");

    create_label(add_dialog, TXT_IMAGE_ID, 375, 105);
    add_img_ta = create_textarea(add_dialog, 460, 95, 170, TXT_DEFAULT_IMAGE_HINT);
    lv_textarea_set_text(add_img_ta, "img_product_01");

    lv_obj_t *save_btn = lv_btn_create(add_dialog);
    lv_obj_set_size(save_btn, 120, 36);
    lv_obj_set_pos(save_btn, 220, 160);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x226644), 0);
    lv_obj_add_event_cb(save_btn, add_dialog_save_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *save_label = lv_label_create(save_btn);
    set_ft(save_label, 18);
    lv_label_set_text(save_label, TXT_SAVE);
    lv_obj_center(save_label);

    lv_obj_t *close_btn = lv_btn_create(add_dialog);
    lv_obj_set_size(close_btn, 120, 36);
    lv_obj_set_pos(close_btn, 380, 160);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x664444), 0);
    lv_obj_add_event_cb(close_btn, add_dialog_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_btn);
    set_ft(close_label, 18);
    lv_label_set_text(close_label, TXT_CANCEL);
    lv_obj_center(close_label);

    /* 统一给当前管理员新增商品弹窗中的控件设置中文字体 */
    ui_apply_font_recursive(add_dialog, 18);
}
