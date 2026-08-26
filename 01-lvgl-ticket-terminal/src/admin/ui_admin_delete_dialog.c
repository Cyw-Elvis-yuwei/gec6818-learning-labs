/*
 * 文件名：ui_admin_delete_dialog.c
 * 版本说明：中文注释。
 * 文件作用：删除商品窗口实现文件。按商品名删除商品并刷新列表。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_admin_delete_dialog.h"
#include "ui_admin_goods_store.h"
#include "ui_admin_page.h"
#include "ui_font.h"
#include "ui_goods_view.h"
#include "ui_keyboard.h"
#include "ui_msgbox.h"
#include "ui_text.h"
#include "lvgl/lvgl.h"

static lv_obj_t *delete_dialog = NULL;
static lv_obj_t *delete_name_ta = NULL;

static void delete_dialog_close_cb(lv_event_t *e)
{
    (void)e;
    hide_input_keyboard();
    if (delete_dialog)
    {
        lv_obj_add_flag(delete_dialog, LV_OBJ_FLAG_HIDDEN);
    }
}

static void delete_dialog_confirm_cb(lv_event_t *e)
{
    (void)e;

    const char *name = lv_textarea_get_text(delete_name_ta);
    int ret;

    if (!ui_admin_goods_text_is_valid(name))
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_NAME_EMPTY);
        return;
    }

    ret = ui_admin_goods_delete(name);
    if (ret == -2)
    {
        show_msg_box(TXT_PROMPT, TXT_GOODS_NOT_FOUND);
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
    show_msg_box(TXT_SUCCESS, TXT_GOODS_DELETE_SUCCESS);
}

void ui_admin_delete_dialog_show(lv_event_t *e)
{
    (void)e;
    hide_input_keyboard();

    if (delete_dialog)
    {
        lv_obj_clear_flag(delete_dialog, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(delete_name_ta, "");
        return;
    }

    delete_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(delete_dialog, 460, 180);
    lv_obj_set_pos(delete_dialog, 170, 20);
    lv_obj_clear_flag(delete_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(delete_dialog, lv_color_hex(0x2C2C3A), 0);
    lv_obj_set_style_border_width(delete_dialog, 2, 0);
    lv_obj_set_style_border_color(delete_dialog, lv_color_hex(0x5A7FFF), 0);
    lv_obj_set_style_radius(delete_dialog, 10, 0);

    lv_obj_t *title = lv_label_create(delete_dialog);
    set_ft(title, 22);
    lv_label_set_text(title, TXT_DELETE_GOODS_TITLE);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *name_label = lv_label_create(delete_dialog);
    set_ft(name_label, 18);
    lv_label_set_text(name_label, TXT_GOODS_NAME);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(name_label, 50, 65);

    delete_name_ta = lv_textarea_create(delete_dialog);
    lv_obj_set_size(delete_name_ta, 260, 36);
    lv_obj_set_pos(delete_name_ta, 150, 55);
    lv_textarea_set_one_line(delete_name_ta, true);
    lv_textarea_set_placeholder_text(delete_name_ta, TXT_GOODS_NAME);
    lv_obj_add_event_cb(delete_name_ta, ta_focus_cb, LV_EVENT_CLICKED, NULL);
    set_ft(delete_name_ta, 16);

    lv_obj_t *delete_btn = lv_btn_create(delete_dialog);
    lv_obj_set_size(delete_btn, 120, 36);
    lv_obj_set_pos(delete_btn, 90, 125);
    lv_obj_set_style_bg_color(delete_btn, lv_color_hex(0x884444), 0);
    lv_obj_add_event_cb(delete_btn, delete_dialog_confirm_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *delete_label = lv_label_create(delete_btn);
    set_ft(delete_label, 18);
    lv_label_set_text(delete_label, TXT_DELETE);
    lv_obj_center(delete_label);

    lv_obj_t *close_btn = lv_btn_create(delete_dialog);
    lv_obj_set_size(close_btn, 120, 36);
    lv_obj_set_pos(close_btn, 250, 125);
    lv_obj_add_event_cb(close_btn, delete_dialog_close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_btn);
    set_ft(close_label, 18);
    lv_label_set_text(close_label, TXT_CANCEL);
    lv_obj_center(close_label);

    /* 统一给当前管理员界面及其子控件设置 FreeType 中文字体，避免中文显示成方框。 */
    ui_apply_font_recursive(delete_dialog, 18);
}
