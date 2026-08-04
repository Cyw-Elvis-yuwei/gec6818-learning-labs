/*
 * 文件作用（答辩）：显示当前号单和排队摘要，并提供自动、手动两种刷新入口。
 * 页面展示本人号码、号单状态、当前科室正在叫的号码以及前方 WAITING 人数；本人不是
 * WAITING 时显示无需等待。刷新只通过回调通知 main.c 发起 get_current_ticket。
 *
 * LVGL 定时器每 5 秒触发一次刷新，但不创建新线程，也不直接访问网络、call_next 或
 * SQLite。刷新期间暂停定时器并禁用刷新和返回；请求结束后从新的 5 秒周期重新计时。
 * 失败时保留上一次成功数据，网络恢复后下一轮自动刷新或手动刷新都可以继续查询。
 *
 * 实现方式：create 建立固定标签；update_ticket 仅在 Ticket 和 QueueSummary 均合法时
 * 一次性更新显示；自动定时器与刷新按钮复用同一个回调，由 main.c 创建网络线程。
 * 请求失败时 main.c 不调用 update_ticket，所以旧标签自然保留；离开页面前先删除
 * 定时器，避免页面销毁后继续回调旧的 ClinicQueuePage。
 */
#include "queue_page.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

static const char *queue_status_text(ClinicTicketStatus status)
{
    switch(status) {
        case CLINIC_TICKET_WAITING:
            return "等待叫号";
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

static int ticket_is_valid(const ClinicTicket *ticket)
{
    return ticket != NULL && ticket->id > 0 && ticket->user_id > 0 &&
           ticket->department_id > 0 && ticket->queue_number > 0 &&
           ticket->created_time > 0 && ticket->called_time >= 0 &&
           strlen(ticket->service_date) == CLINIC_SERVICE_DATE_LENGTH &&
           queue_status_text(ticket->status) != NULL;
}

static int queue_summary_is_valid(const ClinicQueueSummary *summary)
{
    return summary != NULL &&
           summary->current_called_queue_number >= 0 &&
           summary->waiting_ahead_count >= 0;
}

static int detail_labels_are_valid(const ClinicQueuePage *page)
{
    return page != NULL && page->ticket_id_label != NULL &&
           page->user_id_label != NULL && page->department_id_label != NULL &&
           page->queue_number_label != NULL && page->status_label != NULL &&
           page->current_called_label != NULL &&
           page->waiting_ahead_label != NULL &&
           page->service_date_label != NULL &&
           page->created_time_label != NULL && page->called_time_label != NULL &&
           page->empty_label != NULL && page->message_label != NULL &&
           lv_obj_is_valid(page->ticket_id_label) &&
           lv_obj_is_valid(page->user_id_label) &&
           lv_obj_is_valid(page->department_id_label) &&
           lv_obj_is_valid(page->queue_number_label) &&
           lv_obj_is_valid(page->current_called_label) &&
           lv_obj_is_valid(page->waiting_ahead_label) &&
           lv_obj_is_valid(page->status_label) &&
           lv_obj_is_valid(page->service_date_label) &&
           lv_obj_is_valid(page->created_time_label) &&
           lv_obj_is_valid(page->called_time_label) &&
           lv_obj_is_valid(page->empty_label) &&
           lv_obj_is_valid(page->message_label);
}

static void set_detail_labels_hidden(ClinicQueuePage *page, int hidden)
{
    lv_obj_t *labels[] = {
        page->ticket_id_label,
        page->user_id_label,
        page->department_id_label,
        page->queue_number_label,
        page->current_called_label,
        page->waiting_ahead_label,
        page->status_label,
        page->service_date_label,
        page->created_time_label,
        page->called_time_label
    };
    size_t index;

    for(index = 0U; index < sizeof(labels) / sizeof(labels[0]); ++index) {
        if(hidden) {
            lv_obj_add_flag(labels[index], LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_clear_flag(labels[index], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static lv_obj_t *create_value_label(
    lv_obj_t *parent,
    const lv_font_t *font,
    lv_coord_t x,
    lv_coord_t y,
    lv_coord_t width)
{
    lv_obj_t *label = lv_label_create(parent);

    if(label == NULL) {
        return NULL;
    }
    lv_label_set_text(label, "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x173C35), LV_PART_MAIN);
    return label;
}

int clinic_queue_page_show_message(ClinicQueuePage *page, const char *message)
{
    if(page == NULL || message == NULL || page->message_label == NULL ||
       !lv_obj_is_valid(page->message_label)) {
        return -1;
    }
    lv_label_set_text(page->message_label, message);
    if(message[0] == '\0') {
        lv_obj_add_flag(page->message_label, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_clear_flag(page->message_label, LV_OBJ_FLAG_HIDDEN);
    }
    return 0;
}

/*
 * 把一组完整的新数据提交到页面：当前叫号 0 显示“暂无叫号”；本人不是 WAITING 时
 * 显示“无需等待”。校验失败直接返回，不做部分更新。
 */
int clinic_queue_page_update_ticket(
    ClinicQueuePage *page,
    const ClinicTicket *ticket,
    const ClinicQueueSummary *summary)
{
    char ticket_id_text[64];
    char user_text[64];
    char department_text[64];
    char queue_text[64];
    char current_called_text[64];
    char waiting_ahead_text[64];
    char status_text[64];
    char date_text[64];
    char created_text[64];
    char called_text[64];
    const char *status;

    if(!detail_labels_are_valid(page) || !ticket_is_valid(ticket) ||
       !queue_summary_is_valid(summary)) {
        return -1;
    }
    status = queue_status_text(ticket->status);
    if(format_text(ticket_id_text, sizeof(ticket_id_text),
                   "号单 ID：%" PRId64, ticket->id) != 0 ||
       format_text(user_text, sizeof(user_text),
                   "用户 ID：%" PRId64, ticket->user_id) != 0 ||
       format_text(department_text, sizeof(department_text),
                   "科室 ID：%" PRId64, ticket->department_id) != 0 ||
       format_text(queue_text, sizeof(queue_text),
                   "排队序号：%" PRId64, ticket->queue_number) != 0 ||
       (summary->current_called_queue_number == 0
            ? format_text(
                  current_called_text,
                  sizeof(current_called_text),
                  "当前叫号：暂无叫号")
            : format_text(
                  current_called_text,
                  sizeof(current_called_text),
                  "当前叫号：%" PRId64,
                  summary->current_called_queue_number)) != 0 ||
       (ticket->status == CLINIC_TICKET_WAITING
            ? format_text(
                  waiting_ahead_text,
                  sizeof(waiting_ahead_text),
                  "前方等待：%" PRId64 " 人",
                  summary->waiting_ahead_count)
            : format_text(
                  waiting_ahead_text,
                  sizeof(waiting_ahead_text),
                  "前方等待：无需等待")) != 0 ||
       format_text(status_text, sizeof(status_text),
                   "当前状态：%s", status) != 0 ||
       format_text(date_text, sizeof(date_text),
                   "服务日期：%s", ticket->service_date) != 0 ||
       format_text(created_text, sizeof(created_text),
                   "创建时间：%" PRId64, ticket->created_time) != 0) {
        return -1;
    }
    if(ticket->called_time == 0) {
        if(format_text(called_text, sizeof(called_text),
                       "叫号时间：尚未叫号") != 0) {
            return -1;
        }
    }
    else if(format_text(called_text, sizeof(called_text),
                        "叫号时间：%" PRId64, ticket->called_time) != 0) {
        return -1;
    }

    lv_label_set_text(page->ticket_id_label, ticket_id_text);
    lv_label_set_text(page->user_id_label, user_text);
    lv_label_set_text(page->department_id_label, department_text);
    lv_label_set_text(page->queue_number_label, queue_text);
    lv_label_set_text(page->current_called_label, current_called_text);
    lv_label_set_text(page->waiting_ahead_label, waiting_ahead_text);
    lv_label_set_text(page->status_label, status_text);
    lv_label_set_text(page->service_date_label, date_text);
    lv_label_set_text(page->created_time_label, created_text);
    lv_label_set_text(page->called_time_label, called_text);
    set_detail_labels_hidden(page, 0);
    lv_obj_add_flag(page->empty_label, LV_OBJ_FLAG_HIDDEN);
    return clinic_queue_page_show_message(page, "");
}

int clinic_queue_page_show_no_ticket(ClinicQueuePage *page)
{
    if(!detail_labels_are_valid(page)) {
        return -1;
    }
    set_detail_labels_hidden(page, 1);
    lv_label_set_text(page->empty_label, "当前没有号单");
    lv_obj_clear_flag(page->empty_label, LV_OBJ_FLAG_HIDDEN);
    return clinic_queue_page_show_message(page, "");
}

static void pause_auto_refresh_timer(ClinicQueuePage *page)
{
    if(page != NULL && page->auto_refresh_timer != NULL) {
        lv_timer_pause(page->auto_refresh_timer);
    }
}

static void restart_auto_refresh_timer(ClinicQueuePage *page)
{
    if(page != NULL && page->auto_refresh_timer != NULL) {
        lv_timer_reset(page->auto_refresh_timer);
        lv_timer_resume(page->auto_refresh_timer);
    }
}

static void delete_auto_refresh_timer(ClinicQueuePage *page)
{
    if(page != NULL && page->auto_refresh_timer != NULL) {
        lv_timer_del(page->auto_refresh_timer);
        page->auto_refresh_timer = NULL;
    }
}

/* 刷新期间同时禁用刷新和返回，结束后恢复，避免重复线程和中途删页。 */
void clinic_queue_page_set_refreshing(ClinicQueuePage *page, int refreshing)
{
    if(page == NULL || page->refresh_button == NULL ||
       page->refresh_button_label == NULL || page->back_button == NULL ||
       !lv_obj_is_valid(page->refresh_button) ||
       !lv_obj_is_valid(page->refresh_button_label) ||
       !lv_obj_is_valid(page->back_button)) {
        return;
    }
    if(refreshing) {
        page->refreshing = 1;
        /*
         * 网络请求可能超过 5 秒。请求期间暂停定时器，既避免重叠请求，也避免断网时
         * 定时器连续到期，使返回按钮刚恢复就再次被禁用。
         */
        pause_auto_refresh_timer(page);
        lv_label_set_text(page->refresh_button_label, "正在刷新...");
        lv_obj_add_state(page->refresh_button, LV_STATE_DISABLED);
        lv_obj_add_state(page->back_button, LV_STATE_DISABLED);
    }
    else {
        page->refreshing = 0;
        lv_label_set_text(page->refresh_button_label, "刷新状态");
        lv_obj_clear_state(page->refresh_button, LV_STATE_DISABLED);
        lv_obj_clear_state(page->back_button, LV_STATE_DISABLED);
        /* 每次请求完成后重新等待完整的 5 秒，给用户留下稳定的操作窗口。 */
        restart_auto_refresh_timer(page);
    }
    lv_obj_center(page->refresh_button_label);
}

/*
 * 定时器回调运行在调用 lv_timer_handler() 的 LVGL 主线程。
 * 它只复用按钮的上层回调；TCP/JSON 收发仍在原有网络 worker 中进行。
 */
static void auto_refresh_timer_cb(lv_timer_t *timer)
{
    ClinicQueuePage *page = timer == NULL ? NULL : timer->user_data;

    if(page == NULL || page->screen == NULL ||
       !lv_obj_is_valid(page->screen) || lv_scr_act() != page->screen ||
       page->refreshing || page->refresh_callback == NULL) {
        return;
    }
    page->refresh_callback(page->refresh_user_data);
}

/* 只通知上层“用户要求刷新”，不在 LVGL 回调里执行阻塞网络通信。 */
static void refresh_button_event_cb(lv_event_t *event)
{
    ClinicQueuePage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_CLICKED && page != NULL &&
       !page->refreshing && page->refresh_callback != NULL) {
        page->refresh_callback(page->refresh_user_data);
    }
}

static void back_button_event_cb(lv_event_t *event)
{
    ClinicQueuePage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_CLICKED && page != NULL &&
       !page->refreshing && page->back_callback != NULL) {
        /*
         * 先停掉页面私有定时器，再通知 main.c 切页。即使切页发生在本轮主循环稍后，
         * 也不会在“已经申请返回”与“真正删除页面”之间启动新的 worker。
         */
        delete_auto_refresh_timer(page);
        page->back_callback(page->back_user_data);
    }
}

void clinic_queue_page_cleanup(ClinicQueuePage *page)
{
    if(page == NULL) {
        return;
    }
    delete_auto_refresh_timer(page);
    memset(page, 0, sizeof(*page));
}

/* 创建排队页后先显示首次查询结果，再启动 5 秒自动刷新定时器。 */
int clinic_queue_page_create(
    ClinicQueuePage *page,
    lv_obj_t *screen,
    const lv_font_t *font,
    const ClinicTicket *ticket,
    const ClinicQueueSummary *summary,
    ClinicQueueRefreshCallback refresh_callback,
    void *refresh_user_data,
    ClinicQueueBackCallback back_callback,
    void *back_user_data)
{
    lv_obj_t *title;
    lv_obj_t *card;
    lv_obj_t *back_label;

    if(page == NULL || screen == NULL || font == NULL ||
       !ticket_is_valid(ticket) || !queue_summary_is_valid(summary) ||
       refresh_callback == NULL ||
       back_callback == NULL) {
        return -1;
    }
    memset(page, 0, sizeof(*page));
    page->screen = screen;
    page->font = font;
    page->refresh_callback = refresh_callback;
    page->refresh_user_data = refresh_user_data;
    page->back_callback = back_callback;
    page->back_user_data = back_user_data;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0xEAF3EF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(screen);
    card = lv_obj_create(screen);
    page->message_label = lv_label_create(screen);
    page->refresh_button = lv_btn_create(screen);
    page->back_button = lv_btn_create(screen);
    page->refresh_button_label = page->refresh_button == NULL
        ? NULL
        : lv_label_create(page->refresh_button);
    back_label = page->back_button == NULL
        ? NULL
        : lv_label_create(page->back_button);
    if(title == NULL || card == NULL || page->message_label == NULL ||
       page->refresh_button == NULL || page->back_button == NULL ||
       page->refresh_button_label == NULL || back_label == NULL) {
        return -1;
    }

    lv_label_set_text(title, "排队状态（每 5 秒自动刷新）");
    lv_obj_set_style_text_font(title, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0D5A4C), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_set_pos(card, 40, 68);
    lv_obj_set_size(card, 720, 280);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x9FC5B8), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 18, LV_PART_MAIN);

    page->ticket_id_label = create_value_label(card, font, 16, 10, 320);
    page->user_id_label = create_value_label(card, font, 360, 10, 310);
    page->department_id_label = create_value_label(card, font, 16, 70, 320);
    page->queue_number_label = create_value_label(card, font, 360, 70, 310);
    page->current_called_label = create_value_label(card, font, 16, 130, 320);
    page->waiting_ahead_label = create_value_label(card, font, 360, 130, 310);
    page->status_label = create_value_label(card, font, 16, 190, 320);
    page->service_date_label = create_value_label(card, font, 360, 190, 310);
    page->created_time_label = create_value_label(card, font, 16, 230, 320);
    page->called_time_label = create_value_label(card, font, 360, 230, 310);
    page->empty_label = lv_label_create(card);
    if(!detail_labels_are_valid(page)) {
        return -1;
    }
    lv_label_set_text(page->empty_label, "当前没有号单");
    lv_obj_set_style_text_font(page->empty_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(
        page->empty_label,
        lv_color_hex(0x8A4B20),
        LV_PART_MAIN);
    lv_obj_center(page->empty_label);
    lv_obj_add_flag(page->empty_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_pos(page->message_label, 40, 355);
    lv_obj_set_width(page->message_label, 720);
    lv_obj_set_style_text_font(page->message_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(
        page->message_label,
        lv_color_hex(0xA33B2B),
        LV_PART_MAIN);
    lv_obj_set_style_text_align(
        page->message_label,
        LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN);
    lv_obj_add_flag(page->message_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_pos(page->refresh_button, 205, 400);
    lv_obj_set_size(page->refresh_button, 180, 56);
    lv_obj_set_style_bg_color(
        page->refresh_button,
        lv_color_hex(0x16705A),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page->refresh_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(page->refresh_button, 10, LV_PART_MAIN);
    lv_label_set_text(page->refresh_button_label, "刷新状态");
    lv_obj_set_style_text_font(page->refresh_button_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(
        page->refresh_button_label,
        lv_color_hex(0xFFFFFF),
        LV_PART_MAIN);
    lv_obj_center(page->refresh_button_label);

    lv_obj_set_pos(page->back_button, 415, 400);
    lv_obj_set_size(page->back_button, 180, 56);
    lv_obj_set_style_bg_color(
        page->back_button,
        lv_color_hex(0x0B5D8C),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page->back_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(page->back_button, 10, LV_PART_MAIN);
    lv_label_set_text(back_label, "返回主页");
    lv_obj_set_style_text_font(back_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(back_label);

    lv_obj_add_event_cb(
        page->refresh_button,
        refresh_button_event_cb,
        LV_EVENT_CLICKED,
        page);
    lv_obj_add_event_cb(
        page->back_button,
        back_button_event_cb,
        LV_EVENT_CLICKED,
        page);
    if(clinic_queue_page_update_ticket(page, ticket, summary) != 0) {
        return -1;
    }

    /*
     * lv_timer_create() 只登记 LVGL 主线程回调。第一次回调发生在完整周期之后，
     * 不会在刚进入页面时重复查询首页已经取得的同一份号单数据。
     */
    page->auto_refresh_timer = lv_timer_create(
        auto_refresh_timer_cb,
        CLINIC_QUEUE_AUTO_REFRESH_PERIOD_MS,
        page);
    if(page->auto_refresh_timer == NULL) {
        return -1;
    }
    return 0;
}
