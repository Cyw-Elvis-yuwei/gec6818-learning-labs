/*
 * 文件名：ui_admin_dialog.c
 * 版本说明：中文注释。
 * 文件作用：管理员登录弹窗实现文件。输入账号密码，验证成功后进入商品管理页面。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_admin_dialog.h"
#include "ui_admin_page.h"
#include "ui_font.h"
#include "ui_keyboard.h"
#include "ui_msgbox.h"
#include "ui_text.h"
#include "lvgl/lvgl.h"
#include <string.h>

#define ADMIN_ACCOUNT  "admin"
#define ADMIN_PASSWORD "123456"

static lv_obj_t *admin_dialog = NULL;
static lv_obj_t *admin_account_ta = NULL;
static lv_obj_t *admin_password_ta = NULL;

static void admin_cancel_cb(lv_event_t *e)
{
    (void)e;
    hide_input_keyboard();

    if (admin_dialog)
    {
        lv_obj_add_flag(admin_dialog, LV_OBJ_FLAG_HIDDEN);
    }
}

static void admin_confirm_cb(lv_event_t *e)
{
    (void)e;

    const char *account = lv_textarea_get_text(admin_account_ta);
    const char *password = lv_textarea_get_text(admin_password_ta);

    if (strcmp(account, ADMIN_ACCOUNT) != 0 || strcmp(password, ADMIN_PASSWORD) != 0)
    {
        hide_input_keyboard();
        show_msg_box(TXT_PROMPT, TXT_ADMIN_LOGIN_FAILED);
        return;
    }

    hide_input_keyboard();

    if (admin_dialog)
    {
        lv_obj_add_flag(admin_dialog, LV_OBJ_FLAG_HIDDEN);
    }

    show_msg_box(TXT_SUCCESS, TXT_ADMIN_LOGIN_SUCCESS);
    ui_admin_page_show();
}

void show_admin_login_dialog(lv_event_t *e)
{
    (void)e;
    hide_input_keyboard();

    if (admin_dialog)
    {
        lv_obj_clear_flag(admin_dialog, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(admin_dialog, 190, 5);
        lv_textarea_set_text(admin_account_ta, "");
        lv_textarea_set_text(admin_password_ta, "");
        return;
    }

    admin_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(admin_dialog, 420, 210);
    lv_obj_set_pos(admin_dialog, 190, 5);
    lv_obj_clear_flag(admin_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(admin_dialog, lv_color_hex(0x2C2C3A), 0);
    lv_obj_set_style_border_width(admin_dialog, 2, 0);
    lv_obj_set_style_border_color(admin_dialog, lv_color_hex(0x5A7FFF), 0);
    lv_obj_set_style_radius(admin_dialog, 10, 0);

    lv_obj_t *title = lv_label_create(admin_dialog);
    set_ft(title, 22);
    lv_label_set_text(title, TXT_ADMIN_LOGIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *account_label = lv_label_create(admin_dialog);
    set_ft(account_label, 20);
    lv_label_set_text(account_label, TXT_ACCOUNT);
    lv_obj_set_style_text_color(account_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(account_label, 55, 62);

    admin_account_ta = lv_textarea_create(admin_dialog);
    lv_obj_set_size(admin_account_ta, 220, 38);
    lv_obj_set_pos(admin_account_ta, 145, 52);
    lv_textarea_set_one_line(admin_account_ta, true);
    lv_textarea_set_placeholder_text(admin_account_ta, TXT_ACCOUNT);
    lv_obj_add_event_cb(admin_account_ta, ta_focus_cb, LV_EVENT_CLICKED, NULL);
    set_ft(admin_account_ta, 18);

    lv_obj_t *password_label = lv_label_create(admin_dialog);
    set_ft(password_label, 20);
    lv_label_set_text(password_label, TXT_PASSWORD);
    lv_obj_set_style_text_color(password_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(password_label, 55, 112);

    admin_password_ta = lv_textarea_create(admin_dialog);
    lv_obj_set_size(admin_password_ta, 220, 38);
    lv_obj_set_pos(admin_password_ta, 145, 102);
    lv_textarea_set_one_line(admin_password_ta, true);
    lv_textarea_set_password_mode(admin_password_ta, true);
    lv_textarea_set_placeholder_text(admin_password_ta, TXT_PASSWORD);
    lv_obj_add_event_cb(admin_password_ta, ta_focus_cb, LV_EVENT_CLICKED, NULL);
    set_ft(admin_password_ta, 18);

    lv_obj_t *btn_confirm = lv_btn_create(admin_dialog);
    lv_obj_set_size(btn_confirm, 120, 36);
    lv_obj_set_pos(btn_confirm, 70, 160);
    lv_obj_add_event_cb(btn_confirm, admin_confirm_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *confirm_label = lv_label_create(btn_confirm);
    set_ft(confirm_label, 18);
    lv_label_set_text(confirm_label, TXT_CONFIRM);
    lv_obj_center(confirm_label);

    lv_obj_t *btn_cancel = lv_btn_create(admin_dialog);
    lv_obj_set_size(btn_cancel, 120, 36);
    lv_obj_set_pos(btn_cancel, 230, 160);
    lv_obj_add_event_cb(btn_cancel, admin_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_label = lv_label_create(btn_cancel);
    set_ft(cancel_label, 18);
    lv_label_set_text(cancel_label, TXT_CANCEL);
    lv_obj_center(cancel_label);

    /* 统一给当前管理员界面及其子控件设置 FreeType 中文字体，避免中文显示成方框。 */
    ui_apply_font_recursive(admin_dialog, 18);
}
