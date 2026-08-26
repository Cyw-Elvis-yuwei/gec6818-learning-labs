/*
 * 文件名：ui_pay_dialog.c
 * 版本说明：中文注释。
 * 文件作用：付款弹窗模块实现文件。显示支付金额和微信/支付宝二维码区域。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_pay_dialog.h"
#include "ui_font.h"
#include "ui_keyboard.h"
#include "ui_main_state.h"
#include "ui_msgbox.h"
#include "ui_qr_display.h"
#include "ui_text.h"
#include <stdio.h>
#include <stdlib.h>

/* Payment dialog state. The dialog is created once and reused on later opens. */
static lv_obj_t *pay_dialog = NULL;
static lv_obj_t *pay_total_label = NULL;
static lv_obj_t *pay_ta = NULL;
static float total_money = 0.0f;

/* Calculate the current shopping-cart total. */
static float calc_cart_total(void)
{
    float sum = 0.0f;

    for (int i = 0; i < cart_count; i++)
    {
        sum += cart[i].price * cart[i].qty;
    }

    return sum;
}

/* Hide the payment dialog and release the software keyboard. */
static void pay_cancel_cb(lv_event_t *e)
{
    (void)e;
    hide_input_keyboard();

    if (pay_dialog)
    {
        lv_obj_add_flag(pay_dialog, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Keep the original manual confirmation logic. QR codes are display-only. */
static void pay_confirm_cb(lv_event_t *e)
{
    (void)e;

    const char *txt = lv_textarea_get_text(pay_ta);
    float pay = atof(txt);

    if (pay <= 0)
    {
        hide_input_keyboard();
        show_msg_box(TXT_PROMPT, TXT_MONEY_ERR);
        return;
    }

    if (pay < total_money)
    {
        hide_input_keyboard();
        show_msg_box(TXT_PROMPT, TXT_PAY_INSUFFICIENT);
        return;
    }

    cart_count = 0;
    hide_input_keyboard();
    lv_obj_add_flag(pay_dialog, LV_OBJ_FLAG_HIDDEN);
    show_msg_box(TXT_SUCCESS, TXT_PAY_SUCCESS);
}

/* Open or reuse the payment dialog. The WeChat/Alipay QR blocks are visual only. */
void show_pay_dialog(lv_event_t *e)
{
    (void)e;

    total_money = calc_cart_total();

    if (cart_count <= 0)
    {
        show_msg_box(TXT_PROMPT, TXT_CART_EMPTY);
        return;
    }

    char buf[80];
    snprintf(buf, sizeof(buf), "%s:%.2f%s", TXT_TOTAL, (double)total_money, TXT_YUAN);

    if (pay_dialog)
    {
        lv_obj_clear_flag(pay_dialog, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(pay_dialog, 80, 5);

        if (pay_total_label)
        {
            lv_label_set_text(pay_total_label, buf);
        }

        lv_textarea_set_text(pay_ta, "");
        return;
    }

    /* Keep the dialog above the keyboard area. Keyboard starts at y=216. */
    pay_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(pay_dialog, 640, 210);
    lv_obj_set_pos(pay_dialog, 80, 5);
    lv_obj_clear_flag(pay_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(pay_dialog, lv_color_hex(0x2C2C3A), 0);
    lv_obj_set_style_border_width(pay_dialog, 2, 0);
    lv_obj_set_style_border_color(pay_dialog, lv_color_hex(0x5A7FFF), 0);
    lv_obj_set_style_radius(pay_dialog, 10, 0);
    lv_obj_set_style_pad_all(pay_dialog, 0, 0);

    pay_total_label = lv_label_create(pay_dialog);
    set_ft(pay_total_label, 22);
    lv_label_set_text(pay_total_label, buf);
    lv_obj_set_style_text_color(pay_total_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(pay_total_label, 35, 15);

    pay_ta = lv_textarea_create(pay_dialog);
    lv_obj_set_size(pay_ta, 220, 42);
    lv_obj_set_pos(pay_ta, 35, 58);
    lv_obj_add_event_cb(pay_ta, ta_focus_cb, LV_EVENT_CLICKED, NULL);
    lv_textarea_set_placeholder_text(pay_ta, TXT_INPUT_MONEY);
    lv_textarea_set_one_line(pay_ta, true);
    set_ft(pay_ta, 18);

    lv_obj_t *btn_pay = lv_btn_create(pay_dialog);
    lv_obj_set_size(btn_pay, 100, 38);
    lv_obj_set_pos(btn_pay, 35, 150);
    lv_obj_add_event_cb(btn_pay, pay_confirm_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *pay_lbl = lv_label_create(btn_pay);
    set_ft(pay_lbl, 18);
    lv_label_set_text(pay_lbl, TXT_CONFIRM);
    lv_obj_center(pay_lbl);

    lv_obj_t *btn_pay_cancel = lv_btn_create(pay_dialog);
    lv_obj_set_size(btn_pay_cancel, 100, 38);
    lv_obj_set_pos(btn_pay_cancel, 155, 150);
    lv_obj_add_event_cb(btn_pay_cancel, pay_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *pay_cl_lbl = lv_label_create(btn_pay_cancel);
    set_ft(pay_cl_lbl, 18);
    lv_label_set_text(pay_cl_lbl, TXT_CANCEL);
    lv_obj_center(pay_cl_lbl);

    /* Instruction 9: payment QR images are display-only and do not change payment logic. */
    ui_qr_display_create(pay_dialog, 360, 45, "WeChat Pay", UI_QR_WECHAT);
    ui_qr_display_create(pay_dialog, 495, 45, "Alipay", UI_QR_ALIPAY);
}
