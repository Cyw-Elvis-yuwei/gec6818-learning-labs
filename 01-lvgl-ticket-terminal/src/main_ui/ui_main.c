/*
 * 文件名：ui_main.c
 * 版本说明：中文注释。
 * 文件作用：主页面入口实现文件。初始化商品数据、创建主页面、接入管理员按钮。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_main.h"
#include "file_goods.h"
#include "ui_goods_view.h"
#include "ui_keyboard.h"
#include "ui_main_state.h"
#include "lvgl/lvgl.h"

static void screen_click_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    lv_obj_t *current = lv_event_get_current_target(e);

    if (target == current)
    {
        hide_input_keyboard();
    }
}

void ui_main_create(void)
{
    if (file_goods_read_all(&goods) != 0)
    {
        return;
    }

    total_pages = (goods.count + 3) / 4;

    main_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(main_screen, 800, 480);
    lv_obj_set_pos(main_screen, 0, 0);
    lv_obj_add_flag(main_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(main_screen, screen_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(main_screen, 0, 0);
    lv_obj_set_style_pad_all(main_screen, 0, 0);
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x0D0D1A), 0);

    ui_goods_view_create(main_screen);
    refresh_page();
}
