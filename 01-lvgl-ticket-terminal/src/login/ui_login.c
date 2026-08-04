/**
 * @file ui_login.c
 * @brief GEC6818 登录界面模块
 */
#include "ui_login.h"
#include "file_user.h"
#include "src/common/ui_keyboard.h"
#include <stdio.h>
#include <string.h>
#include "ui_main.h"

LV_IMG_DECLARE(img_login_bg_classic);
LV_IMG_DECLARE(img_login_bg_vintage);
LV_IMG_DECLARE(img_product_01);
LV_IMG_DECLARE(img_product_02);
LV_IMG_DECLARE(img_product_03);
LV_IMG_DECLARE(img_product_04);
LV_IMG_DECLARE(img_product_05);
LV_IMG_DECLARE(img_product_06);
LV_IMG_DECLARE(img_product_07);
LV_IMG_DECLARE(img_product_08);
LV_IMG_DECLARE(img_wechat_qr);
LV_IMG_DECLARE(img_alipay_qr);

#define LV_FONT_KAI "/font/simkai.ttf"



static void login_blank_click_cb(lv_event_t *e);
static lv_obj_t *login_screen    = NULL;
static lv_obj_t *bg_image        = NULL;
static lv_obj_t *ta_username     = NULL;
static lv_obj_t *ta_password     = NULL;
static lv_obj_t *cb_remember     = NULL;
static lv_obj_t *cb_vintage      = NULL;
static lv_obj_t *register_dialog = NULL;
static lv_obj_t *ta_reg_name     = NULL;
static lv_obj_t *ta_reg_pwd      = NULL;
static lv_obj_t *msg_box         = NULL;
static lv_obj_t *keyboard_move_obj = NULL;
static lv_coord_t keyboard_move_origin_y = 0;
static int keyboard_move_saved = 0;
static int current_theme = 0;


static void set_ft(lv_obj_t *obj, int size)
{
    /* 每种字号独立的 static 字体和样式，只创建一次 */
    static struct { int sz; lv_ft_info_t i; lv_font_t *f; lv_style_t s; int ok; } _c[6];
    int s = -1, e = -1;
    for (int j = 0; j < 6; j++) { if (_c[j].ok && _c[j].sz == size) { s = j; break; } if (e < 0 && !_c[j].ok) e = j; }
    if (s < 0) s = e;
    if (s < 0) return;
    if (!_c[s].ok) {
        _c[s].sz = size;
        _c[s].i.name = LV_FONT_KAI; _c[s].i.weight = size;
        _c[s].i.style = FT_FONT_STYLE_BOLD; _c[s].i.mem = NULL;
        if (!lv_ft_font_init(&_c[s].i)) return;
        _c[s].f = _c[s].i.font;
        lv_style_init(&_c[s].s);
        lv_style_set_text_font(&_c[s].s, _c[s].f);
        _c[s].ok = 1;
    }
    lv_obj_add_style(obj, &_c[s].s, 0);
}
static void set_freetype_font(lv_obj_t *obj, const char *fontPath, int fontSize)
{
    static lv_ft_info_t info;
    info.name   = fontPath;
    info.weight = fontSize;
    info.style  = FT_FONT_STYLE_BOLD;
    info.mem    = NULL;
    if (!lv_ft_font_init(&info)) {
        LV_LOG_ERROR("ui_login: FreeType font init failed: %s", fontPath);
        return;
    }
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_font(&style, info.font);
    lv_obj_add_style(obj, &style, 0);
}

static void msg_box_close_cb(lv_event_t *e) { (void)e; msg_box = NULL; }

static void restore_keyboard_move_obj(void)
{
    if (keyboard_move_saved && keyboard_move_obj != NULL) {
        lv_obj_set_y(keyboard_move_obj, keyboard_move_origin_y);
    }

    keyboard_move_obj = NULL;
    keyboard_move_origin_y = 0;
    keyboard_move_saved = 0;
}

static void login_hide_keyboard_and_restore(void)
{
    hide_input_keyboard();
    restore_keyboard_move_obj();
}

