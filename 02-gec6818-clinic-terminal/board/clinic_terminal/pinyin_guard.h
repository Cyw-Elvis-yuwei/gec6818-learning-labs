#ifndef CLINIC_TERMINAL_PINYIN_GUARD_H
#define CLINIC_TERMINAL_PINYIN_GUARD_H

#include "src/extra/others/ime/lv_ime_pinyin.h"

void clinic_pinyin_reset_composition(lv_ime_pinyin_t *pinyin_ime);

int clinic_pinyin_discard_invalid_candidates(
    lv_ime_pinyin_t *pinyin_ime);

#endif
