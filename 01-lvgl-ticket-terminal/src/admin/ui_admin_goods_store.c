/*
 * 文件名：ui_admin_goods_store.c
 * 版本说明：答辩版中文注释。
 * 文件作用：管理员商品数据操作实现文件。负责操作 goods 数组并写回 goods_utf8.txt。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_admin_goods_store.h"
#include "ui_main_state.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep this path aligned with the file_goods module. */
#ifndef GOODS_FILE_PATH
#define GOODS_FILE_PATH "goods_utf8.txt"
#endif

/* The default image is used when the administrator leaves image id empty. */
#define UI_DEFAULT_PRODUCT_IMAGE "img_product_01"

/* Reject separators used by goods_utf8.txt. */
int ui_admin_goods_text_is_valid(const char *text)
{
    if (text == NULL || text[0] == '\0')
    {
        return 0;
    }

    for (const char *p = text; *p != '\0'; p++)
    {
        if (*p == '|' || *p == '\n' || *p == '\r')
        {
            return 0;
        }
    }

    return 1;
}

int ui_admin_goods_get_capacity(void)
{
    return (int)(sizeof(goods.items) / sizeof(goods.items[0]));
}

void ui_admin_goods_update_pages(void)
{
    total_pages = (goods.count + 3) / 4;
    if (total_pages <= 0)
    {
        total_pages = 1;
    }

    if (current_page >= total_pages)
    {
        current_page = total_pages - 1;
    }

    if (current_page < 0)
    {
        current_page = 0;
    }
}

int ui_admin_parse_price(const char *text, float *out_value)
{
    char *end = NULL;
    double value;

    if (text == NULL || text[0] == '\0' || out_value == NULL)
    {
        return -1;
    }

    errno = 0;
    value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || value < 0.0)
    {
        return -1;
    }

    *out_value = (float)value;
    return 0;
}

int ui_admin_parse_stock(const char *text, int *out_value)
{
    char *end = NULL;
    long value;

    if (text == NULL || text[0] == '\0' || out_value == NULL)
    {
        return -1;
    }

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0)
    {
        return -1;
    }

    *out_value = (int)value;
    return 0;
}

int ui_admin_goods_find_by_name(const char *name)
{
    if (name == NULL)
    {
        return -1;
    }

    for (int i = 0; i < goods.count; i++)
    {
        if (strcmp(goods.items[i].scenic, name) == 0)
        {
            return i;
        }
    }

    return -1;
}

int ui_admin_goods_save_all(void)
{
    FILE *fp = fopen(GOODS_FILE_PATH, "w");

    /* The board demo is sometimes launched from /. Keep a fallback path. */
    if (fp == NULL)
    {
        fp = fopen("/goods_utf8.txt", "w");
    }

    if (fp == NULL)
    {
        return -1;
    }

    for (int i = 0; i < goods.count; i++)
    {
        fprintf(fp,
                "%s|%.2f|%d|%s\n",
                goods.items[i].scenic,
                (double)goods.items[i].price,
                goods.items[i].stock,
                goods.items[i].img_name);
    }

    fclose(fp);
    return 0;
}

int ui_admin_goods_add(const char *name, float price, int stock, const char *img_name)
{
    int capacity = ui_admin_goods_get_capacity();

    if (!ui_admin_goods_text_is_valid(name))
    {
        return -2;
    }

    if (goods.count >= capacity)
    {
        return -3;
    }

    if (ui_admin_goods_find_by_name(name) >= 0)
    {
        return -4;
    }

    int index = goods.count;
    memset(&goods.items[index], 0, sizeof(goods.items[index]));

    strncpy(goods.items[index].scenic, name, sizeof(goods.items[index].scenic) - 1);
    goods.items[index].price = price;
    goods.items[index].stock = stock;

    if (img_name != NULL && img_name[0] != '\0')
    {
        strncpy(goods.items[index].img_name, img_name, sizeof(goods.items[index].img_name) - 1);
    }
    else
    {
        strncpy(goods.items[index].img_name, UI_DEFAULT_PRODUCT_IMAGE, sizeof(goods.items[index].img_name) - 1);
    }

    goods.count++;
    ui_admin_goods_update_pages();

    return ui_admin_goods_save_all();
}

int ui_admin_goods_delete(const char *name)
{
    int index = ui_admin_goods_find_by_name(name);

    if (index < 0)
    {
        return -2;
    }

    for (int i = index; i < goods.count - 1; i++)
    {
        goods.items[i] = goods.items[i + 1];
    }

    if (goods.count > 0)
    {
        goods.count--;
    }

    ui_admin_goods_update_pages();
    return ui_admin_goods_save_all();
}

int ui_admin_goods_modify(const char *name, float price, int stock, const char *img_name)
{
    int index = ui_admin_goods_find_by_name(name);

    if (index < 0)
    {
        return -2;
    }

    goods.items[index].price = price;
    goods.items[index].stock = stock;

    if (img_name != NULL && img_name[0] != '\0')
    {
        if (!ui_admin_goods_text_is_valid(img_name))
        {
            return -3;
        }
        memset(goods.items[index].img_name, 0, sizeof(goods.items[index].img_name));
        strncpy(goods.items[index].img_name, img_name, sizeof(goods.items[index].img_name) - 1);
    }

    ui_admin_goods_update_pages();
    return ui_admin_goods_save_all();
}
