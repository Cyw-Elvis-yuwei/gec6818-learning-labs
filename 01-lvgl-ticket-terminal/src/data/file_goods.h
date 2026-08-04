#ifndef FILE_GOODS_H
#define FILE_GOODS_H

#define GOODS_FILE_PATH  "/goods.txt"
#define GOODS_LINE_MAX   128
#define GOODS_NAME_MAX   40
#define GOODS_MAX_COUNT  20      // 8个景区足够

/* 单张景区门票数据 */
typedef struct {
    char  scenic[GOODS_NAME_MAX];   // 景区名称
    float price;                    // 票价
    int   stock;                    // 日库存
    char  img_name[40];             // 图片资源变量名(如 img_product_01)
} goods_item_t;

typedef struct {
    goods_item_t items[GOODS_MAX_COUNT];
    int count;
} goods_list_t;

int file_goods_read_all(goods_list_t *out_list);
int file_goods_find(goods_list_t *list, const char *scenic);
int file_goods_write_all(goods_list_t *list);
int file_goods_update_stock(const char *scenic, int new_stock);

#endif