static void keyboard_avoid_input(lv_obj_t *ta)
{
    if (ta == NULL) {
        return;
    }

    lv_obj_t *move_obj = lv_obj_get_parent(ta);
    if (move_obj == NULL) {
        return;
    }

    if (keyboard_move_saved && keyboard_move_obj != NULL && keyboard_move_obj != move_obj) {
        lv_obj_set_y(keyboard_move_obj, keyboard_move_origin_y);
        keyboard_move_saved = 0;
        keyboard_move_obj = NULL;
    }

    if (!keyboard_move_saved) {
        keyboard_move_obj = move_obj;
        keyboard_move_origin_y = lv_obj_get_y(move_obj);
        keyboard_move_saved = 1;
    }

    lv_obj_set_y(move_obj, keyboard_move_origin_y);
    lv_obj_update_layout(lv_scr_act());

    lv_area_t ta_area;
    lv_obj_get_coords(ta, &ta_area);

    const lv_coord_t keyboard_top = 256;
    const lv_coord_t safe_bottom = keyboard_top - 16;

    if (ta_area.y2 <= safe_bottom) {
        return;
    }

    lv_coord_t new_y = keyboard_move_origin_y - (ta_area.y2 - safe_bottom);
    if (new_y < -180) {
        new_y = -180;
    }

    lv_obj_set_y(move_obj, new_y);
}

static void login_ta_focus_cb(lv_event_t *e)
{
    ta_focus_cb(e);
    keyboard_avoid_input(lv_event_get_target(e));
}

static void show_msg_box(const char *title, const char *msg)
{
    if (msg_box != NULL) {
        lv_msgbox_close(msg_box);
        msg_box = NULL;
    }
    msg_box = lv_msgbox_create(NULL, title, msg, NULL, true);
    lv_obj_add_event_cb(msg_box, msg_box_close_cb, LV_EVENT_DELETE, NULL);
    lv_obj_set_size(msg_box, 420, 200);
    lv_obj_center(msg_box);
    set_freetype_font(lv_msgbox_get_title(msg_box), LV_FONT_KAI, 24);
    set_freetype_font(lv_msgbox_get_text(msg_box), LV_FONT_KAI, 20);
    lv_obj_t *close_btn = lv_msgbox_get_close_btn(msg_box);
    if (close_btn != NULL) set_freetype_font(close_btn, LV_FONT_KAI, 18);
}

static void close_register_dialog_async(void)
{
    login_hide_keyboard_and_restore();
    destroy_input_keyboard();

    if (register_dialog != NULL) {
        lv_obj_t *dlg = register_dialog;
        register_dialog = NULL;
        ta_reg_name = NULL;
        ta_reg_pwd  = NULL;
        lv_obj_del_async(dlg);
    }
}

static void login_blank_click_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    lv_obj_t *current = lv_event_get_current_target(e);

    /*
     * 只有真正点击空白区域时才收起。
     * 如果点击的是输入框、按钮等子控件，不处理。
     */
    if (target == current)
    {
        login_hide_keyboard_and_restore();
    }
}

static void theme_toggle_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        if (lv_obj_has_state(cb_vintage, LV_STATE_CHECKED)) {
            current_theme = 1;
            lv_img_set_src(bg_image, &img_login_bg_vintage);
        } else {
            current_theme = 0;
            lv_img_set_src(bg_image, &img_login_bg_classic);
        }
    }
}

static void login_btn_cb(lv_event_t *e)
{
    (void)e;
    const char *username = lv_textarea_get_text(ta_username);
    const char *password = lv_textarea_get_text(ta_password);
    if (username[0] == '\0' || password[0] == '\0') {
        login_hide_keyboard_and_restore();
        show_msg_box("提示", "用户名和密码不能为空!");
        return;
    }
    int ret = file_user_login_check(username, password);
    if (ret == 1) {
        login_hide_keyboard_and_restore();
        destroy_input_keyboard();

        if (lv_obj_has_state(cb_remember, LV_STATE_CHECKED)) {
            file_pwd_cache_write(username, password);
        } else {
            file_pwd_cache_clear();
        }

        lv_obj_clean(lv_scr_act());
        ui_main_create();
    } else if (ret == 0) {
        login_hide_keyboard_and_restore();
        show_msg_box("失败", "密码错误, 请重新输入!");
    } else if (ret == -1) {
        login_hide_keyboard_and_restore();
        show_msg_box("失败", "用户不存在, 请先注册!");
    } else {
        login_hide_keyboard_and_restore();
        show_msg_box("错误", "系统错误, 请稍后重试!");
    }
}

