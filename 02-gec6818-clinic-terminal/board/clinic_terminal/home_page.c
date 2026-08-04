/*
 * 文件作用（答辩）：登录成功后的 LVGL 主页视图。
 * 页面显示真实 user_id，并提供科室查询、医生查询、门诊取号、排队查询和退出登录入口。
 * 三种医疗入口通过 ClinicServiceFlow 区分，避免“查询”和“取号”语义混在一起。
 *
 * 本文件只创建控件、处理点击和调用上层回调，不直接连接服务器或访问 SQLite。
 * 请求期间由主控制器设置加载状态并禁用相关入口，页面销毁时统一清空对象指针。
 *
 * 实现方式：create_service_entry 创建统一样式入口；各 clicked_cb 只设置 flow 并调用
 * 上层回调；set_*_loading 负责请求期间禁用按钮；cleanup 关闭消息框并清空所有对象引用。
 */
#include "home_page.h"

#include <inttypes.h>
#include <stdio.h>

#define HOME_HEADER_X 20
#define HOME_HEADER_Y 16
#define HOME_HEADER_WIDTH 760
#define HOME_HEADER_HEIGHT 120
#define HOME_BUTTON_WIDTH 350
#define HOME_BUTTON_HEIGHT 120

static const char *development_message_buttons[] = {"确定", ""};

static void close_message_box(ClinicHomePage *page)
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

/* 页面离开后清空所有 lv_obj_t 指针；实际 screen 删除由 main.c 统一负责。 */
void clinic_home_page_cleanup(ClinicHomePage *page)
{
    if(page == NULL) {
        return;
    }

    close_message_box(page);
    page->screen = NULL;
    page->department_button = NULL;
    page->department_button_label = NULL;
    page->doctor_button = NULL;
    page->doctor_button_label = NULL;
    page->ticket_button = NULL;
    page->ticket_button_label = NULL;
    page->current_ticket_button = NULL;
    page->current_ticket_button_label = NULL;
    page->logout_button = NULL;
    page->logout_button_label = NULL;
    page->logout_requested = 0;
    page->font = NULL;
    page->department_request_callback = NULL;
    page->department_request_user_data = NULL;
    page->current_ticket_request_callback = NULL;
    page->current_ticket_request_user_data = NULL;
}

static void development_message_event_cb(lv_event_t *event)
{
    ClinicHomePage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        close_message_box(page);
    }
}

