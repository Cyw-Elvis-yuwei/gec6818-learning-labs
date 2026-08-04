/*
 * 文件作用（答辩）：显示服务器返回的科室列表，并按当前 ClinicServiceFlow 决定点击行为。
 * 科室查询模式只显示科室信息；医生查询模式把 department_id 交给医生请求；门诊取号
 * 模式先弹出确认框，再把 user_id/department_id 所需信息交给上层创建号单。
 *
 * 本文件不请求网络、不查询医生、不创建号单、不访问 SQLite。请求进行时可禁用返回和
 * 列表操作；cleanup 负责关闭消息框并清空页面对象，避免旧 screen 被继续访问。
 *
 * 同一个页面复用三种业务流：DEPARTMENT_QUERY 点击只看详情；DOCTOR_QUERY 点击调用
 * on_doctor_requested；TICKET_REGISTRATION 点击先确认再调用 on_ticket_requested。
 * 因而“页面展示”和“真正网络请求”仍由回调边界分离。
 */
#include "department_page.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define DEPARTMENT_LIST_X 20
#define DEPARTMENT_LIST_Y 112
#define DEPARTMENT_LIST_WIDTH 760
#define DEPARTMENT_LIST_HEIGHT 348
#define DEPARTMENT_ROW_HEIGHT 68
#define DEPARTMENT_DETAIL_TEXT_CAPACITY 256U

static const char *department_message_buttons[] = {"确定", ""};
static const char *department_detail_buttons[] = {"关闭", ""};
static const char *department_ticket_buttons[] = {
    "取消",
    "确认取号",
    ""
};

static void close_message_box(ClinicDepartmentPage *page)
{
    lv_obj_t *message_box;

    if(page == NULL) {
        return;
    }
    message_box = page->message_box;
    page->message_box = NULL;
    page->detail_action_pending = 0;
    page->selected_department_index = 0U;
    if(message_box != NULL && lv_obj_is_valid(message_box)) {
        lv_msgbox_close(message_box);
    }
}

/* 关闭当前弹窗并清空页面持有的对象/回调，防止切页后旧回调继续触发。 */
void clinic_department_page_cleanup(ClinicDepartmentPage *page)
{
    if(page == NULL) {
        return;
    }

    close_message_box(page);
    page->screen = NULL;
    page->back_button = NULL;
    page->department_matrix = NULL;
    page->status_label = NULL;
    page->font = NULL;
    page->select_callback = NULL;
    page->select_user_data = NULL;
    page->back_callback = NULL;
    page->back_user_data = NULL;
    page->department_count = 0U;
    page->request_loading = 0;
    page->detail_action_pending = 0;
    page->selected_department_index = 0U;
    page->flow = CLINIC_SERVICE_FLOW_DEPARTMENT_QUERY;
    memset(page->departments, 0, sizeof(page->departments));
}

static const char *department_page_title(ClinicServiceFlow flow)
{
    switch(flow) {
        case CLINIC_SERVICE_FLOW_DEPARTMENT_QUERY:
            return "科室查询";
        case CLINIC_SERVICE_FLOW_DOCTOR_QUERY:
            return "医生查询";
        case CLINIC_SERVICE_FLOW_TICKET:
            return "门诊取号";
        default:
            return NULL;
    }
}

static const char *department_page_hint(ClinicServiceFlow flow)
{
    switch(flow) {
        case CLINIC_SERVICE_FLOW_DEPARTMENT_QUERY:
            return "查看服务端提供的科室名称与编号";
        case CLINIC_SERVICE_FLOW_DOCTOR_QUERY:
            return "选择科室筛选医生，再查看医生详情";
        case CLINIC_SERVICE_FLOW_TICKET:
            return "选择科室后确认获取该科室当日排队号";
        default:
            return NULL;
    }
}

