#ifndef CLINIC_TERMINAL_DEPARTMENT_PAGE_H
#define CLINIC_TERMINAL_DEPARTMENT_PAGE_H

#include "clinic_types.h"
#include "lvgl.h"
#include "service_flow.h"

#define CLINIC_DEPARTMENT_BUTTON_TEXT_CAPACITY 112U
#define CLINIC_DEPARTMENT_BUTTON_MAP_CAPACITY (CLINIC_MAX_DEPARTMENTS * 2U)

typedef void (*ClinicDepartmentBackCallback)(void *user_data);
typedef void (*ClinicDepartmentSelectCallback)(
    int64_t department_id,
    const char *department_name,
    void *user_data);

typedef struct ClinicDepartmentPage {
    lv_obj_t *screen;
    lv_obj_t *back_button;
    lv_obj_t *department_matrix;
    lv_obj_t *status_label;
    lv_obj_t *message_box;
    const lv_font_t *font;
    ClinicDepartmentSelectCallback select_callback;
    void *select_user_data;
    ClinicDepartmentBackCallback back_callback;
    void *back_user_data;
    size_t department_count;
    int request_loading;
    int detail_action_pending;
    size_t selected_department_index;
    ClinicServiceFlow flow;
    ClinicDepartment departments[CLINIC_MAX_DEPARTMENTS];
    char button_texts[CLINIC_MAX_DEPARTMENTS]
                     [CLINIC_DEPARTMENT_BUTTON_TEXT_CAPACITY];
    const char *button_map[CLINIC_DEPARTMENT_BUTTON_MAP_CAPACITY];
} ClinicDepartmentPage;

int clinic_department_page_create(
    ClinicDepartmentPage *page,
    lv_obj_t *screen,
    const lv_font_t *font,
    ClinicServiceFlow flow,
    const ClinicDepartment *departments,
    size_t department_count,
    ClinicDepartmentSelectCallback select_callback,
    void *select_user_data,
    ClinicDepartmentBackCallback back_callback,
    void *back_user_data);

void clinic_department_page_set_request_loading(
    ClinicDepartmentPage *page,
    int loading);

int clinic_department_page_show_message(
    ClinicDepartmentPage *page,
    const char *message);

void clinic_department_page_cleanup(ClinicDepartmentPage *page);

#endif
