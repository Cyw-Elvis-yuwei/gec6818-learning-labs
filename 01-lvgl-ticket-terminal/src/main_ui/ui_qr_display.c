/*
 * 文件名：ui_qr_display.c
 * 版本说明：答辩版中文注释。
 * 文件作用：二维码显示模块实现文件。负责支付页面二维码区域显示。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_qr_display.h"
#include "ui_font.h"
#include "lvgl/lvgl.h"
#include <stdint.h>

#define QR_GRID_SIZE  9
#define QR_CELL_SIZE  8
#define QR_PANEL_SIZE 84
#define QR_PANEL_PAD  6

/*
 * These two bitmaps are display-only QR-like patterns.
 * They are not intended to encode real payment URLs.
 */
static const uint16_t wechat_pattern[QR_GRID_SIZE] = {
    0x1D7, /* 111010111 */
    0x145, /* 101000101 */
    0x1D7, /* 111010111 */
    0x028, /* 000101000 */
    0x1BA, /* 110111010 */
    0x04C, /* 001001100 */
    0x1D7, /* 111010111 */
    0x101, /* 100000001 */
    0x1D7  /* 111010111 */
};

static const uint16_t alipay_pattern[QR_GRID_SIZE] = {
    0x1D7, /* 111010111 */
    0x101, /* 100000001 */
    0x1D7, /* 111010111 */
    0x16A, /* 101101010 */
    0x0B5, /* 010110101 */
    0x092, /* 010010010 */
    0x1D7, /* 111010111 */
    0x145, /* 101000101 */
    0x1D7  /* 111010111 */
};

static void create_black_cell(lv_obj_t *parent, int col, int row)
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_set_size(cell, QR_CELL_SIZE, QR_CELL_SIZE);
    lv_obj_set_pos(cell, QR_PANEL_PAD + col * QR_CELL_SIZE, QR_PANEL_PAD + row * QR_CELL_SIZE);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(cell, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cell, 0, 0);
    lv_obj_set_style_radius(cell, 0, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
}

static void draw_qr_pattern(lv_obj_t *panel, const uint16_t pattern[QR_GRID_SIZE])
{
    for (int row = 0; row < QR_GRID_SIZE; row++)
    {
        for (int col = 0; col < QR_GRID_SIZE; col++)
        {
            int bit = (pattern[row] >> (QR_GRID_SIZE - 1 - col)) & 0x01;
            if (bit)
            {
                create_black_cell(panel, col, row);
            }
        }
    }
}

lv_obj_t *ui_qr_display_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const char *title, ui_qr_type_t type)
{
    lv_obj_t *wrapper = lv_obj_create(parent);
    lv_obj_set_size(wrapper, 108, 118);
    lv_obj_set_pos(wrapper, x, y);
    lv_obj_clear_flag(wrapper, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(wrapper, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(wrapper, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_style_pad_all(wrapper, 0, 0);

    lv_obj_t *title_label = lv_label_create(wrapper);
    set_ft(title_label, 16);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *panel = lv_obj_create(wrapper);
    lv_obj_set_size(panel, QR_PANEL_SIZE, QR_PANEL_SIZE);
    lv_obj_set_pos(panel, 12, 28);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xDDDDDD), 0);
    lv_obj_set_style_radius(panel, 3, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);

    draw_qr_pattern(panel, type == UI_QR_WECHAT ? wechat_pattern : alipay_pattern);
    return wrapper;
}