static void message_box_event_cb(lv_event_t *event)
{
    ClinicDepartmentPage *page = lv_event_get_user_data(event);
    ClinicDepartment selected_department;
    ClinicDepartmentSelectCallback action_callback;
    void *action_user_data;
    int invoke_action = 0;

    if(lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED || page == NULL) {
        return;
    }

    memset(&selected_department, 0, sizeof(selected_department));
    action_callback = page->select_callback;
    action_user_data = page->select_user_data;
    if(page->message_box != NULL && page->detail_action_pending &&
       lv_msgbox_get_active_btn(page->message_box) == 1U &&
       page->selected_department_index < page->department_count &&
       action_callback != NULL) {
        selected_department =
            page->departments[page->selected_department_index];
        invoke_action = 1;
    }
    close_message_box(page);

    if(invoke_action) {
        action_callback(
            selected_department.id,
            selected_department.name,
            action_user_data);
    }
}

static int create_message_box(
    ClinicDepartmentPage *page,
    const char *title_text,
    const char *message,
    const char *buttons_map[],
    int detail_action_pending,
    size_t selected_department_index)
{
    lv_obj_t *backdrop;
    lv_obj_t *title;
    lv_obj_t *text;
    lv_obj_t *content;
    lv_obj_t *buttons;

    if(page == NULL || page->font == NULL || title_text == NULL ||
       title_text[0] == '\0' || message == NULL || message[0] == '\0' ||
       buttons_map == NULL ||
       (detail_action_pending &&
        selected_department_index >= page->department_count)) {
        return -1;
    }
    if(page->message_box != NULL && lv_obj_is_valid(page->message_box)) {
        return 0;
    }
    page->message_box = NULL;
    page->detail_action_pending = detail_action_pending != 0;
    page->selected_department_index = selected_department_index;

    page->message_box = lv_msgbox_create(
        NULL,
        title_text,
        message,
        buttons_map,
        false);
    if(page->message_box == NULL) {
        page->detail_action_pending = 0;
        page->selected_department_index = 0U;
        fprintf(stderr, "failed to create department message box\n");
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

    lv_obj_set_width(page->message_box, 520);
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

    lv_obj_set_size(buttons, 390, 50);
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

int clinic_department_page_show_message(
    ClinicDepartmentPage *page,
    const char *message)
{
    if(message == NULL || strlen(message) > CLINIC_MESSAGE_MAX_LENGTH) {
        return -1;
    }
    return create_message_box(
        page,
        "提示",
        message,
        department_message_buttons,
        0,
        0U);
}

static int show_department_detail(
    ClinicDepartmentPage *page,
    size_t department_index)
{
    char detail_text[DEPARTMENT_DETAIL_TEXT_CAPACITY];
    const ClinicDepartment *department;
    const char *title_text;
    const char **buttons_map;
    int action_pending;
    int written;

    if(page == NULL || department_index >= page->department_count) {
        return -1;
    }
    department = &page->departments[department_index];
    if(page->flow == CLINIC_SERVICE_FLOW_DEPARTMENT_QUERY) {
        title_text = "科室信息";
        buttons_map = department_detail_buttons;
        action_pending = 0;
        written = snprintf(
            detail_text,
            sizeof(detail_text),
            "科室名称：%s\n科室编号：%" PRId64,
            department->name,
            department->id);
    }
    else if(page->flow == CLINIC_SERVICE_FLOW_TICKET) {
        title_text = "确认门诊取号";
        buttons_map = department_ticket_buttons;
        action_pending = page->select_callback != NULL;
        written = snprintf(
            detail_text,
            sizeof(detail_text),
            "取号科室：%s\n科室编号：%" PRId64
            "\n\n确认获取该科室当日排队号？",
            department->name,
            department->id);
    }
    else {
        return -1;
    }
    if(written < 0 || (size_t)written >= sizeof(detail_text)) {
        return -1;
    }
    return create_message_box(
        page,
        title_text,
        detail_text,
        buttons_map,
        action_pending,
        department_index);
}

/* 根据当前 flow 分派点击含义；本函数只通知上层，不直接发 TCP 请求。 */
static void department_matrix_event_cb(lv_event_t *event)
{
    ClinicDepartmentPage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED &&
       page != NULL && !page->request_loading) {
        uint16_t selected =
            lv_btnmatrix_get_selected_btn(page->department_matrix);

        if(selected != LV_BTNMATRIX_BTN_NONE &&
           (size_t)selected < page->department_count) {
            if(page->flow == CLINIC_SERVICE_FLOW_DOCTOR_QUERY &&
               page->select_callback != NULL) {
                page->select_callback(
                    page->departments[selected].id,
                    page->departments[selected].name,
                    page->select_user_data);
            }
            else {
                (void)show_department_detail(page, (size_t)selected);
            }
        }
    }
}

static void back_button_event_cb(lv_event_t *event)
{
    ClinicDepartmentPage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_CLICKED && page != NULL &&
       !page->request_loading && page->back_callback != NULL) {
        page->back_callback(page->back_user_data);
    }
}

