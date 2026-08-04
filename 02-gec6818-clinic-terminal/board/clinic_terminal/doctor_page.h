#ifndef CLINIC_TERMINAL_DOCTOR_PAGE_H
#define CLINIC_TERMINAL_DOCTOR_PAGE_H

#include "clinic_types.h"
#include "lvgl.h"

#define CLINIC_DOCTOR_BUTTON_TEXT_CAPACITY 160U
#define CLINIC_DOCTOR_BUTTON_MAP_CAPACITY (CLINIC_MAX_DOCTORS * 2U)

typedef void (*ClinicDoctorBackCallback)(void *user_data);

typedef struct ClinicDoctorPage {
    lv_obj_t *screen;
    lv_obj_t *back_button;
    lv_obj_t *doctor_matrix;
    lv_obj_t *message_box;
    const lv_font_t *font;
    ClinicDoctorBackCallback back_callback;
    void *back_user_data;
    size_t doctor_count;
    ClinicDoctor doctors[CLINIC_MAX_DOCTORS];
    char department_name[CLINIC_DEPARTMENT_NAME_MAX_LENGTH + 1U];
    char button_texts[CLINIC_MAX_DOCTORS]
                      [CLINIC_DOCTOR_BUTTON_TEXT_CAPACITY];
    const char *button_map[CLINIC_DOCTOR_BUTTON_MAP_CAPACITY];
} ClinicDoctorPage;

int clinic_doctor_page_create(
    ClinicDoctorPage *page,
    lv_obj_t *screen,
    const lv_font_t *font,
    const char *department_name,
    const ClinicDoctor *doctors,
    size_t doctor_count,
    ClinicDoctorBackCallback back_callback,
    void *back_user_data);

void clinic_doctor_page_cleanup(ClinicDoctorPage *page);

#endif
