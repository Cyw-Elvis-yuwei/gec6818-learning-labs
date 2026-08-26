/*
 * 文件作用：创建和管理注册页面、三组输入框、软键盘及拼音候选栏。
 * 页面先做空值、长度、换行和两次密码一致性检查，再通过回调把稳定文本交给 main.c；
 * 真正的 TCP 注册请求不在本文件执行。
 *
 * 生命周期要点：所有键盘、IME、候选栏、输入框和消息对象都属于当前 screen；离开页面
 * 前解除事件绑定并清空指针，网络请求期间设置 busy，防止重复提交或切页。
 *
 * 实现顺序：create 建立控件与事件绑定；register_clicked_cb 只做本地输入检查并回调
 * main.c；main.c 的 auth_worker 完成网络注册；set_busy/show_status 显示结果；cleanup
 * 解除键盘/IME 绑定并清空对象指针。注册页本身不拥有 pthread。
 */
#include "register_page.h"

#include "clinic_types.h"
#include "pinyin_guard.h"

#include <stdio.h>
#include <string.h>

#include "src/extra/others/ime/lv_ime_pinyin.h"

#define REGISTER_DISPLAY_WIDTH 800
#define REGISTER_KEYBOARD_HEIGHT 200

static void style_textarea(lv_obj_t *textarea, const lv_font_t *font)
{
    lv_obj_set_style_bg_color(textarea, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(textarea, lv_color_hex(0xA6B8B2), LV_PART_MAIN);
    lv_obj_set_style_border_width(textarea, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(textarea, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_left(textarea, 14, LV_PART_MAIN);
    lv_obj_set_style_text_color(textarea, lv_color_hex(0x173C35), LV_PART_MAIN);
    lv_obj_set_style_text_font(textarea, font, LV_PART_MAIN);
    lv_obj_set_style_border_color(textarea, lv_color_hex(0x0D806B),
                                  LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(textarea, 3,
                                  LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_border_width(textarea, 0, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_TRANSP,
                            LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(textarea, lv_color_hex(0x0D5A4C),
                                  LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(textarea, 3,
                                  LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(textarea, LV_BORDER_SIDE_LEFT,
                                 LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_anim_time(textarea, 500,
                               LV_PART_CURSOR | LV_STATE_FOCUSED);
}

static void style_keyboard(lv_obj_t *keyboard)
{
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x173C35), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(keyboard, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(0xF4F8F6),
                              LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(0x173C35),
                                LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_20,
                               LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(keyboard, lv_color_hex(0x8FA39C),
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(keyboard, 1,
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(keyboard, 4,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x9FB7AE),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER,
                            LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_CLICK_FOCUSABLE);
}

static void style_candidate_panel(
    lv_obj_t *candidate_panel,
    const lv_font_t *font)
{
    lv_obj_set_style_bg_color(candidate_panel, lv_color_hex(0xDCEAE5),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(candidate_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(candidate_panel, lv_color_hex(0x8FA39C),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(candidate_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(candidate_panel, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(candidate_panel, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(candidate_panel, lv_color_hex(0xFFFFFF),
                              LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(candidate_panel, LV_OPA_COVER,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(candidate_panel, lv_color_hex(0x173C35),
                                LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(candidate_panel, font,
                               LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(candidate_panel, lv_color_hex(0xB5C7C1),
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(candidate_panel, 1,
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(candidate_panel, 4,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(candidate_panel, lv_color_hex(0x0D806B),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(candidate_panel, lv_color_hex(0xFFFFFF),
                                LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_clear_flag(candidate_panel, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_btnmatrix_set_btn_ctrl(
        candidate_panel,
        0U,
        (lv_btnmatrix_ctrl_t)(LV_BTNMATRIX_CTRL_HIDDEN |
                              LV_BTNMATRIX_CTRL_DISABLED));
    lv_btnmatrix_set_btn_ctrl(
        candidate_panel,
        LV_IME_PINYIN_CAND_TEXT_NUM + 1U,
        (lv_btnmatrix_ctrl_t)(LV_BTNMATRIX_CTRL_HIDDEN |
                              LV_BTNMATRIX_CTRL_DISABLED));
}

static void style_button(
    lv_obj_t *button,
    lv_obj_t *label,
    const lv_font_t *font,
    unsigned int background,
    unsigned int pressed_background)
{
    lv_obj_set_style_bg_color(button, lv_color_hex(background), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_hex(0x04324B), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(pressed_background),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(label);
}

static lv_obj_t *get_register_candidate_panel(ClinicRegisterPage *page)
{
    lv_obj_t *candidate_panel;

    if(page == NULL) {
        return NULL;
    }
    if(page->pinyin_ime == NULL || !lv_obj_is_valid(page->pinyin_ime)) {
        page->candidate_panel = NULL;
        return NULL;
    }

    candidate_panel = lv_ime_pinyin_get_cand_panel(page->pinyin_ime);
    if(candidate_panel == NULL || !lv_obj_is_valid(candidate_panel)) {
        page->candidate_panel = NULL;
        return NULL;
    }

    page->candidate_panel = candidate_panel;
    return candidate_panel;
}

static void reset_register_pinyin_composition(ClinicRegisterPage *page)
{
    lv_ime_pinyin_t *pinyin_ime;

    if(page == NULL || page->pinyin_ime == NULL ||
       !lv_obj_is_valid(page->pinyin_ime)) {
        return;
    }

    pinyin_ime = (lv_ime_pinyin_t *)page->pinyin_ime;
    clinic_pinyin_reset_composition(pinyin_ime);
}

static int register_pinyin_buffer_would_overflow(
    ClinicRegisterPage *page,
    const char *text)
{
    lv_ime_pinyin_t *pinyin_ime;
    size_t input_length = 0U;

    if(page == NULL || text == NULL || text[0] == '\0' ||
       text[1] != '\0' ||
       !((text[0] >= 'a' && text[0] <= 'z') ||
         (text[0] >= 'A' && text[0] <= 'Z')) ||
       page->pinyin_ime == NULL || !lv_obj_is_valid(page->pinyin_ime)) {
        return 0;
    }

    pinyin_ime = (lv_ime_pinyin_t *)page->pinyin_ime;
    if(pinyin_ime->mode != LV_IME_PINYIN_MODE_K26) {
        return 0;
    }

    while(input_length < sizeof(pinyin_ime->input_char) &&
          pinyin_ime->input_char[input_length] != '\0') {
        ++input_length;
    }
    return input_length >= sizeof(pinyin_ime->input_char) ||
        input_length + strlen(text) >= sizeof(pinyin_ime->input_char);
}

static void register_pinyin_keyboard_event_cb(lv_event_t *event)
{
    ClinicRegisterPage *page = lv_event_get_user_data(event);
    lv_obj_t *keyboard = lv_event_get_target(event);
    const char *text = NULL;
    int overflow = 0;

    if(page != NULL && keyboard == page->username_keyboard &&
       lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        uint16_t button = lv_btnmatrix_get_selected_btn(keyboard);

        if(button != LV_BTNMATRIX_BTN_NONE) {
            text = lv_btnmatrix_get_btn_text(keyboard, button);
            overflow = register_pinyin_buffer_would_overflow(page, text);
            if(overflow) {
                reset_register_pinyin_composition(page);
                if(get_register_candidate_panel(page) != NULL) {
                    lv_obj_add_flag(page->candidate_panel, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    /* The stock callback is removed when this guard is installed. */
    lv_keyboard_def_event_cb(event);
    if(overflow) {
        /* Keep the typed character but skip the unsafe IME append. */
        lv_event_stop_processing(event);
    }
}

static void register_pinyin_candidate_guard_event_cb(lv_event_t *event)
{
    ClinicRegisterPage *page = lv_event_get_user_data(event);
    lv_obj_t *keyboard = lv_event_get_target(event);
    lv_ime_pinyin_t *pinyin_ime;

    if(page == NULL || keyboard != page->username_keyboard ||
       lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED ||
       page->pinyin_ime == NULL || !lv_obj_is_valid(page->pinyin_ime)) {
        return;
    }

    pinyin_ime = (lv_ime_pinyin_t *)page->pinyin_ime;
    if(clinic_pinyin_discard_invalid_candidates(pinyin_ime) &&
       get_register_candidate_panel(page) != NULL) {
        lv_obj_add_flag(page->candidate_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_input_panel(ClinicRegisterPage *page)
{
    lv_obj_t *candidate_panel;

    if(page == NULL) {
        return;
    }
    if(page->username_keyboard != NULL &&
       lv_obj_is_valid(page->username_keyboard)) {
        lv_obj_add_flag(page->username_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    if(page->password_keyboard != NULL &&
       lv_obj_is_valid(page->password_keyboard)) {
        lv_keyboard_set_textarea(page->password_keyboard, NULL);
        lv_obj_add_flag(page->password_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    candidate_panel = get_register_candidate_panel(page);
    if(candidate_panel != NULL) {
        lv_obj_add_flag(candidate_panel, LV_OBJ_FLAG_HIDDEN);
    }
    reset_register_pinyin_composition(page);
    if(page->username != NULL && lv_obj_is_valid(page->username)) {
        lv_obj_clear_state(page->username, LV_STATE_FOCUSED);
    }
    if(page->password != NULL && lv_obj_is_valid(page->password)) {
        lv_obj_clear_state(page->password, LV_STATE_FOCUSED);
    }
    if(page->password_confirmation != NULL &&
       lv_obj_is_valid(page->password_confirmation)) {
        lv_obj_clear_state(page->password_confirmation, LV_STATE_FOCUSED);
    }
    page->active_input = NULL;
}

static void detach_register_input_bindings(ClinicRegisterPage *page)
{
    lv_obj_t *ime_keyboard = NULL;

    if(page == NULL) {
        return;
    }

    hide_input_panel(page);
    if(page->pinyin_ime != NULL && lv_obj_is_valid(page->pinyin_ime)) {
        ime_keyboard = lv_ime_pinyin_get_kb(page->pinyin_ime);
        if(ime_keyboard != NULL && lv_obj_is_valid(ime_keyboard)) {
            lv_keyboard_set_textarea(ime_keyboard, NULL);
        }
    }
    if(page->username_keyboard != NULL &&
       lv_obj_is_valid(page->username_keyboard)) {
        lv_keyboard_set_textarea(page->username_keyboard, NULL);
    }
    if(page->password_keyboard != NULL &&
       lv_obj_is_valid(page->password_keyboard)) {
        lv_keyboard_set_textarea(page->password_keyboard, NULL);
    }
}

static void show_keyboard_cb(lv_event_t *event)
{
    ClinicRegisterPage *page = lv_event_get_user_data(event);
    lv_obj_t *textarea = lv_event_get_target(event);
    lv_obj_t *candidate_panel;

    if(page == NULL || !clinic_register_page_is_active(page) ||
       textarea == NULL || !lv_obj_is_valid(textarea) ||
       (textarea != page->username && textarea != page->password &&
        textarea != page->password_confirmation)) {
        return;
    }

    hide_input_panel(page);
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    lv_textarea_set_cursor_pos(textarea, LV_TEXTAREA_CURSOR_LAST);
    page->active_input = textarea;

    if(textarea == page->username && page->username_keyboard != NULL &&
       lv_obj_is_valid(page->username_keyboard)) {
        lv_keyboard_set_mode(
            page->username_keyboard,
            LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(page->username_keyboard, page->username);
        lv_obj_clear_flag(page->username_keyboard, LV_OBJ_FLAG_HIDDEN);
        candidate_panel = get_register_candidate_panel(page);
        if(candidate_panel != NULL) {
            lv_obj_clear_flag(candidate_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else if(page->password_keyboard != NULL &&
            lv_obj_is_valid(page->password_keyboard)) {
        lv_keyboard_set_mode(
            page->password_keyboard,
            LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(page->password_keyboard, textarea);
        lv_obj_clear_flag(page->password_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void keyboard_event_cb(lv_event_t *event)
{
    ClinicRegisterPage *page = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);

    if(page != NULL && clinic_register_page_is_active(page) &&
       (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)) {
        hide_input_panel(page);
    }
}

static void screen_clicked_cb(lv_event_t *event)
{
    ClinicRegisterPage *page = lv_event_get_user_data(event);

    if(page != NULL && clinic_register_page_is_active(page) &&
       lv_event_get_target(event) == lv_event_get_current_target(event)) {
        hide_input_panel(page);
    }
}

static void username_insert_cb(lv_event_t *event)
{
    ClinicRegisterPage *page = lv_event_get_user_data(event);
    const char *inserted_text = lv_event_get_param(event);
    const char *current_text;
    size_t current_length;
    size_t inserted_length;

    if(page == NULL || !clinic_register_page_is_active(page) ||
       page->username == NULL || !lv_obj_is_valid(page->username) ||
       inserted_text == NULL) {
        return;
    }
    current_text = lv_textarea_get_text(page->username);
    current_length = strlen(current_text);
    inserted_length = strlen(inserted_text);
    if(current_length > CLINIC_USERNAME_MAX_LENGTH ||
       inserted_length > CLINIC_USERNAME_MAX_LENGTH - current_length) {
        lv_textarea_set_insert_replace(page->username, "");
        clinic_register_page_show_status(
            page,
            "用户名长度超过服务器限制");
    }
}

/* 主线程本地校验通过后，把三段文本交给上层；这里不直接连接服务器。 */
static void register_clicked_cb(lv_event_t *event)
{
    ClinicRegisterPage *page;
    void *user_data;
    const char *username;
    const char *password;
    const char *confirmation;

    user_data = lv_event_get_user_data(event);
    page = user_data;
    if(page == NULL || !clinic_register_page_is_active(page) || page->busy ||
       page->submit_callback == NULL) {
        return;
    }
    hide_input_panel(page);
    username = lv_textarea_get_text(page->username);
    password = lv_textarea_get_text(page->password);
    confirmation = lv_textarea_get_text(page->password_confirmation);

    if(username[0] == '\0') {
        clinic_register_page_show_status(page, "请输入用户名");
        return;
    }
    if(password[0] == '\0') {
        clinic_register_page_show_status(page, "请输入密码");
        return;
    }
    if(confirmation[0] == '\0') {
        clinic_register_page_show_status(page, "请确认密码");
        return;
    }
    if(strchr(username, '\n') != NULL || strchr(username, '\r') != NULL) {
        clinic_register_page_show_status(page, "用户名不能包含换行");
        return;
    }
    if(strlen(username) > CLINIC_USERNAME_MAX_LENGTH ||
       strlen(password) > CLINIC_PASSWORD_MAX_LENGTH) {
        clinic_register_page_show_status(page, "输入内容过长");
        return;
    }
    if(strcmp(password, confirmation) != 0) {
        clinic_register_page_show_status(page, "两次输入的密码不一致");
        return;
    }

    page->submit_callback(username, password, page->callback_user_data);
}

static void back_clicked_cb(lv_event_t *event)
{
    ClinicRegisterPage *page = lv_event_get_user_data(event);

    if(page == NULL || !clinic_register_page_is_active(page) || page->busy ||
       page->back_callback == NULL) {
        return;
    }
    hide_input_panel(page);
    page->back_callback(page->callback_user_data);
}

/* 创建用户名、密码、确认密码、两套键盘和一个拼音 IME，并绑定候选保护回调。 */
int clinic_register_page_create(
    ClinicRegisterPage *page,
    lv_obj_t *screen,
    const lv_font_t *font,
    ClinicRegisterSubmitCallback submit_callback,
    ClinicRegisterBackCallback back_callback,
    void *callback_user_data)
{
    lv_obj_t *title;
    lv_obj_t *username_label;
    lv_obj_t *password_label;
    lv_obj_t *confirmation_label;
    lv_obj_t *register_label;
    lv_obj_t *back_label;

    if(page == NULL || screen == NULL || font == NULL ||
       submit_callback == NULL || back_callback == NULL) {
        return -1;
    }

    memset(page, 0, sizeof(*page));
    page->screen = screen;
    page->font = font;
    page->submit_callback = submit_callback;
    page->back_callback = back_callback;
    page->callback_user_data = callback_user_data;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0xEAF3EF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);

    title = lv_label_create(screen);
    username_label = lv_label_create(screen);
    password_label = lv_label_create(screen);
    confirmation_label = lv_label_create(screen);
    page->username = lv_textarea_create(screen);
    page->password = lv_textarea_create(screen);
    page->password_confirmation = lv_textarea_create(screen);
    page->register_button = lv_btn_create(screen);
    register_label = lv_label_create(page->register_button);
    page->back_button = lv_btn_create(screen);
    back_label = lv_label_create(page->back_button);
    page->status_label = lv_label_create(screen);
    page->pinyin_ime = lv_ime_pinyin_create(screen);
    page->username_keyboard = lv_keyboard_create(screen);
    page->password_keyboard = lv_keyboard_create(screen);
    page->candidate_panel = NULL;
    if(page->pinyin_ime != NULL) {
        page->candidate_panel = lv_ime_pinyin_get_cand_panel(page->pinyin_ime);
    }

    if(title == NULL || username_label == NULL || password_label == NULL ||
       confirmation_label == NULL || page->username == NULL ||
       page->password == NULL || page->password_confirmation == NULL ||
       page->register_button == NULL || register_label == NULL ||
       page->back_button == NULL || back_label == NULL ||
       page->status_label == NULL || page->pinyin_ime == NULL ||
       page->username_keyboard == NULL || page->password_keyboard == NULL ||
       page->candidate_panel == NULL) {
        fprintf(stderr, "failed to create register page widgets\n");
        clinic_register_page_cleanup(page);
        return -1;
    }
    page->register_button_label = register_label;

    lv_label_set_text(title, "用户注册");
    lv_obj_set_style_text_font(title, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0D5A4C), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    lv_label_set_text(username_label, "用户名");
    lv_label_set_text(password_label, "密码");
    lv_label_set_text(confirmation_label, "确认密码");
    lv_obj_set_style_text_font(username_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_font(password_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_font(confirmation_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(username_label, lv_color_hex(0x173C35),
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(password_label, lv_color_hex(0x173C35),
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(confirmation_label, lv_color_hex(0x173C35),
                                LV_PART_MAIN);
    lv_obj_set_pos(username_label, 34, 45);
    lv_obj_set_pos(password_label, 34, 94);
    lv_obj_set_pos(confirmation_label, 34, 143);

    lv_textarea_set_one_line(page->username, true);
    lv_textarea_set_max_length(page->username, CLINIC_USERNAME_MAX_LENGTH);
    lv_textarea_set_placeholder_text(page->username, "请输入用户名");
    lv_obj_set_pos(page->username, 170, 35);
    lv_obj_set_size(page->username, 590, 44);
    style_textarea(page->username, font);

    lv_textarea_set_one_line(page->password, true);
    lv_textarea_set_max_length(page->password, CLINIC_PASSWORD_MAX_LENGTH);
    lv_textarea_set_password_mode(page->password, true);
    lv_textarea_set_placeholder_text(page->password, "请输入密码");
    lv_obj_set_pos(page->password, 170, 84);
    lv_obj_set_size(page->password, 590, 44);
    style_textarea(page->password, font);

    lv_textarea_set_one_line(page->password_confirmation, true);
    lv_textarea_set_max_length(
        page->password_confirmation,
        CLINIC_PASSWORD_MAX_LENGTH);
    lv_textarea_set_password_mode(page->password_confirmation, true);
    lv_textarea_set_placeholder_text(page->password_confirmation, "请再次输入密码");
    lv_obj_set_pos(page->password_confirmation, 170, 133);
    lv_obj_set_size(page->password_confirmation, 590, 44);
    style_textarea(page->password_confirmation, font);

    lv_obj_set_size(page->register_button, 180, 44);
    lv_obj_align(page->register_button, LV_ALIGN_TOP_MID, -110, 184);
    lv_label_set_text(register_label, "注册");
    style_button(page->register_button, register_label, font, 0x0B5D8C,
                 0x073E5D);

    lv_obj_set_size(page->back_button, 180, 44);
    lv_obj_align(page->back_button, LV_ALIGN_TOP_MID, 110, 184);
    lv_label_set_text(back_label, "返回登录");
    style_button(page->back_button, back_label, font, 0x53736A, 0x36544C);

    lv_obj_set_width(page->status_label, 720);
    lv_obj_set_height(page->status_label, 42);
    lv_obj_align(page->status_label, LV_ALIGN_TOP_MID, 0, 232);
    lv_label_set_long_mode(page->status_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(page->status_label, "");
    lv_obj_set_style_text_font(page->status_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(page->status_label, lv_color_hex(0xA23A2B),
                                LV_PART_MAIN);
    lv_obj_set_style_text_align(page->status_label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);

    lv_obj_clear_flag(page->pinyin_ime, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(page->pinyin_ime, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(page->pinyin_ime, 0, 0);
    lv_obj_set_style_text_font(page->pinyin_ime, font, LV_PART_MAIN);

    lv_obj_set_size(page->username_keyboard, REGISTER_DISPLAY_WIDTH,
                    REGISTER_KEYBOARD_HEIGHT);
    lv_obj_align(page->username_keyboard, LV_ALIGN_TOP_MID, 0, 280);
    style_keyboard(page->username_keyboard);
    lv_keyboard_set_mode(page->username_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(page->username_keyboard, NULL);
    lv_obj_add_flag(page->username_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_event_cb(page->username_keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(page->username_keyboard,
                        register_pinyin_keyboard_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        page);

    lv_obj_set_size(page->password_keyboard, REGISTER_DISPLAY_WIDTH,
                    REGISTER_KEYBOARD_HEIGHT);
    lv_obj_align(page->password_keyboard, LV_ALIGN_TOP_MID, 0, 280);
    style_keyboard(page->password_keyboard);
    lv_keyboard_set_mode(page->password_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(page->password_keyboard, NULL);
    lv_obj_add_flag(page->password_keyboard, LV_OBJ_FLAG_HIDDEN);

    lv_ime_pinyin_set_keyboard(page->pinyin_ime, page->username_keyboard);
    lv_obj_add_event_cb(page->username_keyboard,
                        register_pinyin_candidate_guard_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        page);
    lv_ime_pinyin_set_mode(page->pinyin_ime, LV_IME_PINYIN_MODE_K26);
    lv_obj_set_size(page->candidate_panel, REGISTER_DISPLAY_WIDTH, 40);
    lv_obj_align_to(page->candidate_panel, page->username_keyboard,
                    LV_ALIGN_OUT_TOP_MID, 0, 0);
    style_candidate_panel(page->candidate_panel, font);
    lv_obj_add_flag(page->candidate_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(page->username, username_insert_cb,
                        LV_EVENT_INSERT, page);
    lv_obj_add_event_cb(page->username, show_keyboard_cb, LV_EVENT_CLICKED, page);
    lv_obj_add_event_cb(page->password, show_keyboard_cb, LV_EVENT_CLICKED, page);
    lv_obj_add_event_cb(page->password_confirmation, show_keyboard_cb,
                        LV_EVENT_CLICKED, page);
    lv_obj_add_event_cb(page->username_keyboard, keyboard_event_cb,
                        LV_EVENT_READY, page);
    lv_obj_add_event_cb(page->username_keyboard, keyboard_event_cb,
                        LV_EVENT_CANCEL, page);
    lv_obj_add_event_cb(page->password_keyboard, keyboard_event_cb,
                        LV_EVENT_READY, page);
    lv_obj_add_event_cb(page->password_keyboard, keyboard_event_cb,
                        LV_EVENT_CANCEL, page);
    lv_obj_add_event_cb(page->register_button, register_clicked_cb,
                        LV_EVENT_CLICKED, page);
    lv_obj_add_event_cb(page->back_button, back_clicked_cb,
                        LV_EVENT_CLICKED, page);
    lv_obj_add_event_cb(screen, screen_clicked_cb, LV_EVENT_CLICKED, page);
    return 0;
}

/* 请求期间禁止提交和返回，保证当前 screen 在认证 worker 完成前保持有效。 */
void clinic_register_page_set_busy(ClinicRegisterPage *page, int busy)
{
    if(page == NULL || !clinic_register_page_is_active(page) ||
       page->register_button == NULL || page->back_button == NULL ||
       page->register_button_label == NULL) {
        return;
    }
    page->busy = busy != 0;
    if(page->busy) {
        lv_obj_add_state(page->register_button, LV_STATE_DISABLED);
        lv_obj_add_state(page->back_button, LV_STATE_DISABLED);
        lv_label_set_text(page->register_button_label, "正在注册...");
        clinic_register_page_show_status(page, "正在注册，请稍候");
    }
    else {
        lv_obj_clear_state(page->register_button, LV_STATE_DISABLED);
        lv_obj_clear_state(page->back_button, LV_STATE_DISABLED);
        lv_label_set_text(page->register_button_label, "注册");
    }
}

void clinic_register_page_show_status(
    ClinicRegisterPage *page,
    const char *message)
{
    if(page == NULL || !clinic_register_page_is_active(page) ||
       page->status_label == NULL || !lv_obj_is_valid(page->status_label) ||
       message == NULL) {
        return;
    }
    lv_label_set_text(page->status_label, message);
}

/* 先解除 textarea/keyboard/IME 关系，再清空指针；实际 screen 由 main.c 删除。 */
void clinic_register_page_cleanup(ClinicRegisterPage *page)
{
    if(page == NULL) {
        return;
    }
    detach_register_input_bindings(page);
    if(page->password != NULL && lv_obj_is_valid(page->password)) {
        lv_textarea_set_text(page->password, "");
    }
    if(page->password_confirmation != NULL &&
       lv_obj_is_valid(page->password_confirmation)) {
        lv_textarea_set_text(page->password_confirmation, "");
    }
    memset(page, 0, sizeof(*page));
}

int clinic_register_page_is_active(const ClinicRegisterPage *page)
{
    return page != NULL && page->screen != NULL &&
        lv_obj_is_valid(page->screen);
}