/* 医生或取号请求期间禁用列表和返回，避免重复请求及请求中途切页。 */
void clinic_department_page_set_request_loading(
    ClinicDepartmentPage *page,
    int loading)
{
    if(page == NULL || page->status_label == NULL ||
       !lv_obj_is_valid(page->status_label)) {
        return;
    }

    page->request_loading = loading != 0;
    lv_label_set_text(
        page->status_label,
        page->request_loading
            ? (page->flow == CLINIC_SERVICE_FLOW_TICKET
                   ? "正在取号..."
                   : "正在加载医生...")
            : "");
    if(page->back_button != NULL && lv_obj_is_valid(page->back_button)) {
        if(page->request_loading) {
            lv_obj_add_state(page->back_button, LV_STATE_DISABLED);
        }
        else {
            lv_obj_clear_state(page->back_button, LV_STATE_DISABLED);
        }
    }
    if(page->department_matrix != NULL &&
       lv_obj_is_valid(page->department_matrix)) {
        if(page->request_loading) {
            lv_obj_add_state(page->department_matrix, LV_STATE_DISABLED);
        }
        else {
            lv_obj_clear_state(page->department_matrix, LV_STATE_DISABLED);
        }
    }
}

static int prepare_button_map(
    ClinicDepartmentPage *page,
    const ClinicDepartment *departments,
    size_t department_count)
{
    size_t map_index = 0U;
    size_t index;

    for(index = 0U; index < department_count; ++index) {
        int written = snprintf(
            page->button_texts[index],
            sizeof(page->button_texts[index]),
            "%s    ID：%" PRId64,
            departments[index].name,
            departments[index].id);

        if(written < 0 ||
           (size_t)written >= sizeof(page->button_texts[index])) {
            return -1;
        }
        page->departments[index] = departments[index];
        page->button_map[map_index++] = page->button_texts[index];
        if(index + 1U < department_count) {
            page->button_map[map_index++] = "\n";
        }
    }
    page->button_map[map_index] = "";
    return 0;
}