static void register_confirm_cb(lv_event_t *e)
{
    (void)e;
    const char *name = lv_textarea_get_text(ta_reg_name);
    const char *pwd  = lv_textarea_get_text(ta_reg_pwd);
    if (name[0] == '\0' || pwd[0] == '\0') {
        login_hide_keyboard_and_restore();
        show_msg_box("提示", "账号和密码不能为空!");
        return;
    }
    if (strchr(name, '@') != NULL || strchr(pwd, '@') != NULL) {
        login_hide_keyboard_and_restore();
        show_msg_box("提示", "账号和密码不能包含@符号!");
        return;
    }
    if (strlen(name) >= USER_NAME_MAX || strlen(pwd) >= USER_PWD_MAX) {
        login_hide_keyboard_and_restore();
        show_msg_box("提示", "账号或密码过长!");
        return;
    }
    int ret = file_user_register(name, pwd);
    if (ret == 0) {
        show_msg_box("成功", "注册成功! 请登录.");
        close_register_dialog_async();
    } else if (ret == -2) {
        login_hide_keyboard_and_restore();
        show_msg_box("失败", "用户名已存在, 请更换!");
    } else {
        login_hide_keyboard_and_restore();
        show_msg_box("错误", "注册失败, 请稍后重试!");
    }
}

static void register_cancel_cb(lv_event_t *e)
{
    (void)e;
    close_register_dialog_async();
}