int clinic_home_page_show_message(ClinicHomePage *page, const char *message)
{
    lv_obj_t *backdrop;
    lv_obj_t *title;
    lv_obj_t *text;
    lv_obj_t *content;
    lv_obj_t *buttons;

    if(page == NULL || page->font == NULL || message == NULL) {
        return -1;
    }
    if(page->message_box != NULL && lv_obj_is_valid(page->message_box)) {
        lv_label_set_text(lv_msgbox_get_text(page->message_box), message);
        return 0;
    }
    page->message_box = NULL;

    page->message_box = lv_msgbox_create(
        NULL,
        "提示",
        message,
        development_message_buttons,
        false);
    if(page->message_box == NULL) {
        fprintf(stderr, "failed to create home page message box\n");
        return -1;
    }

    backdrop = lv_obj_get_parent(page->message_box);
    title = lv_msgbox_get_title(page->message_box);
    text = lv_msgbox_get_text(page->message_box);
    content = lv_msgbox_get_content(page->message_box);
    buttons = lv_msgbox_get_btns(page->message_box);
    if(backdrop == NULL || title == NULL || text == NULL ||
       content == NULL || buttons == NULL) {
        fprintf(stderr, "failed to initialize home page message box\n");
        close_message_box(page);
        return -1;
    }

    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_50, LV_PART_MAIN);

    lv_obj_set_width(page->message_box, 440);
    lv_obj_set_style_bg_color(page->message_box, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page->message_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(page->message_box, lv_color_hex(0x1C668C), LV_PART_MAIN);
    lv_obj_set_style_border_width(page->message_box, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(page->message_box, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page->message_box, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_row(page->message_box, 14, LV_PART_MAIN);

    lv_obj_set_style_text_font(title, page->font, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x123F5A), LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(text, page->font, LV_PART_MAIN);
    lv_obj_set_style_text_color(text, lv_color_hex(0x173C35), LV_PART_MAIN);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_set_size(buttons, 180, 50);
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
    lv_obj_set_style_border_width(buttons, 0,
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(buttons, 8,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(buttons, lv_color_hex(0x073E5D),
                              LV_PART_ITEMS | LV_STATE_PRESSED);

    lv_obj_add_event_cb(
        page->message_box,
        development_message_event_cb,
        LV_EVENT_VALUE_CHANGED,
        page);
    lv_obj_center(page->message_box);
    return 0;
}

void clinic_home_page_set_department_loading(
    ClinicHomePage *page,
    int loading)
{
    if(page == NULL || page->department_button == NULL ||
       page->department_button_label == NULL ||
       page->doctor_button == NULL || page->doctor_button_label == NULL ||
       page->ticket_button == NULL || page->ticket_button_label == NULL ||
       page->current_ticket_button == NULL || page->logout_button == NULL ||
       !lv_obj_is_valid(page->department_button) ||
       !lv_obj_is_valid(page->department_button_label) ||
       !lv_obj_is_valid(page->doctor_button) ||
       !lv_obj_is_valid(page->doctor_button_label) ||
       !lv_obj_is_valid(page->ticket_button) ||
       !lv_obj_is_valid(page->ticket_button_label) ||
       !lv_obj_is_valid(page->current_ticket_button) ||
       !lv_obj_is_valid(page->logout_button)) {
        return;
    }

    if(loading) {
        lv_label_set_text(page->department_button_label, "正在加载科室...");
        lv_label_set_text(page->doctor_button_label, "正在准备查询...");
        lv_label_set_text(page->ticket_button_label, "正在准备取号...");
        lv_obj_add_state(page->department_button, LV_STATE_DISABLED);
        lv_obj_add_state(page->doctor_button, LV_STATE_DISABLED);
        lv_obj_add_state(page->ticket_button, LV_STATE_DISABLED);
        lv_obj_add_state(page->current_ticket_button, LV_STATE_DISABLED);
        lv_obj_add_state(page->logout_button, LV_STATE_DISABLED);
    }
    else {
        lv_obj_clear_state(page->department_button, LV_STATE_DISABLED);
        lv_obj_clear_state(page->doctor_button, LV_STATE_DISABLED);
        lv_obj_clear_state(page->ticket_button, LV_STATE_DISABLED);
        lv_obj_clear_state(page->current_ticket_button, LV_STATE_DISABLED);
        lv_obj_clear_state(page->logout_button, LV_STATE_DISABLED);
        lv_label_set_text(page->department_button_label, "科室查询");
        lv_label_set_text(page->doctor_button_label, "医生查询");
        lv_label_set_text(page->ticket_button_label, "门诊取号");
    }
    lv_obj_center(page->department_button_label);
    lv_obj_center(page->doctor_button_label);
    lv_obj_center(page->ticket_button_label);
}

void clinic_home_page_set_current_ticket_loading(
    ClinicHomePage *page,
    int loading)
{
    if(page == NULL || page->current_ticket_button == NULL ||
       page->current_ticket_button_label == NULL ||
       page->department_button == NULL || page->doctor_button == NULL ||
       page->ticket_button == NULL || page->logout_button == NULL ||
       !lv_obj_is_valid(page->current_ticket_button) ||
       !lv_obj_is_valid(page->current_ticket_button_label) ||
       !lv_obj_is_valid(page->department_button) ||
       !lv_obj_is_valid(page->doctor_button) ||
       !lv_obj_is_valid(page->ticket_button) ||
       !lv_obj_is_valid(page->logout_button)) {
        return;
    }

    if(loading) {
        lv_label_set_text(page->current_ticket_button_label, "正在查询号单...");
        lv_obj_add_state(page->current_ticket_button, LV_STATE_DISABLED);
        lv_obj_add_state(page->department_button, LV_STATE_DISABLED);
        lv_obj_add_state(page->doctor_button, LV_STATE_DISABLED);
        lv_obj_add_state(page->ticket_button, LV_STATE_DISABLED);
        lv_obj_add_state(page->logout_button, LV_STATE_DISABLED);
    }
    else {
        lv_obj_clear_state(page->current_ticket_button, LV_STATE_DISABLED);
        lv_obj_clear_state(page->department_button, LV_STATE_DISABLED);
        lv_obj_clear_state(page->doctor_button, LV_STATE_DISABLED);
        lv_obj_clear_state(page->ticket_button, LV_STATE_DISABLED);
        lv_obj_clear_state(page->logout_button, LV_STATE_DISABLED);
        lv_label_set_text(page->current_ticket_button_label, "排队查询");
    }
    lv_obj_center(page->current_ticket_button_label);
}

/* 科室查询入口只请求科室列表，并把 flow 标成“只查看科室”。 */
static void department_entry_clicked_cb(lv_event_t *event)
{
    ClinicHomePage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_CLICKED && page != NULL &&
       page->department_request_callback != NULL) {
        page->department_request_callback(
            page->department_request_user_data,
            CLINIC_SERVICE_FLOW_DEPARTMENT_QUERY);
    }
}

/* 医生查询先进入科室筛选流程，选择科室后才请求医生。 */
static void doctor_entry_clicked_cb(lv_event_t *event)
{
    ClinicHomePage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_CLICKED && page != NULL &&
       page->department_request_callback != NULL) {
        page->department_request_callback(
            page->department_request_user_data,
            CLINIC_SERVICE_FLOW_DOCTOR_QUERY);
    }
}

/* 门诊取号同样先选择科室，但 flow 会让科室页显示确认取号逻辑。 */
static void ticket_entry_clicked_cb(lv_event_t *event)
{
    ClinicHomePage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_CLICKED && page != NULL &&
       page->department_request_callback != NULL) {
        page->department_request_callback(
            page->department_request_user_data,
            CLINIC_SERVICE_FLOW_TICKET);
    }
}

static void current_ticket_entry_clicked_cb(lv_event_t *event)
{
    ClinicHomePage *page = lv_event_get_user_data(event);

    if(page != NULL && page->current_ticket_request_callback != NULL) {
        page->current_ticket_request_callback(
            page->current_ticket_request_user_data);
    }
}

static void logout_entry_clicked_cb(lv_event_t *event)
{
    ClinicHomePage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_CLICKED && page != NULL) {
        page->logout_requested = 1;
        if(page->logout_button != NULL &&
           lv_obj_is_valid(page->logout_button)) {
            lv_obj_add_state(page->logout_button, LV_STATE_DISABLED);
        }
    }
}

