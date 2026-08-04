/*
 * 文件名：ui_keyboard.c
 *
 * 本文件是项目中的公共软键盘/拼音输入法模块。
 * 模块基于 LVGL 和 lv_100ask_pinyin_ime 实现，主要负责：
 * 1. 将当前输入框绑定到公共拼音输入法对象；
 * 2. 显示屏幕底部的软键盘；
 * 3. 调整候选栏和键盘本体的位置、尺寸和颜色；
 * 4. 在输入结束或界面切换时隐藏、销毁键盘对象。
 *
 * 登录界面、注册弹窗、管理员登录弹窗等页面都可以复用本模块，
 * 这样各界面不需要重复创建键盘，也便于统一维护输入体验。
 */

#include "ui_keyboard.h"
#include "ui_font.h"
#include <stdio.h>
#include "lv_100ask_pinyin_ime.h"

/*
 * pinyin_ime 是公共拼音输入法对象。
 * 多个输入框共用同一个键盘对象，避免每个页面重复创建键盘。
 * 点击不同输入框时，通过 lv_100ask_pinyin_ime_attach()
 * 将公共键盘重新绑定到当前输入框。
 */
static lv_obj_t * pinyin_ime = NULL;

/*
 * 设置输入法显示时的颜色样式。
 * 候选栏需要显示中文候选字，因此可以使用中文字体。
 * 键盘本体只显示字母、删除、回车、切换等按键，不要递归套
 * FreeType 字体，否则删除键、回车键等特殊符号可能显示异常。
 */
