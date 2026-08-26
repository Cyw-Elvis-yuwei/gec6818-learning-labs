/*
 * 文件名：ui_goods_view.c
 * 版本说明：中文注释。
 * 文件作用：商品展示模块实现文件。负责展示商品图片、名称、售价、库存和翻页。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_goods_view.h"
#include "ui_buy_dialog.h"
#include "ui_admin_dialog.h"
#include "ui_font.h"
#include "ui_main_state.h"
#include "ui_pay_dialog.h"
#include "ui_text.h"
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <string.h>

LV_IMG_DECLARE(img_product_01);
LV_IMG_DECLARE(img_product_02);
LV_IMG_DECLARE(img_product_03);
LV_IMG_DECLARE(img_product_04);
LV_IMG_DECLARE(img_product_05);
LV_IMG_DECLARE(img_product_06);
LV_IMG_DECLARE(img_product_07);
LV_IMG_DECLARE(img_product_08);

#define GOODS_IMG_AREA_W 185
#define GOODS_IMG_AREA_H 160

static uint16_t get_contain_zoom(const lv_img_dsc_t *img)
{
    uint32_t zoom_w;
    uint32_t zoom_h;
    uint32_t zoom;

    if (img == NULL || img->header.w == 0 || img->header.h == 0)
    {
        return 256;
    }

    zoom_w = (GOODS_IMG_AREA_W * 256U) / img->header.w;
    zoom_h = (GOODS_IMG_AREA_H * 256U) / img->header.h;
    zoom = zoom_w < zoom_h ? zoom_w : zoom_h;

    if (zoom < 1)
    {
        zoom = 1;
    }
    if (zoom > 512)
    {
        zoom = 512;
    }

    return (uint16_t)zoom;
}

static const lv_img_dsc_t *get_img_by_name(const char *name)
{
    if (strcmp(name, "img_product_01") == 0)
    {
        return &img_product_01;
    }
    if (strcmp(name, "img_product_02") == 0)
    {
        return &img_product_02;
    }
    if (strcmp(name, "img_product_03") == 0)
    {
        return &img_product_03;
    }
    if (strcmp(name, "img_product_04") == 0)
    {
        return &img_product_04;
    }
    if (strcmp(name, "img_product_05") == 0)
    {
        return &img_product_05;
    }
    if (strcmp(name, "img_product_06") == 0)
    {
        return &img_product_06;
    }
    if (strcmp(name, "img_product_07") == 0)
    {
        return &img_product_07;
    }
    if (strcmp(name, "img_product_08") == 0)
    {
        return &img_product_08;
    }

    return NULL;
}

static lv_obj_t *create_frame(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *f = lv_obj_create(parent);
    lv_obj_set_size(f, w, h);
    lv_obj_set_pos(f, x, y);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(f, lv_color_hex(0x1E1E2E), 0);
    lv_obj_set_style_border_width(f, 2, 0);
    lv_obj_set_style_border_color(f, lv_color_hex(0x5A7FFF), 0);
    lv_obj_set_style_radius(f, 10, 0);
    lv_obj_set_style_pad_all(f, 6, 0);
    return f;
}

void refresh_page(void)
{
    char buf[40];
    int start = current_page * 4;

    for (int i = 0; i < 4; i++)
    {
        int idx = start + i;

        if (idx < goods.count)
        {
            lv_obj_clear_flag(frames[i], LV_OBJ_FLAG_HIDDEN);

            const lv_img_dsc_t *img = get_img_by_name(goods.items[idx].img_name);
            if (img)
            {
                lv_img_set_src(frame_imgs[i], img);
                lv_img_set_zoom(frame_imgs[i], get_contain_zoom(img));
                lv_obj_center(frame_imgs[i]);
            }

            lv_label_set_text(frame_names[i], goods.items[idx].scenic);

            snprintf(buf, sizeof(buf), "%.2f%s", (double)goods.items[idx].price, TXT_YUAN);
            lv_label_set_text(frame_prices[i], buf);

            snprintf(buf, sizeof(buf), "%s: %d", TXT_STOCK, goods.items[idx].stock);
            lv_label_set_text(frame_stocks[i], buf);
        }
        else
        {
            lv_obj_add_flag(frames[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    snprintf(buf, sizeof(buf), "%s%d/%d%s", TXT_PAGE, current_page + 1, total_pages, TXT_PAGE_END);
    lv_label_set_text(page_label, buf);
}

static void prev_click_cb(lv_event_t *e)
{
    (void)e;

    if (current_page > 0)
    {
        current_page--;
        refresh_page();
    }
}

static void next_click_cb(lv_event_t *e)
{
    (void)e;

    if (current_page < total_pages - 1)
    {
        current_page++;
        refresh_page();
    }
}

static void frame_click_cb(lv_event_t *e)
{
    lv_obj_t *f = lv_event_get_target(e);

    for (int i = 0; i < 4; i++)
    {
        if (frames[i] == f)
        {
            int idx = current_page * 4 + i;
            if (idx < goods.count)
            {
                show_buy_dialog(idx);
            }
            break;
        }
    }
}

void ui_goods_view_create(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    set_ft(title, 28);
    lv_label_set_text(title, TXT_SCREEN_TITLE);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    page_label = lv_label_create(parent);
    set_ft(page_label, 18);
    lv_obj_set_style_text_color(page_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(page_label, LV_ALIGN_TOP_RIGHT, -20, 14);

    frames[0] = create_frame(parent, 25, 55, 365, 195);
    frames[1] = create_frame(parent, 410, 55, 365, 195);
    frames[2] = create_frame(parent, 25, 265, 365, 195);
    frames[3] = create_frame(parent, 410, 265, 365, 195);

    for (int i = 0; i < 4; i++)
    {
        lv_obj_t *f = frames[i];

        lv_obj_t *img_area = lv_obj_create(f);
        lv_obj_set_pos(img_area, 10, 18);
        lv_obj_set_size(img_area, GOODS_IMG_AREA_W, GOODS_IMG_AREA_H);
        lv_obj_clear_flag(img_area, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(img_area, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(img_area, 0, 0);
        lv_obj_set_style_border_width(img_area, 0, 0);
        lv_obj_set_style_radius(img_area, 6, 0);
        lv_obj_set_style_bg_color(img_area, lv_color_hex(0x111122), 0);
        lv_obj_set_style_clip_corner(img_area, true, 0);

        frame_imgs[i] = lv_img_create(img_area);
        lv_obj_center(frame_imgs[i]);

        frame_names[i] = lv_label_create(f);
        set_ft(frame_names[i], 20);
        lv_obj_set_style_text_color(frame_names[i], lv_color_hex(0xFFD700), 0);
        lv_obj_set_width(frame_names[i], 140);
        lv_label_set_long_mode(frame_names[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(frame_names[i], 210, 25);

        frame_prices[i] = lv_label_create(f);
        set_ft(frame_prices[i], 18);
        lv_obj_set_style_text_color(frame_prices[i], lv_color_hex(0xFF6347), 0);
        lv_obj_set_pos(frame_prices[i], 210, 85);

        frame_stocks[i] = lv_label_create(f);
        set_ft(frame_stocks[i], 14);
        lv_obj_set_style_text_color(frame_stocks[i], lv_color_hex(0x9AA0AA), 0);
        lv_obj_set_pos(frame_stocks[i], 210, 125);

        lv_obj_add_event_cb(frames[i], frame_click_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *prev_btn = lv_btn_create(parent);
    lv_obj_set_size(prev_btn, 110, 36);
    lv_obj_align(prev_btn, LV_ALIGN_BOTTOM_LEFT, 25, -10);
    lv_obj_set_style_bg_color(prev_btn, lv_color_hex(0x444466), 0);
    lv_obj_set_style_radius(prev_btn, 6, 0);

    lv_obj_t *prev_lbl = lv_label_create(prev_btn);
    set_ft(prev_lbl, 18);
    lv_label_set_text(prev_lbl, TXT_PRE_PAGE);
    lv_obj_center(prev_lbl);
    lv_obj_add_event_cb(prev_btn, prev_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *next_btn = lv_btn_create(parent);
    lv_obj_set_size(next_btn, 110, 36);
    lv_obj_align(next_btn, LV_ALIGN_BOTTOM_RIGHT, -25, -10);
    lv_obj_set_style_bg_color(next_btn, lv_color_hex(0x444466), 0);
    lv_obj_set_style_radius(next_btn, 6, 0);

    lv_obj_t *next_lbl = lv_label_create(next_btn);
    set_ft(next_lbl, 18);
    lv_label_set_text(next_lbl, TXT_NEXT_PAGE);
    lv_obj_center(next_lbl);
    lv_obj_add_event_cb(next_btn, next_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *pay_btn = lv_btn_create(parent);
    lv_obj_set_size(pay_btn, 110, 36);
    lv_obj_align(pay_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(pay_btn, lv_color_hex(0x226644), 0);
    lv_obj_set_style_radius(pay_btn, 6, 0);

    lv_obj_t *pay_btn_lbl = lv_label_create(pay_btn);
    set_ft(pay_btn_lbl, 18);
    lv_label_set_text(pay_btn_lbl, TXT_PAY);
    lv_obj_center(pay_btn_lbl);
    lv_obj_add_event_cb(pay_btn, show_pay_dialog, LV_EVENT_CLICKED, NULL);

    lv_obj_t *admin_btn = lv_btn_create(parent);
    lv_obj_set_size(admin_btn, 110, 36);
    lv_obj_set_pos(admin_btn, 25, 10);
    lv_obj_set_style_bg_color(admin_btn, lv_color_hex(0x664422), 0);
    lv_obj_set_style_radius(admin_btn, 6, 0);

    lv_obj_t *admin_btn_lbl = lv_label_create(admin_btn);
    set_ft(admin_btn_lbl, 18);
    lv_label_set_text(admin_btn_lbl, TXT_ADMIN);
    lv_obj_center(admin_btn_lbl);
    lv_obj_add_event_cb(admin_btn, show_admin_login_dialog, LV_EVENT_CLICKED, NULL);
}
