#ifndef CLINIC_TERMINAL_QUEUE_PAGE_H
#define CLINIC_TERMINAL_QUEUE_PAGE_H

#include "clinic_types.h"
#include "lvgl.h"

#define CLINIC_QUEUE_AUTO_REFRESH_PERIOD_MS 5000U

typedef void (*ClinicQueueBackCallback)(void *user_data);
typedef void (*ClinicQueueRefreshCallback)(void *user_data);

/*
 * 排队页由 LVGL 主线程独占。
 * auto_refresh_timer 只是每 5 秒复用一次 refresh_callback，不执行网络通信；
 * 真正的 get_current_ticket 仍由 main.c 启动原有 pthread worker。
 */
typedef struct ClinicQueuePage {
    lv_obj_t *screen;
    lv_obj_t *ticket_id_label;
    lv_obj_t *user_id_label;
    lv_obj_t *department_id_label;
    lv_obj_t *queue_number_label;
    lv_obj_t *current_called_label;
    lv_obj_t *waiting_ahead_label;
    lv_obj_t *status_label;
    lv_obj_t *service_date_label;
    lv_obj_t *created_time_label;
    lv_obj_t *called_time_label;
    lv_obj_t *empty_label;
    lv_obj_t *message_label;
    lv_obj_t *refresh_button;
    lv_obj_t *refresh_button_label;
    lv_obj_t *back_button;
    const lv_font_t *font;
    ClinicQueueRefreshCallback refresh_callback;
    void *refresh_user_data;
    ClinicQueueBackCallback back_callback;
    void *back_user_data;
    lv_timer_t *auto_refresh_timer;
    int refreshing;
} ClinicQueuePage;

int clinic_queue_page_create(
    ClinicQueuePage *page,
    lv_obj_t *screen,
    const lv_font_t *font,
    const ClinicTicket *ticket,
    const ClinicQueueSummary *summary,
    ClinicQueueRefreshCallback refresh_callback,
    void *refresh_user_data,
    ClinicQueueBackCallback back_callback,
    void *back_user_data);

int clinic_queue_page_update_ticket(
    ClinicQueuePage *page,
    const ClinicTicket *ticket,
    const ClinicQueueSummary *summary);

int clinic_queue_page_show_no_ticket(ClinicQueuePage *page);

int clinic_queue_page_show_message(
    ClinicQueuePage *page,
    const char *message);

void clinic_queue_page_set_refreshing(
    ClinicQueuePage *page,
    int refreshing);

void clinic_queue_page_cleanup(ClinicQueuePage *page);

#endif