static void style_pinyin_ime_visible(void)
{
    if(pinyin_ime == NULL) {
        return;
    }

    /* 根容器保持深色背景，作为候选栏和键盘的底层。 */
    lv_obj_set_style_bg_color(pinyin_ime, lv_color_hex(0x202430), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pinyin_ime, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(pinyin_ime, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(pinyin_ime, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pinyin_ime, 0, LV_PART_MAIN);

    /* 候选栏使用白底黑字，便于显示和识别中文候选字。 */
    lv_obj_t * cp = lv_100ask_pinyin_ime_get_cand_panel(pinyin_ime);
    if(cp) {
        lv_obj_set_style_bg_color(cp, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cp, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(cp, lv_color_hex(0x000000), LV_PART_MAIN);

        lv_obj_set_style_border_width(cp, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(cp, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(cp, 0, LV_PART_MAIN);

        lv_obj_set_style_bg_color(cp, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(cp, LV_OPA_COVER, LV_PART_ITEMS);
        lv_obj_set_style_text_color(cp, lv_color_hex(0x000000), LV_PART_ITEMS);
    }

    /* 键盘本体背景透明，只保留按键块样式，避免出现多余底板。 */
    lv_obj_t * kb = lv_100ask_pinyin_ime_get_kb(pinyin_ime);
    if(kb) {
        lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(kb, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(kb, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(kb, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_column(kb, 0, LV_PART_MAIN);
        lv_obj_set_style_text_color(kb, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

        lv_obj_clear_flag(kb, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_style_bg_color(kb, lv_color_hex(0x666666), LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
        lv_obj_set_style_text_color(kb, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);

        lv_obj_set_style_border_color(kb, lv_color_hex(0xDDDDDD), LV_PART_ITEMS);
        lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
        lv_obj_set_style_radius(kb, 4, LV_PART_ITEMS);
    }
}

/*
 * 重新布局并显示拼音输入法。
 * 屏幕分辨率为 800x480，输入法固定放在屏幕底部。
 * 输入法总高度为 224，所以 y 坐标为 480 - 224 = 256。
 * 其中候选栏高度 24，键盘本体高度 200。
 *
 * 候选栏和键盘本体可能是 pinyin_ime 的子对象，也可能被 LVGL 放到
 * 其他层级，因此这里分别判断父对象后设置位置。
 * 显示时需要把输入法对象移动到前景，保证键盘位于上层。
 * 隐藏时则必须添加隐藏标志，避免隐藏后的键盘残留在上层拦截触摸。
 */
static void layout_pinyin_ime(void)
{
    if(pinyin_ime == NULL) {
        return;
    }

    /*
     * 输入法整体高度：
     * 候选栏 24 + 键盘 200 = 224
     * 屏幕高度 480，所以 y = 480 - 224 = 256
     */
    lv_obj_set_size(pinyin_ime, 800, 224);
    lv_obj_set_pos(pinyin_ime, 0, 256);

    lv_obj_clear_flag(pinyin_ime, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(pinyin_ime, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(pinyin_ime, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(pinyin_ime);

    /*
     * 候选栏：显示拼音输入后的中文候选字。
     * 这里需要中文字体，所以可以递归应用 FreeType 字体。
     */
    lv_obj_t *cp = lv_100ask_pinyin_ime_get_cand_panel(pinyin_ime);
    if(cp) {
        lv_obj_set_size(cp, 800, 24);

        if(lv_obj_get_parent(cp) == pinyin_ime) {
            lv_obj_set_pos(cp, 0, 0);
        } else {
            lv_obj_set_pos(cp, 0, 256);
        }

        lv_obj_clear_flag(cp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cp, LV_OBJ_FLAG_CLICKABLE);

        ui_apply_font_recursive(cp, 18);

        lv_obj_move_foreground(cp);
    }

    /*
     * 键盘本体：不要递归应用 FreeType 字体。
     * 否则删除键、回车键、切换键等特殊符号可能显示为方框。
     */
    lv_obj_t *kb = lv_100ask_pinyin_ime_get_kb(pinyin_ime);
    if(kb) {
        lv_obj_set_size(kb, 800, 200);

        if(lv_obj_get_parent(kb) == pinyin_ime) {
            lv_obj_set_pos(kb, 0, 24);
        } else {
            lv_obj_set_pos(kb, 0, 280);
        }

        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_CLICKABLE);

        /*
         * 注意：这里不要写 ui_apply_font_recursive(kb, 18);
         */

        lv_obj_move_foreground(kb);
    }

    style_pinyin_ime_visible();

    lv_obj_update_layout(lv_layer_top());

    /*
     * 布局刷新后再套一次颜色，防止 LVGL 或输入法默认样式覆盖。
     */
    style_pinyin_ime_visible();
}

/*
 * 输入框点击回调。
 * 用户名、密码、商品名等输入框被点击时，会调用本函数。
 * 如果公共键盘还没有创建，就在顶层创建 lv_100ask_pinyin_ime。
 * 然后通过 lv_100ask_pinyin_ime_attach(pinyin_ime, ta)
 * 将公共输入法绑定到当前输入框。
 * 最后调用 layout_pinyin_ime() 显示并重新布局键盘。
 */
void ta_focus_cb(lv_event_t * e)
{
    lv_obj_t * ta = lv_event_get_target(e);

    printf("[MAIN FOCUS] ta_focus_cb called, pinyin_ime=%p\n", (void *)pinyin_ime);

    if(pinyin_ime == NULL) {
        pinyin_ime = lv_100ask_pinyin_ime_create(lv_layer_top());

        if(pinyin_ime == NULL) {
            printf("[MAIN FOCUS] create pinyin_ime failed!\n");
            return;
        }

        set_ft(pinyin_ime, 20);
    }

    /* 把输入法绑定到当前被点击的文本框。 */
    lv_100ask_pinyin_ime_attach(pinyin_ime, ta);
    layout_pinyin_ime();
}

/*
 * 普通隐藏键盘函数。
 * 用于输入结束、弹窗失败提示、临时收起键盘等场景。
 * 这里会先解除输入框绑定，再隐藏 pinyin_ime、键盘本体和候选栏。
 * 注意：本函数只隐藏对象，不销毁对象，后续点击输入框还能继续复用。
 */
void hide_input_keyboard(void)
{
    if(pinyin_ime != NULL) {
        lv_100ask_pinyin_ime_attach(pinyin_ime, NULL);
        lv_obj_add_flag(pinyin_ime, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t * kb = lv_100ask_pinyin_ime_get_kb(pinyin_ime);
        if(kb) {
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_t * cp = lv_100ask_pinyin_ime_get_cand_panel(pinyin_ime);
        if(cp) {
            lv_obj_add_flag(cp, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/*
 * 彻底销毁键盘函数。
 * 用于登录成功切换主界面、清空 screen 前等场景。
 * 这里会解除输入框绑定，删除 pinyin_ime，并将 pinyin_ime 置为 NULL。
 *
 * 与 hide_input_keyboard() 的区别：
 * hide 是隐藏，保留对象以便下次复用；
 * destroy 是销毁，避免键盘对象残留在上层拦截触摸，
 * 也避免界面清理后出现悬空指针导致 Segmentation fault。
 */
void destroy_input_keyboard(void)
{
    if(pinyin_ime == NULL) {
        return;
    }

    lv_100ask_pinyin_ime_attach(pinyin_ime, NULL);
    lv_obj_del(pinyin_ime);
    pinyin_ime = NULL;
}
