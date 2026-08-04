/*
 * 文件名：ui_msgbox.c
 * 版本说明：答辩版中文注释。
 * 文件作用：提示框模块实现文件。用于显示成功、失败、错误等消息。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_msgbox.h"
#include "ui_font.h"
#include "lvgl/lvgl.h"

static lv_obj_t *msg_box = NULL;

static void show_msg_box_close_cb(lv_event_t *e)
{
    (void)e;
    msg_box = NULL;
}

void show_msg_box(const char *title, const char *msg)
{
    if (msg_box)
    {
        lv_obj_clear_flag(msg_box, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *title_lab = lv_msgbox_get_title(msg_box);
        lv_obj_t *text_lab = lv_msgbox_get_text(msg_box);

        lv_label_set_text(title_lab, title);
        lv_label_set_text(text_lab, msg);

        set_ft(title_lab, 22);
        set_ft(text_lab, 20);

        lv_obj_t *close_btn = lv_msgbox_get_close_btn(msg_box);
        lv_obj_t *btn_lbl = lv_obj_get_child(close_btn, 0);
        set_ft(btn_lbl, 18);
        return;
    }

    msg_box = lv_msgbox_create(NULL, title, msg, NULL, true);
    lv_obj_set_size(msg_box, 420, 200);
    lv_obj_center(msg_box);

    lv_obj_t *title_lab = lv_msgbox_get_title(msg_box);
    set_ft(title_lab, 22);

    lv_obj_t *text_lab = lv_msgbox_get_text(msg_box);
    set_ft(text_lab, 20);

    lv_obj_t *close_btn = lv_msgbox_get_close_btn(msg_box);
    lv_obj_t *btn_lbl = lv_obj_get_child(close_btn, 0);
    set_ft(btn_lbl, 18);

    lv_obj_add_event_cb(msg_box, show_msg_box_close_cb, LV_EVENT_DELETE, NULL);
}
