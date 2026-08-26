#ifndef CLINIC_TERMINAL_REGISTER_PAGE_H
#define CLINIC_TERMINAL_REGISTER_PAGE_H

#include "lvgl.h"

typedef void (*ClinicRegisterSubmitCallback)(
    const char *username,
    const char *password,
    void *user_data);

typedef void (*ClinicRegisterBackCallback)(void *user_data);

typedef struct ClinicRegisterPage {
    lv_obj_t *screen;
    lv_obj_t *username;
    lv_obj_t *password;
    lv_obj_t *password_confirmation;
    lv_obj_t *username_keyboard;
    lv_obj_t *password_keyboard;
    lv_obj_t *pinyin_ime;
    lv_obj_t *candidate_panel;
    lv_obj_t *active_input;
    lv_obj_t *status_label;
    lv_obj_t *register_button;
    lv_obj_t *register_button_label;
    lv_obj_t *back_button;
    const lv_font_t *font;
    ClinicRegisterSubmitCallback submit_callback;
    ClinicRegisterBackCallback back_callback;
    void *callback_user_data;
    int busy;
} ClinicRegisterPage;

int clinic_register_page_create(
    ClinicRegisterPage *page,
    lv_obj_t *screen,
    const lv_font_t *font,
    ClinicRegisterSubmitCallback submit_callback,
    ClinicRegisterBackCallback back_callback,
    void *callback_user_data);

void clinic_register_page_set_busy(ClinicRegisterPage *page, int busy);
void clinic_register_page_show_status(
    ClinicRegisterPage *page,
    const char *message);
void clinic_register_page_cleanup(ClinicRegisterPage *page);
int clinic_register_page_is_active(const ClinicRegisterPage *page);

#endif
