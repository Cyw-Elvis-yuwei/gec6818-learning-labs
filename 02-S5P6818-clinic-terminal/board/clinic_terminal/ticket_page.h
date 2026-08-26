#ifndef CLINIC_TERMINAL_TICKET_PAGE_H
#define CLINIC_TERMINAL_TICKET_PAGE_H

#include "clinic_types.h"
#include "lvgl.h"

typedef void (*ClinicTicketBackCallback)(void *user_data);

typedef struct ClinicTicketPage {
    lv_obj_t *screen;
    lv_obj_t *message_box;
    const lv_font_t *font;
    ClinicTicketBackCallback back_callback;
    void *back_user_data;
} ClinicTicketPage;

int clinic_ticket_page_create(
    ClinicTicketPage *page,
    lv_obj_t *screen,
    const lv_font_t *font,
    const ClinicTicket *ticket,
    const char *department_name,
    int existing_ticket,
    ClinicTicketBackCallback back_callback,
    void *back_user_data);

int clinic_ticket_page_show_existing_notice(ClinicTicketPage *page);

void clinic_ticket_page_cleanup(ClinicTicketPage *page);

#endif
