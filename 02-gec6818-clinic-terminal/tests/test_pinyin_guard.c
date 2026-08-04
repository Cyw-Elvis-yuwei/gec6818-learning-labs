#include "pinyin_guard.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if(!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n",                    \
                    __FILE__, __LINE__, #condition);                           \
            failures++;                                                       \
        }                                                                      \
    } while(0)

static void test_reset_preserves_dictionary_indexes(void)
{
    lv_ime_pinyin_t pinyin_ime;
    uint16_t expected_num[26];
    uint16_t expected_pos[26];
    size_t index;

    memset(&pinyin_ime, 0, sizeof(pinyin_ime));
    memcpy(pinyin_ime.input_char, "zhang", sizeof("zhang"));
    pinyin_ime.cand_str = (char *)"张";
    pinyin_ime.ta_count = 5U;
    pinyin_ime.cand_num = 1U;
    pinyin_ime.py_page = 2U;
    for(index = 0U; index < 26U; ++index) {
        pinyin_ime.py_num[index] = (uint16_t)(index + 1U);
        pinyin_ime.py_pos[index] = (uint16_t)(index * 7U);
    }
    memcpy(expected_num, pinyin_ime.py_num, sizeof(expected_num));
    memcpy(expected_pos, pinyin_ime.py_pos, sizeof(expected_pos));

    clinic_pinyin_reset_composition(&pinyin_ime);

    CHECK(pinyin_ime.input_char[0] == '\0');
    CHECK(pinyin_ime.cand_str == NULL);
    CHECK(pinyin_ime.ta_count == 0U);
    CHECK(pinyin_ime.cand_num == 0U);
    CHECK(pinyin_ime.py_page == 0U);
    CHECK(memcmp(pinyin_ime.py_num, expected_num, sizeof(expected_num)) == 0);
    CHECK(memcmp(pinyin_ime.py_pos, expected_pos, sizeof(expected_pos)) == 0);
}

static void test_invalid_candidates_are_discarded(void)
{
    lv_ime_pinyin_t pinyin_ime;

    memset(&pinyin_ime, 0, sizeof(pinyin_ime));
    memcpy(pinyin_ime.input_char, "iv", sizeof("iv"));
    pinyin_ime.cand_str = NULL;
    pinyin_ime.cand_num = 8U;
    pinyin_ime.py_page = 1U;

    CHECK(clinic_pinyin_discard_invalid_candidates(&pinyin_ime) == 1);
    CHECK(pinyin_ime.cand_str == NULL);
    CHECK(pinyin_ime.cand_num == 0U);
    CHECK(pinyin_ime.py_page == 0U);
}

static void test_valid_candidates_are_preserved(void)
{
    lv_ime_pinyin_t pinyin_ime;
    char candidates[] = "张章长";

    memset(&pinyin_ime, 0, sizeof(pinyin_ime));
    memcpy(pinyin_ime.input_char, "zhang", sizeof("zhang"));
    pinyin_ime.cand_str = candidates;
    pinyin_ime.cand_num = 3U;

    CHECK(clinic_pinyin_discard_invalid_candidates(&pinyin_ime) == 0);
    CHECK(pinyin_ime.cand_str == candidates);
    CHECK(pinyin_ime.cand_num == 3U);
}

int main(void)
{
    test_reset_preserves_dictionary_indexes();
    test_invalid_candidates_are_discarded();
    test_valid_candidates_are_preserved();

    if(failures != 0) {
        fprintf(stderr, "%d pinyin guard test(s) failed\n", failures);
        return 1;
    }
    printf("pinyin guard tests passed\n");
    return 0;
}
