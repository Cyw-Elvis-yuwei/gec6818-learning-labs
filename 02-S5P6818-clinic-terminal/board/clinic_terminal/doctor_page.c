/*
 * 文件作用：显示某科室的医生列表和医生详情。
 * 数据来自服务器，页面展示姓名、职称和擅长方向；点击医生只打开详情，不执行取号，
 * 因为本项目的号单按科室创建，并不是指定医生预约。
 *
 * 本文件是纯 LVGL 视图：不访问网络和 SQLite。返回时由上层使用缓存的科室列表重建
 * 科室页，cleanup 关闭详情框并清空对象指针。
 *
 * 实现方式：prepare_button_map 根据医生数组生成按钮；矩阵点击只调用
 * show_doctor_detail，把当前医生结构体格式化到消息框；返回按钮只设置上层返回请求。
 */
#include "doctor_page.h"

#include <stdio.h>
#include <string.h>

#define DOCTOR_LIST_X 20
#define DOCTOR_LIST_Y 142
#define DOCTOR_LIST_WIDTH 760
#define DOCTOR_LIST_HEIGHT 318
#define DOCTOR_ROW_HEIGHT 68
#define DOCTOR_DETAIL_TEXT_CAPACITY 512U

static const char *doctor_detail_buttons[] = {"关闭", ""};

static void close_message_box(ClinicDoctorPage *page)
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

void clinic_doctor_page_cleanup(ClinicDoctorPage *page)
{
    if(page == NULL) {
        return;
    }

    close_message_box(page);
    page->screen = NULL;
    page->back_button = NULL;
    page->doctor_matrix = NULL;
    page->font = NULL;
    page->back_callback = NULL;
    page->back_user_data = NULL;
    page->doctor_count = 0U;
    memset(page->doctors, 0, sizeof(page->doctors));
    memset(page->department_name, 0, sizeof(page->department_name));
}

static void message_box_event_cb(lv_event_t *event)
{
    ClinicDoctorPage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        close_message_box(page);
    }
}

static int create_message_box(
    ClinicDoctorPage *page,
    const char *title_text,
    const char *message,
    const char *buttons_map[])
{
    lv_obj_t *backdrop;
    lv_obj_t *title;
    lv_obj_t *text;
    lv_obj_t *content;
    lv_obj_t *buttons;

    if(page == NULL || page->font == NULL || title_text == NULL ||
       title_text[0] == '\0' || message == NULL || message[0] == '\0' ||
       buttons_map == NULL) {
        return -1;
    }
    if(page->message_box != NULL && lv_obj_is_valid(page->message_box)) {
        return 0;
    }
    page->message_box = NULL;

    page->message_box = lv_msgbox_create(
        NULL,
        title_text,
        message,
        buttons_map,
        false);
    if(page->message_box == NULL) {
        fprintf(stderr, "failed to create doctor message box\n");
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

    lv_obj_set_size(buttons, 430, 50);
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

/* 展示姓名、职称、擅长和所属科室，不调用 create_ticket。 */
static int show_doctor_detail(ClinicDoctorPage *page, size_t doctor_index)
{
    char detail_text[DOCTOR_DETAIL_TEXT_CAPACITY];
    const ClinicDoctor *doctor;
    int written;

    if(page == NULL || doctor_index >= page->doctor_count) {
        return -1;
    }
    doctor = &page->doctors[doctor_index];

    written = snprintf(
        detail_text,
        sizeof(detail_text),
        "姓名：%s\n职称：%s\n擅长：%s\n所属科室：%s",
        doctor->name,
        doctor->title,
        doctor->specialty,
        page->department_name);
    if(written < 0 || (size_t)written >= sizeof(detail_text)) {
        return -1;
    }
    return create_message_box(
        page,
        "医生详情",
        detail_text,
        doctor_detail_buttons);
}

static void doctor_matrix_event_cb(lv_event_t *event)
{
    ClinicDoctorPage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED && page != NULL) {
        uint16_t selected =
            lv_btnmatrix_get_selected_btn(page->doctor_matrix);

        if(selected != LV_BTNMATRIX_BTN_NONE &&
           (size_t)selected < page->doctor_count) {
            (void)show_doctor_detail(page, (size_t)selected);
        }
    }
}

static void back_button_event_cb(lv_event_t *event)
{
    ClinicDoctorPage *page = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_CLICKED && page != NULL &&
       page->back_callback != NULL) {
        page->back_callback(page->back_user_data);
    }
}

static int prepare_button_map(
    ClinicDoctorPage *page,
    const ClinicDoctor *doctors,
    size_t doctor_count)
{
    size_t map_index = 0U;
    size_t index;

    for(index = 0U; index < doctor_count; ++index) {
        int written = snprintf(
            page->button_texts[index],
            sizeof(page->button_texts[index]),
            "%s    %s",
            doctors[index].name,
            doctors[index].title);

        if(written < 0 ||
           (size_t)written >= sizeof(page->button_texts[index])) {
            return -1;
        }
        page->button_map[map_index++] = page->button_texts[index];
        if(index + 1U < doctor_count) {
            page->button_map[map_index++] = "\n";
        }
    }
    page->button_map[map_index] = "";
    return 0;
}

