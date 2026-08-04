/*
 * 文件作用（答辩）：展示服务器返回的门诊号单详情。
 * 页面显示号单 ID、用户 ID、科室 ID、排队号码、状态、服务日期和时间等真实字段；
 * 如果服务器返回已有活动号单，本页先显示原号单，再弹出“本次未重复取号”提示。
 *
 * 本文件不创建号单、不计算队列、不访问网络或 SQLite。消息框由页面结构持有，关闭、
 * 返回和清理时统一置空，避免重复删除或访问已经销毁的 LVGL 对象。
 *
 * 实现方式：create 根据 ClinicTicket 逐项格式化标签；ticket_status_text 把服务端枚举
 * 转成中文；show_existing_notice 只在服务器明确返回原有效号单时显示提示。
 */
#include "ticket_page.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *existing_ticket_notice_buttons[] = {"确定", ""};

static void close_message_box(ClinicTicketPage *page)
{
    lv_obj_t *message_box;

    if(page == NULL) {
        return;
    }
    message_box = page->message_box;
    page->message_box = NULL;
    if(message_box != NULL && lv_obj_is_valid(message_box)) {
        lv_msgbox_close(message_box);
    }
}

static void message_box_event_cb(lv_event_t *event)
{
    ClinicTicketPage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        close_message_box(page);
    }
}