/* 根据服务器返回的 departments 动态生成按钮文本和按钮映射。 */
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
    void *back_user_data)
{
    lv_obj_t *back_button;
    lv_obj_t *back_label;
    lv_obj_t *title;
    lv_obj_t *hint;
    lv_obj_t *list_container;
    const char *title_text;
    const char *hint_text;

    if(page == NULL || screen == NULL || font == NULL ||
       !clinic_service_flow_is_valid(flow) ||
       department_count > CLINIC_MAX_DEPARTMENTS ||
       (department_count > 0U && departments == NULL) ||
       (flow != CLINIC_SERVICE_FLOW_DEPARTMENT_QUERY &&
        select_callback == NULL) ||
       back_callback == NULL) {
        return -1;
    }
    title_text = department_page_title(flow);
    hint_text = department_page_hint(flow);
    if(title_text == NULL || hint_text == NULL) {
        return -1;
    }
    if(department_count > 0U &&
       prepare_button_map(page, departments, department_count) != 0) {
        return -1;
    }

    page->screen = screen;
    page->back_button = NULL;
    page->department_matrix = NULL;
    page->status_label = NULL;
    page->message_box = NULL;
    page->font = font;
    page->select_callback = select_callback;
    page->select_user_data = select_user_data;
    page->back_callback = back_callback;
    page->back_user_data = back_user_data;
    page->department_count = department_count;
    page->request_loading = 0;
    page->detail_action_pending = 0;
    page->selected_department_index = 0U;
    page->flow = flow;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0xEAF3EF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    page->back_button = lv_btn_create(screen);
    back_button = page->back_button;
    back_label = back_button == NULL ? NULL : lv_label_create(back_button);
    title = lv_label_create(screen);
    hint = lv_label_create(screen);
    page->status_label = lv_label_create(screen);
    list_container = lv_obj_create(screen);
    if(back_button == NULL || back_label == NULL || title == NULL ||
       hint == NULL || page->status_label == NULL || list_container == NULL) {
        return -1;
    }

    lv_obj_set_pos(back_button, 20, 18);
    lv_obj_set_size(back_button, 110, 52);
    lv_obj_set_style_bg_color(back_button, lv_color_hex(0x1C668C),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(back_button, 10, LV_PART_MAIN);
    lv_label_set_text(back_label, "返回");
    lv_obj_set_style_text_font(back_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF),
                                LV_PART_MAIN);
    lv_obj_center(back_label);
    lv_obj_add_event_cb(
        back_button,
        back_button_event_cb,
        LV_EVENT_CLICKED,
        page);

    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_font(title, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0D5A4C), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_label_set_text(hint, hint_text);
    lv_obj_set_width(hint, 600);
    lv_obj_set_style_text_font(hint, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x60736D), LV_PART_MAIN);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 66);

    lv_label_set_text(page->status_label, "");
    lv_obj_set_width(page->status_label, 180);
    lv_obj_set_style_text_font(page->status_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(page->status_label, lv_color_hex(0x1C668C),
                                LV_PART_MAIN);
    lv_obj_set_style_text_align(page->status_label, LV_TEXT_ALIGN_RIGHT,
                                LV_PART_MAIN);
    lv_obj_set_pos(page->status_label, 600, 24);

    lv_obj_set_pos(list_container, DEPARTMENT_LIST_X, DEPARTMENT_LIST_Y);
    lv_obj_set_size(list_container,
                    DEPARTMENT_LIST_WIDTH,
                    DEPARTMENT_LIST_HEIGHT);
    lv_obj_set_scroll_dir(list_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(list_container, lv_color_hex(0xFFFFFF),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list_container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(list_container, lv_color_hex(0xB8D0C7),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(list_container, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(list_container, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list_container, 10, LV_PART_MAIN);

    if(department_count == 0U) {
        lv_obj_t *empty_label = lv_label_create(list_container);

        if(empty_label == NULL) {
            return -1;
        }
        lv_label_set_text(empty_label, "暂无科室数据");
        lv_obj_set_style_text_font(empty_label, font, LV_PART_MAIN);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0x60736D),
                                    LV_PART_MAIN);
        lv_obj_center(empty_label);
        return 0;
    }

    page->department_matrix = lv_btnmatrix_create(list_container);
    if(page->department_matrix == NULL) {
        return -1;
    }
    lv_btnmatrix_set_map(page->department_matrix, page->button_map);
    lv_obj_set_pos(page->department_matrix, 0, 0);
    lv_obj_set_size(page->department_matrix,
                    DEPARTMENT_LIST_WIDTH - 24,
                    (lv_coord_t)(department_count * DEPARTMENT_ROW_HEIGHT));
    lv_obj_clear_flag(page->department_matrix, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page->department_matrix, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_set_style_bg_opa(page->department_matrix, LV_OPA_TRANSP,
                            LV_PART_MAIN);
    lv_obj_set_style_border_width(page->department_matrix, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page->department_matrix, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(page->department_matrix, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(page->department_matrix, lv_color_hex(0xDCEAE5),
                              LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(page->department_matrix, LV_OPA_COVER,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(page->department_matrix,
                                  lv_color_hex(0x91B2A7),
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(page->department_matrix, 1,
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(page->department_matrix, 10,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(page->department_matrix, font,
                               LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(page->department_matrix,
                                lv_color_hex(0x173C35),
                                LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(page->department_matrix, lv_color_hex(0xB8D0C7),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(
        page->department_matrix,
        department_matrix_event_cb,
        LV_EVENT_VALUE_CHANGED,
        page);
    return 0;
}
