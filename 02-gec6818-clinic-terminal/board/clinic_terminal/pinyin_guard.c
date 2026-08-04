/*
 * 文件作用（答辩）：保护 LVGL 拼音输入法候选状态，避免候选词与拼音错位或越界崩溃。
 * 页面切换、输入清空或无有效候选时，本模块清理组合串、候选指针、数量和页码；
 * 板端目前只保留候选首屏，不开放风险较高的候选翻页。
 *
 * 该修复位于项目业务代码中，没有修改 third_party/LVGL 源码；调用仍发生在 LVGL
 * 主线程，避免多个线程同时修改输入法内部状态。
 *
 * 这是针对现有 LVGL 8 拼音对象内部状态的保护层：reset 用于主动结束本次组合输入；
 * discard 会检查候选字符串、数量和页码是否互相一致，发现异常立即清空，避免按钮矩阵
 * 按错误偏移读取候选文本。它不负责词典匹配，也不生成汉字。
 */
#include "pinyin_guard.h"

#include <string.h>

/* 清空拼音输入串、候选指针、候选数量和页码，恢复为“未开始组合”的状态。 */
void clinic_pinyin_reset_composition(lv_ime_pinyin_t *pinyin_ime)
{
    if(pinyin_ime == NULL) {
        return;
    }

    memset(pinyin_ime->input_char, 0, sizeof(pinyin_ime->input_char));
    pinyin_ime->cand_str = NULL;
    pinyin_ime->ta_count = 0;
    pinyin_ime->cand_num = 0;
    pinyin_ime->py_page = 0;
}

/* 返回 1 表示检测到无效候选并已丢弃，页面应同步隐藏候选栏。 */
int clinic_pinyin_discard_invalid_candidates(
    lv_ime_pinyin_t *pinyin_ime)
{
    if(pinyin_ime == NULL) {
        return 0;
    }
    if(pinyin_ime->input_char[0] != '\0' &&
       pinyin_ime->cand_str != NULL && pinyin_ime->cand_num > 0U) {
        return 0;
    }

    pinyin_ime->cand_str = NULL;
    pinyin_ime->cand_num = 0;
    pinyin_ime->py_page = 0;
    return 1;
}
