#ifndef CLINIC_TERMINAL_HOME_PAGE_H
#define CLINIC_TERMINAL_HOME_PAGE_H

#include <stdint.h>

#include "lvgl.h"
#include "service_flow.h"

typedef void (*ClinicHomeDepartmentRequestCallback)(
    void *user_data,
    ClinicServiceFlow flow);
typedef void (*ClinicHomeCurrentTicketRequestCallback)(void *user_data);

typedef struct ClinicHomePage {
    lv_obj_t *screen;
    lv_obj_t *department_button;
    lv_obj_t *department_button_label;
    lv_obj_t *doctor_button;
    lv_obj_t *doctor_button_label;
    lv_obj_t *ticket_button;
    lv_obj_t *ticket_button_label;
    lv_obj_t *current_ticket_button;
    lv_obj_t *current_ticket_button_label;
    lv_obj_t *message_box;
    lv_obj_t *logout_button;
    lv_obj_t *logout_button_label;
    int logout_requested;
    const lv_font_t *font;
    ClinicHomeDepartmentRequestCallback department_request_callback;
    void *department_request_user_data;
    ClinicHomeCurrentTicketRequestCallback current_ticket_request_callback;
    void *current_ticket_request_user_data;
} ClinicHomePage;

int clinic_home_page_create(
    ClinicHomePage *page,
    lv_obj_t *screen,
    const lv_font_t *font,
    int64_t user_id,
    ClinicHomeDepartmentRequestCallback department_request_callback,
    void *department_request_user_data,
    ClinicHomeCurrentTicketRequestCallback current_ticket_request_callback,
    void *current_ticket_request_user_data);

void clinic_home_page_set_department_loading(
    ClinicHomePage *page,
    int loading);

void clinic_home_page_set_current_ticket_loading(
    ClinicHomePage *page,
    int loading);

int clinic_home_page_show_message(
    ClinicHomePage *page,
    const char *message);

void clinic_home_page_cleanup(ClinicHomePage *page);

#endif