/* 使用已解析好的医生数组创建纯展示页，数据所有权仍由页面结构管理。 */
int clinic_doctor_page_create(
    ClinicDoctorPage *page,
    lv_obj_t *screen,
    const lv_font_t *font,
    const char *department_name,
    const ClinicDoctor *doctors,
    size_t doctor_count,
    ClinicDoctorBackCallback back_callback,
    void *back_user_data)
{
    char department_text[CLINIC_DEPARTMENT_NAME_MAX_LENGTH + 32U];
    int department_text_length;
    lv_obj_t *back_button;
    lv_obj_t *back_label;
    lv_obj_t *title;
    lv_obj_t *department_label;
    lv_obj_t *hint_label;
    lv_obj_t *list_container;

    if(page == NULL || screen == NULL || font == NULL ||
       department_name == NULL || department_name[0] == '\0' ||
       strlen(department_name) > CLINIC_DEPARTMENT_NAME_MAX_LENGTH ||
       doctor_count > CLINIC_MAX_DOCTORS ||
       (doctor_count > 0U && doctors == NULL) ||
       back_callback == NULL) {
        return -1;
    }
    department_text_length = snprintf(
        department_text,
        sizeof(department_text),
        "筛选科室：%s",
        department_name);
    if(department_text_length < 0 ||
       (size_t)department_text_length >= sizeof(department_text) ||
       (doctor_count > 0U &&
        prepare_button_map(page, doctors, doctor_count) != 0)) {
        return -1;
    }

    page->screen = screen;
    page->back_button = NULL;
    page->doctor_matrix = NULL;
    page->message_box = NULL;
    page->font = font;
    page->back_callback = back_callback;
    page->back_user_data = back_user_data;
    page->doctor_count = doctor_count;
    memset(page->doctors, 0, sizeof(page->doctors));
    if(doctor_count > 0U) {
        memcpy(
            page->doctors,
            doctors,
            doctor_count * sizeof(page->doctors[0]));
    }
    memcpy(page->department_name, department_name, strlen(department_name) + 1U);

    lv_obj_set_style_bg_color(screen, lv_color_hex(0xEAF3EF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    page->back_button = lv_btn_create(screen);
    back_button = page->back_button;
    back_label = back_button == NULL ? NULL : lv_label_create(back_button);
    title = lv_label_create(screen);
    department_label = lv_label_create(screen);
    hint_label = lv_label_create(screen);
    list_container = lv_obj_create(screen);
    if(back_button == NULL || back_label == NULL || title == NULL ||
       department_label == NULL || hint_label == NULL ||
       list_container == NULL) {
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

    lv_label_set_text(title, "医生查询");
    lv_obj_set_style_text_font(title, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0D5A4C), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_label_set_text(department_label, department_text);
    lv_obj_set_width(department_label, 740);
    lv_obj_set_style_text_font(department_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(department_label, lv_color_hex(0x1C668C),
                                LV_PART_MAIN);
    lv_obj_set_style_text_align(department_label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_align(department_label, LV_ALIGN_TOP_MID, 0, 65);

    lv_label_set_text(hint_label, "点击医生查看职称与擅长方向");
    lv_obj_set_width(hint_label, 700);
    lv_obj_set_style_text_font(hint_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x60736D),
                                LV_PART_MAIN);
    lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_align(hint_label, LV_ALIGN_TOP_MID, 0, 100);

    lv_obj_set_pos(list_container, DOCTOR_LIST_X, DOCTOR_LIST_Y);
    lv_obj_set_size(list_container, DOCTOR_LIST_WIDTH, DOCTOR_LIST_HEIGHT);
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

    if(doctor_count == 0U) {
        lv_obj_t *empty_label = lv_label_create(list_container);

        if(empty_label == NULL) {
            return -1;
        }
        lv_label_set_text(empty_label, "暂无医生数据");
        lv_obj_set_style_text_font(empty_label, font, LV_PART_MAIN);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0x60736D),
                                    LV_PART_MAIN);
        lv_obj_center(empty_label);
        return 0;
    }

    page->doctor_matrix = lv_btnmatrix_create(list_container);
    if(page->doctor_matrix == NULL) {
        return -1;
    }
    lv_btnmatrix_set_map(page->doctor_matrix, page->button_map);
    lv_obj_set_pos(page->doctor_matrix, 0, 0);
    lv_obj_set_size(page->doctor_matrix,
                    DOCTOR_LIST_WIDTH - 24,
                    (lv_coord_t)(doctor_count * DOCTOR_ROW_HEIGHT));
    lv_obj_clear_flag(page->doctor_matrix, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page->doctor_matrix, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_set_style_bg_opa(page->doctor_matrix, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(page->doctor_matrix, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page->doctor_matrix, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(page->doctor_matrix, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(page->doctor_matrix, lv_color_hex(0xDCEAE5),
                              LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(page->doctor_matrix, LV_OPA_COVER,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(page->doctor_matrix,
                                  lv_color_hex(0x91B2A7),
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(page->doctor_matrix, 1,
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(page->doctor_matrix, 10,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(page->doctor_matrix, font,
                               LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(page->doctor_matrix, lv_color_hex(0x173C35),
                                LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(page->doctor_matrix, lv_color_hex(0xB8D0C7),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(
        page->doctor_matrix,
        doctor_matrix_event_cb,
        LV_EVENT_VALUE_CHANGED,
        page);
    return 0;
}
