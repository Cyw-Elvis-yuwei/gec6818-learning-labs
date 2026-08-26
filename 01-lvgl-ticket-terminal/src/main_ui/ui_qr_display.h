/*
 * 文件名：ui_qr_display.h
 * 版本说明：中文注释。
 * 文件作用：二维码显示模块头文件。声明二维码绘制/显示函数。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#ifndef UI_QR_DISPLAY_H
#define UI_QR_DISPLAY_H

#include "lvgl/lvgl.h"

/* Two static QR-like display variants used inside the payment dialog. */
typedef enum
{
    UI_QR_WECHAT = 0,
    UI_QR_ALIPAY = 1
} ui_qr_type_t;

/*
 * Create a non-interactive QR-code display block.
 * The function draws a QR-like image using LVGL rectangles, so no external PNG/C image asset is required.
 */
lv_obj_t *ui_qr_display_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const char *title, ui_qr_type_t type);

#endif /* UI_QR_DISPLAY_H */