static void show_register_dialog(void)
{
    if (register_dialog != NULL) {
        lv_obj_del(register_dialog);
        register_dialog = NULL;
    }
    register_dialog = lv_obj_create(lv_scr_act());
    lv_obj_set_size(register_dialog, 480, 340);
    lv_obj_set_pos(register_dialog, 160, 60);    
    lv_obj_set_style_bg_color(register_dialog, lv_color_hex(0x2C2C3A), 0);
    lv_obj_set_style_border_width(register_dialog, 2, 0);
    lv_obj_set_style_border_color(register_dialog, lv_color_hex(0x5A7FFF), 0);
    lv_obj_set_style_radius(register_dialog, 10, 0);
    lv_obj_clear_flag(register_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title_label = lv_label_create(register_dialog);
    set_freetype_font(title_label, LV_FONT_KAI, 28);
    lv_label_set_text(title_label, "用户注册");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_t *lbl_name = lv_label_create(register_dialog);
    set_freetype_font(lbl_name, LV_FONT_KAI, 18);
    lv_label_set_text(lbl_name, "账 号:");
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 40, 65);
    ta_reg_name = lv_textarea_create(register_dialog);
    lv_obj_set_size(ta_reg_name, 280, 42);
    lv_obj_align_to(ta_reg_name, lbl_name, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_textarea_set_placeholder_text(ta_reg_name, "请输入账号");
    lv_textarea_set_one_line(ta_reg_name, true);
    lv_textarea_set_max_length(ta_reg_name, USER_NAME_MAX - 1);
    set_freetype_font(ta_reg_name, LV_FONT_KAI, 20);
    lv_obj_add_event_cb(ta_reg_name, login_ta_focus_cb, LV_EVENT_CLICKED, NULL);


    lv_obj_t *lbl_pwd = lv_label_create(register_dialog);
    set_freetype_font(lbl_pwd, LV_FONT_KAI, 18);
    lv_label_set_text(lbl_pwd, "密 码:");
    lv_obj_set_style_text_color(lbl_pwd, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(lbl_pwd, LV_ALIGN_TOP_LEFT, 40, 140);
    ta_reg_pwd = lv_textarea_create(register_dialog);
    lv_obj_set_size(ta_reg_pwd, 280, 42);
    lv_obj_align_to(ta_reg_pwd, lbl_pwd, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_textarea_set_placeholder_text(ta_reg_pwd, "请输入密码");
    lv_textarea_set_one_line(ta_reg_pwd, true);
    lv_textarea_set_max_length(ta_reg_pwd, USER_PWD_MAX - 1);
    lv_textarea_set_password_mode(ta_reg_pwd, true);
    set_freetype_font(ta_reg_pwd, LV_FONT_KAI, 20);
    lv_obj_add_event_cb(ta_reg_pwd, login_ta_focus_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_confirm = lv_btn_create(register_dialog);
    lv_obj_set_size(btn_confirm, 130, 48);
    lv_obj_align(btn_confirm, LV_ALIGN_BOTTOM_LEFT, 80, -35);
    lv_obj_set_style_bg_color(btn_confirm, lv_color_hex(0x5A7FFF), 0);
    lv_obj_set_style_radius(btn_confirm, 8, 0);
    lv_obj_t *lbl_cf = lv_label_create(btn_confirm);
    set_freetype_font(lbl_cf, LV_FONT_KAI, 22);
    lv_label_set_text(lbl_cf, "确认");
    lv_obj_center(lbl_cf);
    lv_obj_add_event_cb(btn_confirm, register_confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_cancel = lv_btn_create(register_dialog);
    lv_obj_set_size(btn_cancel, 130, 48);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -80, -35);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x666666), 0);
    lv_obj_set_style_radius(btn_cancel, 8, 0);
    lv_obj_t *lbl_cl = lv_label_create(btn_cancel);
    set_freetype_font(lbl_cl, LV_FONT_KAI, 22);
    lv_label_set_text(lbl_cl, "取消");
    lv_obj_center(lbl_cl);
    lv_obj_add_event_cb(btn_cancel, register_cancel_cb, LV_EVENT_CLICKED, NULL);
}

static void register_btn_cb(lv_event_t *e)
{
    (void)e;
    show_register_dialog();
}

void ui_login_create(void)
{
    login_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(login_screen, 800, 480);
    lv_obj_set_pos(login_screen, 0, 0);
    lv_obj_clear_flag(login_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(login_screen, 0, 0);
    lv_obj_set_style_pad_all(login_screen, 0, 0);
    lv_obj_set_style_bg_opa(login_screen, LV_OPA_TRANSP, 0);

    lv_obj_add_flag(login_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(login_screen, login_blank_click_cb, LV_EVENT_CLICKED, NULL);

    bg_image = lv_img_create(login_screen);
    lv_img_set_src(bg_image, &img_login_bg_classic);
    lv_img_set_zoom(bg_image, 512);
    lv_obj_set_pos(bg_image, 0, 0);
    lv_obj_set_size(bg_image, 800, 480);

    lv_obj_t *panel = lv_obj_create(login_screen);
    lv_obj_set_size(panel, 580, 440);
    lv_obj_set_pos(panel, 110, 15);    
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_50, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x5A7FFF), 0);
    lv_obj_set_style_radius(panel, 14, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    set_freetype_font(title, LV_FONT_KAI, 32);
    lv_label_set_text(title, "景区门票自助售票终端");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_width(title, 400);
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t *lbl_user = lv_label_create(panel);
    set_freetype_font(lbl_user, LV_FONT_KAI, 20);
    lv_label_set_text(lbl_user, "用户名:");
    lv_obj_set_style_text_color(lbl_user, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(lbl_user, LV_ALIGN_TOP_LEFT, 60, 80);

    ta_username = lv_textarea_create(panel);
    lv_obj_set_size(ta_username, 320, 44);
    lv_obj_align_to(ta_username, lbl_user, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_textarea_set_placeholder_text(ta_username, "请输入用户名");
    lv_textarea_set_one_line(ta_username, true);
    lv_textarea_set_max_length(ta_username, USER_NAME_MAX - 1);
    set_freetype_font(ta_username, LV_FONT_KAI, 22);
    lv_obj_add_event_cb(ta_username, login_ta_focus_cb, LV_EVENT_CLICKED, NULL);
    

    lv_obj_t *lbl_pwd = lv_label_create(panel);
    set_freetype_font(lbl_pwd, LV_FONT_KAI, 20);
    lv_label_set_text(lbl_pwd, "密    码:");
    lv_obj_set_style_text_color(lbl_pwd, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(lbl_pwd, LV_ALIGN_TOP_LEFT, 60, 175);

    ta_password = lv_textarea_create(panel);
    lv_obj_set_size(ta_password, 320, 44);
    lv_obj_align_to(ta_password, lbl_pwd, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_textarea_set_placeholder_text(ta_password, "请输入密码");
    lv_textarea_set_one_line(ta_password, true);
    lv_textarea_set_max_length(ta_password, USER_PWD_MAX - 1);
    lv_textarea_set_password_mode(ta_password, true);
    set_freetype_font(ta_password, LV_FONT_KAI, 22);
    lv_obj_add_event_cb(ta_password, login_ta_focus_cb, LV_EVENT_CLICKED, NULL);

    cb_remember = lv_checkbox_create(panel);
    lv_obj_align(cb_remember, LV_ALIGN_TOP_LEFT, 50, 255);
    lv_obj_set_width(cb_remember, 140);
    lv_obj_set_height(cb_remember, 30);
    lv_checkbox_set_text(cb_remember, "记住密码");
    set_freetype_font(cb_remember, LV_FONT_KAI, 14);
    lv_obj_set_style_text_color(cb_remember, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_pad_column(cb_remember, 10, 0);
    lv_obj_set_style_pad_top(cb_remember, 4, 0);

    cb_vintage = lv_checkbox_create(panel);
    lv_obj_set_width(cb_vintage, 140);
    lv_obj_set_height(cb_vintage, 30);
    lv_obj_align_to(cb_vintage, cb_remember, LV_ALIGN_OUT_RIGHT_MID, 100, 0);
    lv_checkbox_set_text(cb_vintage, "复古主题");
    set_freetype_font(cb_vintage, LV_FONT_KAI, 14);
    lv_obj_set_style_text_color(cb_vintage, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_pad_column(cb_vintage, 10, 0);
    lv_obj_set_style_pad_top(cb_vintage, 4, 0);
    lv_obj_add_event_cb(cb_vintage, theme_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *sep = lv_obj_create(panel);
    lv_obj_set_size(sep, 460, 1);
    lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 345);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x5A7FFF), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_50, 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    lv_obj_t *btn_login = lv_btn_create(panel);
    lv_obj_set_size(btn_login, 130, 42);
    lv_obj_align(btn_login, LV_ALIGN_BOTTOM_LEFT, 70, -35);
    lv_obj_set_style_bg_color(btn_login, lv_color_hex(0x4A90D9), 0);
    lv_obj_set_style_radius(btn_login, 10, 0);
    lv_obj_set_style_shadow_width(btn_login, 4, 0);
    lv_obj_set_style_shadow_ofs_y(btn_login, 3, 0);
    lv_obj_t *lbl_login = lv_label_create(btn_login);
    set_freetype_font(lbl_login, LV_FONT_KAI, 26);
    lv_label_set_text(lbl_login, "登  录");
    lv_obj_center(lbl_login);
    lv_obj_add_event_cb(btn_login, login_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_register = lv_btn_create(panel);
    lv_obj_set_size(btn_register, 130, 42);
    lv_obj_align(btn_register, LV_ALIGN_BOTTOM_RIGHT, -70, -35);
    lv_obj_set_style_bg_color(btn_register, lv_color_hex(0xE8913A), 0);
    lv_obj_set_style_radius(btn_register, 10, 0);
    lv_obj_set_style_shadow_width(btn_register, 4, 0);
    lv_obj_set_style_shadow_ofs_y(btn_register, 3, 0);
    lv_obj_t *lbl_register = lv_label_create(btn_register);
    set_freetype_font(lbl_register, LV_FONT_KAI, 26);
    lv_label_set_text(lbl_register, "注  册");
    lv_obj_center(lbl_register);
    lv_obj_add_event_cb(btn_register, register_btn_cb, LV_EVENT_CLICKED, NULL);

    char cached_name[USER_NAME_MAX];
    char cached_pwd[USER_PWD_MAX];
    if (file_pwd_cache_read(cached_name, cached_pwd, sizeof(cached_name)) == 1) {
        lv_textarea_set_text(ta_username, cached_name);
        lv_textarea_set_text(ta_password, cached_pwd);
        lv_obj_add_state(cb_remember, LV_STATE_CHECKED);
    }

    
}
