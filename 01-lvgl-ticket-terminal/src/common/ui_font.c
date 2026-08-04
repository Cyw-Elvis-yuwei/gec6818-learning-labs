/*
 * 文件名：ui_font.c
 * 版本说明：答辩版中文注释。
 * 文件作用：FreeType 字体工具实现文件。负责加载 simkai.ttf 并给 LVGL 控件设置中文字体。
 *
 * 零基础理解：
 * 1. .h 文件主要放“声明”，让其他 .c 文件知道有哪些函数可以调用。
 * 2. .c 文件主要放“实现”，也就是具体怎么创建界面、处理点击、保存数据。
 * 3. 本项目界面基于 LVGL，中文显示依靠 FreeType 字库。
 */

#include "ui_font.h"

/*
 * 文件作用：统一管理中文字体。
 * 本项目采用“直接写中文 + FreeType 字库”的方式显示中文。
 *
 * 注意：开发板上必须存在 /font/simkai.ttf。
 * 如果字体和可执行文件放在同一目录，可把下面路径改成 "./simkai.ttf"。
 */
#define LV_FONT_KAI "/font/simkai.ttf"

/*
 * 给 LVGL 控件设置 FreeType 字体。
 *
 * 参数：
 * obj  ：要设置字体的控件，例如 label、textarea、button 内部 label。
 * size ：字号，例如 16、18、20、24。
 *
 * 设计说明：
 * 1. 这里做了字体缓存，相同字号只初始化一次，避免重复加载字体导致内存浪费。
 * 2. _c[6] 表示最多缓存 6 种字号，项目里已经够用。
 * 3. 初始化成功后，把字体样式添加到 obj 上。
 */
void set_ft(lv_obj_t *obj, int size)
{
    if (obj == NULL)
    {
        return;
    }

    static struct {
        int sz;          /* 当前缓存的字号 */
        lv_ft_info_t i;  /* FreeType 字体信息 */
        lv_font_t *f;    /* LVGL 字体指针 */
        lv_style_t s;    /* 绑定字体的样式 */
        int ok;          /* 是否初始化成功 */
    } _c[6];

    int s = -1;  /* 找到的缓存下标 */
    int e = -1;  /* 空闲缓存下标 */

    /* 先查找是否已经缓存过相同字号。 */
    for (int j = 0; j < 6; j++)
    {
        if (_c[j].ok && _c[j].sz == size)
        {
            s = j;
            break;
        }

        if (e < 0 && !_c[j].ok)
        {
            e = j;
        }
    }

    /* 如果没有找到旧缓存，就使用一个空缓存位。 */
    if (s < 0)
    {
        s = e;
    }

    /* 没有空位，直接返回，避免数组越界。 */
    if (s < 0)
    {
        return;
    }

    /* 当前字号第一次使用时，初始化 FreeType 字体。 */
    if (!_c[s].ok)
    {
        _c[s].sz = size;
        _c[s].i.name = LV_FONT_KAI;
        _c[s].i.weight = size;
        _c[s].i.style = FT_FONT_STYLE_BOLD;
        _c[s].i.mem = NULL;

        if (!lv_ft_font_init(&_c[s].i))
        {
            return;
        }

        _c[s].f = _c[s].i.font;
        lv_style_init(&_c[s].s);
        lv_style_set_text_font(&_c[s].s, _c[s].f);
        _c[s].ok = 1;
    }

    /* 把字体样式添加到控件上。 */
    lv_obj_add_style(obj, &_c[s].s, 0);
}

/*
 * 递归给页面、弹窗、键盘等控件树设置字体。
 * 管理员页面和拼音输入法内部控件较多，使用这个函数可以避免漏设字体。
 */
void ui_apply_font_recursive(lv_obj_t *root, int size)
{
    if (root == NULL)
    {
        return;
    }

    set_ft(root, size);

    uint32_t child_count = lv_obj_get_child_cnt(root);
    for (uint32_t i = 0; i < child_count; i++)
    {
        lv_obj_t *child = lv_obj_get_child(root, i);
        ui_apply_font_recursive(child, size);
    }
}
