#include "file_goods.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ½âÎö "¾°Çø|Æ±¼Û|¿â´æ|Í¼Æ¬Ãû" */
static int parse_line(const char *line, goods_item_t *item)
{
    char buf[GOODS_LINE_MAX];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *scenic = strtok(buf, "|");
    char *price  = strtok(NULL, "|");
    char *stock  = strtok(NULL, "|");
    char *img    = strtok(NULL, "|");

    if (!scenic || !price || !stock || !img) return -1;

    strncpy(item->scenic, scenic, sizeof(item->scenic) - 1);
    item->scenic[sizeof(item->scenic) - 1] = '\0';
    strncpy(item->img_name, img, sizeof(item->img_name) - 1);
    item->img_name[sizeof(item->img_name) - 1] = '\0';
    item->price = atof(price);
    item->stock = atoi(stock);
    return 0;
}

int file_goods_read_all(goods_list_t *out_list)
{
    if (!out_list) return -1;

    FILE *fp = fopen(GOODS_FILE_PATH, "r");
    if (!fp) return -1;

    out_list->count = 0;
    char line[GOODS_LINE_MAX];

    while (fgets(line, sizeof(line), fp)
           && out_list->count < GOODS_MAX_COUNT) {

        int len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (line[0] == '\0') continue;

        if (parse_line(line, &out_list->items[out_list->count]) == 0)
            out_list->count++;
    }

    fclose(fp);
    return 0;
}

int file_goods_find(goods_list_t *list, const char *scenic)
{
    if (!list || !scenic) return -1;
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].scenic, scenic) == 0)
            return i;
    }
    return -1;
}

int file_goods_write_all(goods_list_t *list)
{
    if (!list) return -1;

    FILE *fp = fopen(GOODS_FILE_PATH, "w");
    if (!fp) return -1;

    for (int i = 0; i < list->count; i++) {
        fprintf(fp, "%s|%.2f|%d|%s\n",
                list->items[i].scenic,
                list->items[i].price,
                list->items[i].stock,
                list->items[i].img_name);
    }
    fflush(fp);
    fclose(fp);
    return 0;
}

int file_goods_update_stock(const char *scenic, int new_stock)
{
    if (!scenic || new_stock < 0) return -1;

    goods_list_t list;
    if (file_goods_read_all(&list) != 0) return -1;

    int idx = file_goods_find(&list, scenic);
    if (idx < 0) return -2;

    list.items[idx].stock = new_stock;
    return file_goods_write_all(&list);
}