static lv_obj_t *create_service_entry(
    ClinicHomePage *page,
    lv_obj_t *screen,
    const char *text,
    lv_coord_t x,
    lv_coord_t y,
    uint32_t color,
    uint32_t pressed_color,
    lv_event_cb_t event_callback,
    lv_obj_t **label_out)
{
    lv_obj_t *button = lv_btn_create(screen);
    lv_obj_t *label;

    if(button == NULL) {
        return NULL;
    }
    label = lv_label_create(button);
    if(label == NULL) {
        return NULL;
    }

    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, HOME_BUTTON_WIDTH, HOME_BUTTON_HEIGHT);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(pressed_color),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_PRESSED);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, page->font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(label);

    lv_obj_add_event_cb(button, event_callback, LV_EVENT_CLICKED, page);
    if(label_out != NULL) {
        *label_out = label;
    }
    return button;
}

/* 创建主页视觉结构并保存上层回调；创建完成后页面仍不拥有任何网络线程。 */
int clinic_home_page_create(
    ClinicHomePage *page,
    lv_obj_t *screen,
    const lv_font_t *font,
    int64_t user_id,
    ClinicHomeDepartmentRequestCallback department_request_callback,
    void *department_request_user_data,
    ClinicHomeCurrentTicketRequestCallback current_ticket_request_callback,
    void *current_ticket_request_user_data)
{
    char user_text[64];
    int user_text_length;
    lv_obj_t *header;
    lv_obj_t *title;
    lv_obj_t *welcome;
    lv_obj_t *user_label;
    lv_obj_t *logout_label;

    if(page == NULL || screen == NULL || font == NULL || user_id <= 0 ||
       department_request_callback == NULL ||
       current_ticket_request_callback == NULL) {
        return -1;
    }

    user_text_length = snprintf(
        user_text,
        sizeof(user_text),
        "用户 ID：%" PRId64,
        user_id);
    if(user_text_length < 0 || (size_t)user_text_length >= sizeof(user_text)) {
        return -1;
    }

    page->screen = screen;
    page->department_button = NULL;
    page->department_button_label = NULL;
    page->doctor_button = NULL;
    page->doctor_button_label = NULL;
    page->ticket_button = NULL;
    page->ticket_button_label = NULL;
    page->current_ticket_button = NULL;
    page->current_ticket_button_label = NULL;
    page->logout_button = NULL;
    page->logout_button_label = NULL;
    page->logout_requested = 0;
    page->message_box = NULL;
    page->font = font;
    page->department_request_callback = department_request_callback;
    page->department_request_user_data = department_request_user_data;
    page->current_ticket_request_callback = current_ticket_request_callback;
    page->current_ticket_request_user_data = current_ticket_request_user_data;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0xEAF3EF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    header = lv_obj_create(screen);
    if(header == NULL) {
        return -1;
    }
    lv_obj_set_pos(header, HOME_HEADER_X, HOME_HEADER_Y);
    lv_obj_set_size(header, HOME_HEADER_WIDTH, HOME_HEADER_HEIGHT);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(header, lv_color_hex(0xB8D0C7), LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(header, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);

    title = lv_label_create(header);
    welcome = lv_label_create(header);
    user_label = lv_label_create(header);
    page->logout_button = lv_btn_create(header);
    if(page->logout_button != NULL) {
        logout_label = lv_label_create(page->logout_button);
        page->logout_button_label = logout_label;
    }
    if(title == NULL || welcome == NULL || user_label == NULL ||
       page->logout_button == NULL || page->logout_button_label == NULL) {
        return -1;
    }

    lv_obj_set_pos(page->logout_button, 600, 12);
    lv_obj_set_size(page->logout_button, 135, 42);
    lv_obj_set_style_bg_color(page->logout_button, lv_color_hex(0xA64B3C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page->logout_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(page->logout_button, 8, LV_PART_MAIN);
    lv_label_set_text(page->logout_button_label, "退出登录");
    lv_obj_set_style_text_font(page->logout_button_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(page->logout_button_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(page->logout_button_label);
    lv_obj_add_event_cb(page->logout_button, logout_entry_clicked_cb, LV_EVENT_CLICKED, page);

    lv_label_set_text(title, "医路通");
    lv_obj_set_style_text_font(title, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0D5A4C), LV_PART_MAIN);
    lv_obj_set_pos(title, 24, 14);

    lv_label_set_text(welcome, "欢迎使用医疗服务");
    lv_obj_set_style_text_font(welcome, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(welcome, lv_color_hex(0x173C35), LV_PART_MAIN);
    lv_obj_set_pos(welcome, 24, 66);

    lv_label_set_text(user_label, user_text);
    lv_obj_set_width(user_label, 270);
    lv_obj_set_style_text_font(user_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(user_label, lv_color_hex(0x1C668C), LV_PART_MAIN);
    lv_obj_set_style_text_align(user_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_pos(user_label, 465, 66);

    page->department_button = create_service_entry(
        page,
        screen,
        "科室查询",
        35,
        160,
        0x0D806B,
        0x095B4D,
        department_entry_clicked_cb,
        &page->department_button_label);
    page->doctor_button = create_service_entry(
        page,
        screen,
        "医生查询",
        415,
        160,
        0x1C668C,
        0x124965,
        doctor_entry_clicked_cb,
        &page->doctor_button_label);
    page->ticket_button = create_service_entry(
        page,
        screen,
        "门诊取号",
        35,
        300,
        0x507A49,
        0x365532,
        ticket_entry_clicked_cb,
        &page->ticket_button_label);
    if(page->department_button == NULL || page->doctor_button == NULL ||
       page->ticket_button == NULL) {
        return -1;
    }
    page->current_ticket_button = create_service_entry(
        page,
        screen,
        "排队查询",
        415,
        300,
        0xA66B2B,
        0x754918,
        current_ticket_entry_clicked_cb,
        &page->current_ticket_button_label);
    if(page->current_ticket_button == NULL) {
        return -1;
    }

    return 0;
}
