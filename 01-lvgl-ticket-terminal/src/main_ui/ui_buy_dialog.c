/*
 * 文件名：ui_buy_dialog.c
 * 版本说明：答辩版中文注释。
 * 文件作用：购买弹窗模块实现文件。通过加减按钮选择数量，并加入购物车。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_buy_dialog.h"
#include "file_goods.h"
#include "ui_font.h"
#include "ui_goods_view.h"
#include "ui_keyboard.h"
#include "ui_main_state.h"
#include "ui_msgbox.h"
#include "ui_text.h"
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *buy_dialog = NULL;
static lv_obj_t *buy_title_label = NULL;
static lv_obj_t *buy_qty_label = NULL;
static lv_obj_t *buy_stock_label = NULL;
static lv_obj_t *buy_subtotal_label = NULL;
static int buy_qty = 1;
static int buy_scenic_idx = -1;

static void update_buy_qty_view(void)
{
    if (buy_scenic_idx < 0 || buy_scenic_idx >= goods.count)
    {
        return;
    }

    int stock = goods.items[buy_scenic_idx].stock;

    if (stock <= 0)
    {
        buy_qty = 0;
    }
    else
    {
        if (buy_qty < 1)
        {
            buy_qty = 1;
        }

        if (buy_qty > stock)
        {
            buy_qty = stock;
        }
    }

    char buf[128];

    if (buy_qty_label)
    {
        snprintf(buf, sizeof(buf), "%d", buy_qty);
        lv_label_set_text(buy_qty_label, buf);
    }

    if (buy_stock_label)
    {
        snprintf(buf, sizeof(buf), "%s:%d", TXT_STOCK, stock);
        lv_label_set_text(buy_stock_label, buf);
    }

    if (buy_subtotal_label)
    {
        float subtotal = goods.items[buy_scenic_idx].price * buy_qty;
        snprintf(buf, sizeof(buf), "%s:%.2f%s", TXT_SUBTOTAL, (double)subtotal, TXT_YUAN);
        lv_label_set_text(buy_subtotal_label, buf);
    }
}

static void buy_minus_cb(lv_event_t *e)
{
    (void)e;

    if (buy_qty > 1)
    {
        buy_qty--;
        update_buy_qty_view();
    }
}

static void buy_plus_cb(lv_event_t *e)
{
    (void)e;

    if (buy_scenic_idx < 0 || buy_scenic_idx >= goods.count)
    {
        return;
    }

    int stock = goods.items[buy_scenic_idx].stock;

    if (buy_qty >= stock)
    {
        show_msg_box(TXT_PROMPT, TXT_CANNOT_OVER_STOCK);
        return;
    }

    buy_qty++;
    update_buy_qty_view();
}

static void buy_confirm_cb(lv_event_t *e)
{
    (void)e;

    int idx = buy_scenic_idx;
    int qty = buy_qty;

    if (idx < 0 || idx >= goods.count)
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_DATA_ERROR);
        return;
    }

    if (qty <= 0)
    {
        show_msg_box(TXT_PROMPT, TXT_PLEASE_SELECT_VALID_QUANTITY);
        return;
    }

    if (qty > goods.items[idx].stock)
    {
        show_msg_box(TXT_PROMPT, TXT_INSUFFICIENT_STOCK);
        return;
    }

    if (cart_count >= 20)
    {
        show_msg_box(TXT_PROMPT, TXT_CART_FULL);
        return;
    }

    goods.items[idx].stock -= qty;
    file_goods_update_stock(goods.items[idx].scenic, goods.items[idx].stock);

    strncpy(cart[cart_count].scenic, goods.items[idx].scenic, 39);
    cart[cart_count].scenic[39] = '\0';
    cart[cart_count].price = goods.items[idx].price;
    cart[cart_count].qty = qty;
    cart_count++;

    if (buy_dialog)
    {
        lv_obj_add_flag(buy_dialog, LV_OBJ_FLAG_HIDDEN);
    }

    refresh_page();
    show_msg_box(TXT_SUCCESS, TXT_ADDED_TO_CART);
}

static void buy_cancel_cb(lv_event_t *e)
{
    (void)e;

    if (buy_dialog)
    {
        lv_obj_add_flag(buy_dialog, LV_OBJ_FLAG_HIDDEN);
    }
}

void show_buy_dialog(int scenic_idx)
{
    if (scenic_idx < 0 || scenic_idx >= goods.count)
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_DATA_ERROR);
        return;
    }

    if (goods.items[scenic_idx].stock <= 0)
    {
        show_msg_box(TXT_PROMPT, TXT_INSUFFICIENT_STOCK);
        return;
    }

    hide_input_keyboard();

    buy_scenic_idx = scenic_idx;
    buy_qty = 1;

    char buf[128];

    if (buy_dialog)
    {
        lv_obj_clear_flag(buy_dialog, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(buy_dialog, 200, 80);

        snprintf(buf,
                 sizeof(buf),
                 "%s  %.2f%s",
                 goods.items[scenic_idx].scenic,
                 (double)goods.items[scenic_idx].price,
                 TXT_YUAN);

        if (buy_title_label)
        {
            lv_label_set_text(buy_title_label, buf);
        }

        update_buy_qty_view();
        return;
    }

    buy_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(buy_dialog, 400, 240);
    lv_obj_set_pos(buy_dialog, 200, 80);
    lv_obj_clear_flag(buy_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(buy_dialog, lv_color_hex(0x2C2C3A), 0);
    lv_obj_set_style_border_width(buy_dialog, 2, 0);
    lv_obj_set_style_border_color(buy_dialog, lv_color_hex(0x5A7FFF), 0);
    lv_obj_set_style_radius(buy_dialog, 10, 0);

    buy_title_label = lv_label_create(buy_dialog);
    set_ft(buy_title_label, 22);
    snprintf(buf,
             sizeof(buf),
             "%s  %.2f%s",
             goods.items[scenic_idx].scenic,
             (double)goods.items[scenic_idx].price,
             TXT_YUAN);
    lv_label_set_text(buy_title_label, buf);
    lv_obj_set_style_text_color(buy_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(buy_title_label, LV_ALIGN_TOP_MID, 0, 15);

    buy_stock_label = lv_label_create(buy_dialog);
    set_ft(buy_stock_label, 18);
    lv_obj_set_style_text_color(buy_stock_label, lv_color_hex(0x90EE90), 0);
    lv_obj_align(buy_stock_label, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t *btn_minus = lv_btn_create(buy_dialog);
    lv_obj_set_size(btn_minus, 60, 50);
    lv_obj_set_pos(btn_minus, 80, 85);
    lv_obj_set_style_bg_color(btn_minus, lv_color_hex(0x555577), 0);
    lv_obj_set_style_radius(btn_minus, 8, 0);
    lv_obj_add_event_cb(btn_minus, buy_minus_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *minus_lbl = lv_label_create(btn_minus);
    set_ft(minus_lbl, 28);
    lv_label_set_text(minus_lbl, "-");
    lv_obj_center(minus_lbl);

    buy_qty_label = lv_label_create(buy_dialog);
    set_ft(buy_qty_label, 32);
    lv_obj_set_style_text_color(buy_qty_label, lv_color_hex(0xFFD700), 0);
    lv_obj_set_size(buy_qty_label, 100, 50);
    lv_obj_set_pos(buy_qty_label, 150, 92);
    lv_obj_set_style_text_align(buy_qty_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *btn_plus = lv_btn_create(buy_dialog);
    lv_obj_set_size(btn_plus, 60, 50);
    lv_obj_set_pos(btn_plus, 260, 85);
    lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0x226644), 0);
    lv_obj_set_style_radius(btn_plus, 8, 0);
    lv_obj_add_event_cb(btn_plus, buy_plus_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *plus_lbl = lv_label_create(btn_plus);
    set_ft(plus_lbl, 28);
    lv_label_set_text(plus_lbl, "+");
    lv_obj_center(plus_lbl);

    buy_subtotal_label = lv_label_create(buy_dialog);
    set_ft(buy_subtotal_label, 20);
    lv_obj_set_style_text_color(buy_subtotal_label, lv_color_hex(0xFFCC66), 0);
    lv_obj_align(buy_subtotal_label, LV_ALIGN_TOP_MID, 0, 145);

    lv_obj_t *btn_cf = lv_btn_create(buy_dialog);
    lv_obj_set_size(btn_cf, 120, 42);
    lv_obj_set_pos(btn_cf, 65, 185);

    lv_obj_t *cf_lbl = lv_label_create(btn_cf);
    set_ft(cf_lbl, 20);
    lv_label_set_text(cf_lbl, TXT_CONFIRM);
    lv_obj_center(cf_lbl);
    lv_obj_add_event_cb(btn_cf, buy_confirm_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_cl = lv_btn_create(buy_dialog);
    lv_obj_set_size(btn_cl, 120, 42);
    lv_obj_set_pos(btn_cl, 215, 185);

    lv_obj_t *cl_lbl = lv_label_create(btn_cl);
    set_ft(cl_lbl, 20);
    lv_label_set_text(cl_lbl, TXT_CANCEL);
    lv_obj_center(cl_lbl);
    lv_obj_add_event_cb(btn_cl, buy_cancel_cb, LV_EVENT_CLICKED, NULL);

    update_buy_qty_view();
}