int clinic_ticket_page_show_existing_notice(ClinicTicketPage *page)
{
    lv_obj_t *backdrop;
    lv_obj_t *title;
    lv_obj_t *text;
    lv_obj_t *content;
    lv_obj_t *buttons;

    if(page == NULL || page->screen == NULL || page->font == NULL ||
       !lv_obj_is_valid(page->screen) || page->screen != lv_scr_act()) {
        return -1;
    }
    if(page->message_box != NULL && lv_obj_is_valid(page->message_box)) {
        return 0;
    }
    page->message_box = NULL;
    page->message_box = lv_msgbox_create(
        NULL,
        "提示",
        "您已有有效号单，本次未重复取号，正在显示原号单。",
        existing_ticket_notice_buttons,
        false);
    if(page->message_box == NULL) {
        return -1;
    }

    backdrop = lv_obj_get_parent(page->message_box);
    title = lv_msgbox_get_title(page->message_box);
    text = lv_msgbox_get_text(page->message_box);
    content = lv_msgbox_get_content(page->message_box);
    buttons = lv_msgbox_get_btns(page->message_box);
    if(backdrop == NULL || title == NULL || text == NULL ||
       content == NULL || buttons == NULL) {
        close_message_box(page);
        return -1;
    }

    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_50, LV_PART_MAIN);

    lv_obj_set_width(page->message_box, 560);
    lv_obj_set_style_bg_color(page->message_box, lv_color_hex(0xFFFFFF),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page->message_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(page->message_box, lv_color_hex(0x1C668C),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(page->message_box, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(page->message_box, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page->message_box, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_row(page->message_box, 14, LV_PART_MAIN);

    lv_obj_set_style_text_font(title, page->font, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x123F5A), LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(text, page->font, LV_PART_MAIN);
    lv_obj_set_style_text_color(text, lv_color_hex(0x173C35), LV_PART_MAIN);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_set_size(buttons, 220, 50);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(buttons, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(buttons, lv_color_hex(0x0B5D8C),
                              LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_COVER,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(buttons, lv_color_hex(0xFFFFFF),
                                LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(buttons, page->font,
                               LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(buttons, 8,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(
        page->message_box,
        message_box_event_cb,
        LV_EVENT_VALUE_CHANGED,
        page);
    lv_obj_center(page->message_box);
    return 0;
}

static int format_text(
    char *output,
    size_t output_capacity,
    const char *format,
    ...)
{
    va_list arguments;
    int written;

    if(output == NULL || output_capacity == 0U || format == NULL) {
        return -1;
    }
    va_start(arguments, format);
    written = vsnprintf(output, output_capacity, format, arguments);
    va_end(arguments);
    return written < 0 || (size_t)written >= output_capacity ? -1 : 0;
}

/* 这里只做枚举到中文的显示映射，不改变服务器保存的真实状态。 */
static const char *ticket_status_text(ClinicTicketStatus status)
{
    switch(status) {
        case CLINIC_TICKET_WAITING:
            return "等待中";
        case CLINIC_TICKET_CALLED:
            return "已叫号";
        case CLINIC_TICKET_COMPLETED:
            return "已完成";
        case CLINIC_TICKET_CANCELLED:
            return "已取消";
        default:
            return NULL;
    }
}

static lv_obj_t *create_value_label(
    lv_obj_t *parent,
    const lv_font_t *font,
    const char *text,
    lv_coord_t x,
    lv_coord_t y,
    lv_coord_t width)
{
    lv_obj_t *label = lv_label_create(parent);

    if(label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x173C35), LV_PART_MAIN);
    return label;
}

static void back_button_event_cb(lv_event_t *event)
{
    ClinicTicketPage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_CLICKED && page != NULL &&
       page->back_callback != NULL) {
        page->back_callback(page->back_user_data);
    }
}

void clinic_ticket_page_cleanup(ClinicTicketPage *page)
{
    if(page == NULL) {
        return;
    }
    close_message_box(page);
    page->screen = NULL;
    page->font = NULL;
    page->back_callback = NULL;
    page->back_user_data = NULL;
}

/* 号单页输入必须是服务器已验证的 Ticket；创建失败时由上层清理整个新 screen。 */
int clinic_ticket_page_create(
    ClinicTicketPage *page,
    lv_obj_t *screen,
    const lv_font_t *font,
    const ClinicTicket *ticket,
    const char *department_name,
    int existing_ticket,
    ClinicTicketBackCallback back_callback,
    void *back_user_data)
{
    char ticket_id_text[64];
    char queue_text[64];
    char department_text[CLINIC_DEPARTMENT_NAME_MAX_LENGTH + 64U];
    char status_text[64];
    char date_text[64];
    char user_text[64];
    const char *status;
    lv_obj_t *title;
    lv_obj_t *success_label;
    lv_obj_t *card;
    lv_obj_t *back_button;
    lv_obj_t *back_label;

    if(page == NULL || screen == NULL || font == NULL || ticket == NULL ||
       department_name == NULL || department_name[0] == '\0' ||
       strlen(department_name) > CLINIC_DEPARTMENT_NAME_MAX_LENGTH ||
       ticket->id <= 0 || ticket->user_id <= 0 ||
       ticket->department_id <= 0 || ticket->queue_number <= 0 ||
       strlen(ticket->service_date) != CLINIC_SERVICE_DATE_LENGTH ||
       back_callback == NULL) {
        return -1;
    }
    status = ticket_status_text(ticket->status);
    if(status == NULL ||
       format_text(
           ticket_id_text,
           sizeof(ticket_id_text),
           "号单 ID：%" PRId64,
           ticket->id) != 0 ||
       format_text(
           queue_text,
           sizeof(queue_text),
           "排队序号：%" PRId64,
           ticket->queue_number) != 0 ||
       format_text(
           department_text,
           sizeof(department_text),
           "科室：%s（ID：%" PRId64 "）",
           department_name,
           ticket->department_id) != 0 ||
       format_text(
           status_text,
           sizeof(status_text),
           "当前状态：%s",
           status) != 0 ||
       format_text(
           date_text,
           sizeof(date_text),
           "服务日期：%s",
           ticket->service_date) != 0 ||
       format_text(
           user_text,
           sizeof(user_text),
           "用户 ID：%" PRId64,
           ticket->user_id) != 0) {
        return -1;
    }

    page->screen = screen;
    page->font = font;
    page->back_callback = back_callback;
    page->back_user_data = back_user_data;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0xEAF3EF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(screen);
    success_label = lv_label_create(screen);
    card = lv_obj_create(screen);
    back_button = lv_btn_create(screen);
    back_label = back_button == NULL ? NULL : lv_label_create(back_button);
    if(title == NULL || success_label == NULL || card == NULL ||
       back_button == NULL || back_label == NULL) {
        return -1;
    }

    lv_label_set_text(title, "医路通");
    lv_obj_set_style_text_font(title, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0D5A4C), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    lv_label_set_text(success_label, existing_ticket ? "号单信息" : "取号成功");
    lv_obj_set_style_text_font(success_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(
        success_label,
        lv_color_hex(0x1A7A4A),
        LV_PART_MAIN);
    lv_obj_align(success_label, LV_ALIGN_TOP_MID, 0, 58);

    lv_obj_set_pos(card, 40, 100);
    lv_obj_set_size(card, 720, 275);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x9FC5B8), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 18, LV_PART_MAIN);

    if(create_value_label(card, font, ticket_id_text, 16, 10, 320) == NULL ||
       create_value_label(card, font, user_text, 360, 10, 310) == NULL ||
       create_value_label(card, font, department_text, 16, 62, 650) == NULL ||
       create_value_label(card, font, queue_text, 16, 114, 300) == NULL ||
       create_value_label(card, font, status_text, 360, 114, 310) == NULL ||
       create_value_label(card, font, date_text, 16, 166, 320) == NULL) {
        return -1;
    }

    lv_obj_set_pos(back_button, 315, 400);
    lv_obj_set_size(back_button, 170, 56);
    lv_obj_set_style_bg_color(back_button, lv_color_hex(0x0B5D8C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(back_button, 10, LV_PART_MAIN);
    lv_label_set_text(back_label, "返回主页");
    lv_obj_set_style_text_font(back_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(
        back_button,
        back_button_event_cb,
        LV_EVENT_CLICKED,
        page);
    return 0;
}
