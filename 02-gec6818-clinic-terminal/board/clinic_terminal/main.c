/*
 * 文件作用（答辩）：GEC6818 正式终端的程序入口和总控制器。
 * 这里初始化 framebuffer、触摸、LVGL、中文字体和各业务页面，并保存登录用户 ID。
 *
 * 关键流程：LVGL 主线程接收触摸事件并复制稳定的请求参数，再创建 pthread
 * 网络工作线程；工作线程调用各业务 client 完成 TCP/JSON 请求，只写结果状态，
 * 绝不直接操作 LVGL。主循环发现线程结束后执行 pthread_join()，再更新控件、
 * 切换 screen 或删除旧页面，从而避免界面阻塞、数据竞争和 use-after-free。
 *
 * 答辩阅读地图：
 * 1. LoginContext 等 RequestContext 保存“线程句柄 + 稳定请求副本 + 返回结果”；
 * 2. 点击回调只校验输入、复制参数并 pthread_create()，不会同步等待网络；
 * 3. *_worker() 只调用业务 client，并把结果写回 Context，绝不调用 LVGL；
 * 4. process_*_result() 在 LVGL 主循环中观察状态、pthread_join()、更新页面；
 * 5. 页面切换前先等待相关 worker，再 cleanup、切换 screen、删除旧 screen。
 *
 * 初学者要点：程序并不是长期固定运行两个线程，而是始终有一个 LVGL 主线程，
 * 每次网络请求再临时创建一个工作线程；请求结束后由主线程 join 并回收该线程。
 */
#define _POSIX_C_SOURCE 200809L

#include "department_client.h"
#include "department_page.h"
#include "doctor_client.h"
#include "doctor_page.h"
#include "credential_store.h"
#include "home_page.h"
#include "login_client.h"
#include "pinyin_guard.h"
#include "queue_page.h"
#include "register_page.h"
#include "ticket_client.h"
#include "ticket_page.h"

#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "display/fbdev.h"
#include "indev/evdev.h"
#include "src/extra/libs/freetype/lv_freetype.h"
#include "src/extra/others/ime/lv_ime_pinyin.h"

#define DISPLAY_WIDTH 800
#define DISPLAY_HEIGHT 480
#define DRAW_BUFFER_ROWS 40
#define FONT_SIZE 28
#define LOOP_DELAY_MS 5
#define LOGIN_TIMEOUT_MS 5000U
#define REGISTER_TIMEOUT_MS 5000U
#define DEPARTMENT_TIMEOUT_MS 5000U
#define DOCTOR_TIMEOUT_MS 5000U
#define TICKET_TIMEOUT_MS 5000U
#define CURRENT_TICKET_TIMEOUT_MS 5000U
#define SERVER_IP_MAX_LENGTH 63U
#define SERVER_PORT_MAX_LENGTH 5U

#define FONT_PATH "/font/simkai.ttf"
#define DEFAULT_SERVER_IP "192.168.10.41"
#define DEFAULT_SERVER_PORT "9000"
#define REMEMBERED_CREDENTIALS_PATH "/IOT/.clinic_terminal_credentials"
#define LOGIN_REQUEST_ID_MAX UINT64_C(9007199254740991)
#define DEPARTMENT_REQUEST_ID_MAX UINT64_C(9007199254740991)
#define DOCTOR_REQUEST_ID_MAX UINT64_C(9007199254740991)
#define TICKET_REQUEST_ID_MAX UINT64_C(9007199254740991)
#define CURRENT_TICKET_REQUEST_ID_MAX UINT64_C(9007199254740991)

/* 登录工作线程的状态机；主线程靠它判断“仍在请求”还是“可以消费结果”。 */
typedef enum LoginState {
    LOGIN_STATE_IDLE = 0,
    LOGIN_STATE_RUNNING,
    LOGIN_STATE_SUCCESS,
    LOGIN_STATE_AUTH_FAILED,
    LOGIN_STATE_NETWORK_ERROR,
    LOGIN_STATE_PROTOCOL_ERROR
} LoginState;

/* 登录和注册共用同一套线程上下文，用 operation 区分本次具体动作。 */
typedef enum AuthOperation {
    AUTH_OPERATION_NONE = 0,
    AUTH_OPERATION_LOGIN,
    AUTH_OPERATION_REGISTER
} AuthOperation;

/*
 * 主线程与认证工作线程共享的稳定上下文。
 * mutex 保护 state/result 等共享字段；server_ip、用户名和密码都是点击时复制的副本，
 * 因此工作线程不会去读取可能已被切页删除的 textarea 控件。
 */
typedef struct LoginContext {
    pthread_mutex_t mutex;
    pthread_t worker;
    LoginState state;
    AuthOperation operation;
    int thread_pending_join;
    char server_ip[SERVER_IP_MAX_LENGTH + 1U];
    char server_port[SERVER_PORT_MAX_LENGTH + 1U];
    char username[CLINIC_USERNAME_MAX_LENGTH + 1U];
    char password[CLINIC_PASSWORD_MAX_LENGTH + 1U];
    uint64_t request_id;
    int64_t authenticated_user_id;
    char error_code[CLINIC_ERROR_CODE_MAX_LENGTH + 1U];
    char result_message[CLINIC_MESSAGE_MAX_LENGTH + 1U];
} LoginContext;

/*
 * 科室、医生、取号和排队查询都采用相同并发模型：
 * IDLE -> RUNNING -> FINISHED -> 主线程 join -> IDLE。
 * 各业务分别保存 Context，是为了避免不同请求互相覆盖线程句柄和返回数据。
 */
typedef enum DepartmentRequestState {
    DEPARTMENT_REQUEST_IDLE = 0,
    DEPARTMENT_REQUEST_RUNNING,
    DEPARTMENT_REQUEST_FINISHED
} DepartmentRequestState;

typedef struct DepartmentRequestContext {
    pthread_mutex_t mutex;
    pthread_t worker;
    DepartmentRequestState state;
    int thread_pending_join;
    char server_ip[SERVER_IP_MAX_LENGTH + 1U];
    char server_port[SERVER_PORT_MAX_LENGTH + 1U];
    uint64_t request_id;
    ClinicServiceFlow flow;
    ClinicDepartmentListResult result;
} DepartmentRequestContext;

typedef struct DepartmentUiController {
    DepartmentRequestContext *request;
    ClinicHomePage *home_page;
} DepartmentUiController;

typedef enum DoctorRequestState {
    DOCTOR_REQUEST_IDLE = 0,
    DOCTOR_REQUEST_RUNNING,
    DOCTOR_REQUEST_FINISHED
} DoctorRequestState;

typedef struct DoctorRequestContext {
    pthread_mutex_t mutex;
    pthread_t worker;
    DoctorRequestState state;
    int thread_pending_join;
    char server_ip[SERVER_IP_MAX_LENGTH + 1U];
    char server_port[SERVER_PORT_MAX_LENGTH + 1U];
    uint64_t request_id;
    int64_t department_id;
    char department_name[CLINIC_DEPARTMENT_NAME_MAX_LENGTH + 1U];
    ClinicDoctorListResult result;
} DoctorRequestContext;

typedef struct DoctorUiController {
    DoctorRequestContext *request;
    ClinicDepartmentPage *department_page;
    int *department_return_requested;
} DoctorUiController;

typedef enum TicketRequestState {
    TICKET_REQUEST_IDLE = 0,
    TICKET_REQUEST_RUNNING,
    TICKET_REQUEST_FINISHED
} TicketRequestState;

typedef struct TicketRequestContext {
    pthread_mutex_t mutex;
    pthread_t worker;
    TicketRequestState state;
    int thread_pending_join;
    char server_ip[SERVER_IP_MAX_LENGTH + 1U];
    char server_port[SERVER_PORT_MAX_LENGTH + 1U];
    uint64_t request_id;
    int64_t user_id;
    int64_t department_id;
    char department_name[CLINIC_DEPARTMENT_NAME_MAX_LENGTH + 1U];
    ClinicTicketCreateResult result;
} TicketRequestContext;

typedef struct TicketUiController {
    TicketRequestContext *request;
    ClinicDepartmentPage *department_page;
    int64_t *authenticated_user_id;
    int *department_return_requested;
} TicketUiController;

typedef enum CurrentTicketRequestState {
    CURRENT_TICKET_REQUEST_IDLE = 0,
    CURRENT_TICKET_REQUEST_RUNNING,
    CURRENT_TICKET_REQUEST_FINISHED
} CurrentTicketRequestState;

typedef enum CurrentTicketRequestSource {
    CURRENT_TICKET_REQUEST_SOURCE_NONE = 0,
    CURRENT_TICKET_REQUEST_SOURCE_HOME,
    CURRENT_TICKET_REQUEST_SOURCE_QUEUE
} CurrentTicketRequestSource;

typedef struct CurrentTicketRequestContext {
    pthread_mutex_t mutex;
    pthread_t worker;
    CurrentTicketRequestState state;
    int thread_pending_join;
    char server_ip[SERVER_IP_MAX_LENGTH + 1U];
    char server_port[SERVER_PORT_MAX_LENGTH + 1U];
    uint64_t request_id;
    int64_t user_id;
    CurrentTicketRequestSource source;
    ClinicCurrentTicketResult result;
} CurrentTicketRequestContext;

typedef struct CurrentTicketUiController {
    CurrentTicketRequestContext *request;
    ClinicHomePage *home_page;
    ClinicQueuePage *queue_page;
    int64_t *authenticated_user_id;
} CurrentTicketUiController;

/*
 * 认证页面状态机只由 LVGL 主线程推进。
 * “网络结果已到达”和“马上切页”分成两个状态，可避免事件回调仍在执行时删除当前页面。
 */
typedef enum AuthUiState {
    AUTH_UI_LOGIN_IDLE = 0,
    AUTH_UI_LOGIN_REQUEST_RUNNING,
    AUTH_UI_LOGIN_RESULT_PENDING,
    AUTH_UI_REGISTER_ENTRY_PENDING,
    AUTH_UI_REGISTER_IDLE,
    AUTH_UI_REGISTER_REQUEST_RUNNING,
    AUTH_UI_REGISTER_RESULT_PENDING,
    AUTH_UI_REGISTER_RETURN_PENDING,
    AUTH_UI_HOME_TRANSITION_PENDING,
    AUTH_UI_HOME_ACTIVE
} AuthUiState;

/*
 * 登录页对象所有权表。所有 lv_obj_t 指针只允许 LVGL 主线程读取和修改；
 * 页面销毁后 clear_login_page_objects() 会把这些指针清空，防止误用旧对象。
 */
typedef struct {
    lv_obj_t *username;
    lv_obj_t *password;
    lv_obj_t *username_keyboard;
    lv_obj_t *password_keyboard;
    lv_obj_t *pinyin_ime;
    lv_obj_t *candidate_panel;
    lv_obj_t *message_box;
    lv_obj_t *active_input;
    lv_obj_t *login_button;
    lv_obj_t *login_button_label;
    lv_obj_t *register_entry_button;
    lv_obj_t *remember_button;
    lv_obj_t *remember_button_label;
    LoginContext *login;
    const lv_font_t *font;
    int64_t pending_home_user_id;
    int home_transition_requested;
    int register_transition_requested;
    int message_box_close_requested;
    int login_page_active;
    AuthUiState auth_state;
} login_ui_t;

typedef struct RegisterUiController {
    LoginContext *auth;
    ClinicRegisterPage *page;
    int *return_requested;
    login_ui_t *login_ui;
} RegisterUiController;

static volatile sig_atomic_t keep_running = 1;
static const char *validation_buttons[] = {"确定", ""};

static void secure_clear(void *memory, size_t length)
{
    volatile unsigned char *cursor = memory;

    while(length > 0U) {
        *cursor = 0U;
        ++cursor;
        --length;
    }
}

static int copy_string_checked(
    char *destination,
    size_t destination_capacity,
    const char *source,
    size_t maximum_length)
{
    size_t length;

    if(destination == NULL || destination_capacity == 0U || source == NULL) {
        return -1;
    }

    length = strlen(source);
    if(length > maximum_length || length + 1U > destination_capacity) {
        destination[0] = '\0';
        return -1;
    }

    memcpy(destination, source, length + 1U);
    return 0;
}

static int port_is_valid(const char *port)
{
    const unsigned char *cursor = (const unsigned char *)port;
    unsigned long value = 0UL;

    if(port == NULL || port[0] == '\0') {
        return 0;
    }
    while(*cursor != '\0') {
        if(*cursor < (unsigned char)'0' || *cursor > (unsigned char)'9') {
            return 0;
        }
        value = value * 10UL + (unsigned long)(*cursor - (unsigned char)'0');
        if(value > 65535UL) {
            return 0;
        }
        ++cursor;
    }
    return value >= 1UL;
}

static void handle_signal(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
}

static int install_signal_handlers(void)
{
    struct sigaction action = {0};
    struct sigaction pipe_action = {0};

    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if(sigaction(SIGINT, &action, NULL) != 0) {
        perror("sigaction(SIGINT)");
        return -1;
    }

    if(sigaction(SIGTERM, &action, NULL) != 0) {
        perror("sigaction(SIGTERM)");
        return -1;
    }

    pipe_action.sa_handler = SIG_IGN;
    sigemptyset(&pipe_action.sa_mask);
    pipe_action.sa_flags = 0;
    if(sigaction(SIGPIPE, &pipe_action, NULL) != 0) {
        perror("sigaction(SIGPIPE)");
        return -1;
    }

    return 0;
}

static void sleep_ms(long milliseconds)
{
    struct timespec delay;

    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (milliseconds % 1000) * 1000000L;
    nanosleep(&delay, NULL);
}

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

static void style_candidate_panel(lv_obj_t *candidate_panel, const lv_font_t *font)
{
    lv_obj_set_style_bg_color(candidate_panel, lv_color_hex(0xDCEAE5), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(candidate_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(candidate_panel, lv_color_hex(0x8FA39C), LV_PART_MAIN);
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

static lv_obj_t *get_login_candidate_panel(login_ui_t *ui)
{
    lv_obj_t *candidate_panel;

    if(ui == NULL) {
        return NULL;
    }
    if(ui->pinyin_ime == NULL || !lv_obj_is_valid(ui->pinyin_ime)) {
        ui->candidate_panel = NULL;
        return NULL;
    }

    candidate_panel = lv_ime_pinyin_get_cand_panel(ui->pinyin_ime);
    if(candidate_panel == NULL || !lv_obj_is_valid(candidate_panel)) {
        ui->candidate_panel = NULL;
        return NULL;
    }

    ui->candidate_panel = candidate_panel;
    return candidate_panel;
}

static void reset_login_pinyin_composition(login_ui_t *ui)
{
    lv_ime_pinyin_t *pinyin_ime;

    if(ui == NULL || ui->pinyin_ime == NULL ||
       !lv_obj_is_valid(ui->pinyin_ime)) {
        return;
    }

    pinyin_ime = (lv_ime_pinyin_t *)ui->pinyin_ime;
    clinic_pinyin_reset_composition(pinyin_ime);
}

static int is_ascii_letter_text(const char *text)
{
    if(text == NULL || text[0] == '\0' || text[1] != '\0') {
        return 0;
    }
    return (text[0] >= 'a' && text[0] <= 'z') ||
        (text[0] >= 'A' && text[0] <= 'Z');
}

static int login_pinyin_buffer_would_overflow(
    login_ui_t *ui,
    const char *text)
{
    lv_ime_pinyin_t *pinyin_ime;
    size_t input_length = 0U;

    if(ui == NULL || text == NULL || !is_ascii_letter_text(text) ||
       ui->pinyin_ime == NULL || !lv_obj_is_valid(ui->pinyin_ime)) {
        return 0;
    }

    pinyin_ime = (lv_ime_pinyin_t *)ui->pinyin_ime;
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

static void login_pinyin_keyboard_event_cb(lv_event_t *event)
{
    login_ui_t *ui = lv_event_get_user_data(event);
    lv_obj_t *keyboard = lv_event_get_target(event);
    const char *text = NULL;
    int overflow = 0;

    if(ui != NULL && keyboard == ui->username_keyboard &&
       lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        uint16_t button = lv_btnmatrix_get_selected_btn(keyboard);

        if(button != LV_BTNMATRIX_BTN_NONE) {
            text = lv_btnmatrix_get_btn_text(keyboard, button);
            overflow = login_pinyin_buffer_would_overflow(ui, text);
            if(overflow) {
                reset_login_pinyin_composition(ui);
                if(get_login_candidate_panel(ui) != NULL) {
                    lv_obj_add_flag(ui->candidate_panel, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    /* The stock callback is removed when this guard is installed. */
    lv_keyboard_def_event_cb(event);
    if(overflow) {
        /* Treat the overflowing key as ordinary text and skip the IME callback. */
        lv_event_stop_processing(event);
    }
}

static void login_pinyin_candidate_guard_event_cb(lv_event_t *event)
{
    login_ui_t *ui = lv_event_get_user_data(event);
    lv_obj_t *keyboard = lv_event_get_target(event);
    lv_ime_pinyin_t *pinyin_ime;

    if(ui == NULL || keyboard != ui->username_keyboard ||
       lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED ||
       ui->pinyin_ime == NULL || !lv_obj_is_valid(ui->pinyin_ime)) {
        return;
    }

    pinyin_ime = (lv_ime_pinyin_t *)ui->pinyin_ime;
    if(clinic_pinyin_discard_invalid_candidates(pinyin_ime) &&
       get_login_candidate_panel(ui) != NULL) {
        lv_obj_add_flag(ui->candidate_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_input_panel(login_ui_t *ui)
{
    lv_obj_t *candidate_panel;

    if(ui == NULL) {
        return;
    }
    if(ui->username_keyboard != NULL &&
       lv_obj_is_valid(ui->username_keyboard)) {
        lv_obj_add_flag(ui->username_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    if(ui->password_keyboard != NULL &&
       lv_obj_is_valid(ui->password_keyboard)) {
        lv_keyboard_set_textarea(ui->password_keyboard, NULL);
        lv_obj_add_flag(ui->password_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    candidate_panel = get_login_candidate_panel(ui);
    if(candidate_panel != NULL) {
        lv_obj_add_flag(candidate_panel, LV_OBJ_FLAG_HIDDEN);
    }
    reset_login_pinyin_composition(ui);
    if(ui->username != NULL && lv_obj_is_valid(ui->username)) {
        lv_obj_clear_state(ui->username, LV_STATE_FOCUSED);
    }
    if(ui->password != NULL && lv_obj_is_valid(ui->password)) {
        lv_obj_clear_state(ui->password, LV_STATE_FOCUSED);
    }
    ui->active_input = NULL;
}

static void detach_login_input_bindings(login_ui_t *ui)
{
    lv_obj_t *ime_keyboard = NULL;

    if(ui == NULL) {
        return;
    }

    hide_input_panel(ui);
    if(ui->pinyin_ime != NULL && lv_obj_is_valid(ui->pinyin_ime)) {
        ime_keyboard = lv_ime_pinyin_get_kb(ui->pinyin_ime);
        if(ime_keyboard != NULL && lv_obj_is_valid(ime_keyboard)) {
            lv_keyboard_set_textarea(ime_keyboard, NULL);
        }
    }
    if(ui->username_keyboard != NULL &&
       lv_obj_is_valid(ui->username_keyboard)) {
        lv_keyboard_set_textarea(ui->username_keyboard, NULL);
    }
    if(ui->password_keyboard != NULL &&
       lv_obj_is_valid(ui->password_keyboard)) {
        lv_keyboard_set_textarea(ui->password_keyboard, NULL);
    }
}

static void show_keyboard_cb(lv_event_t *event)
{
    login_ui_t *ui = lv_event_get_user_data(event);
    lv_obj_t *textarea = lv_event_get_target(event);
    lv_obj_t *candidate_panel;

    if(ui == NULL || !ui->login_page_active || textarea == NULL ||
       !lv_obj_is_valid(textarea) ||
       (textarea != ui->username && textarea != ui->password)) {
        return;
    }

    hide_input_panel(ui);

    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    lv_textarea_set_cursor_pos(textarea, LV_TEXTAREA_CURSOR_LAST);
    ui->active_input = textarea;

    if(textarea == ui->username && ui->username_keyboard != NULL &&
       lv_obj_is_valid(ui->username_keyboard)) {
        lv_keyboard_set_mode(ui->username_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(ui->username_keyboard, ui->username);
        lv_obj_clear_flag(ui->username_keyboard, LV_OBJ_FLAG_HIDDEN);
        candidate_panel = get_login_candidate_panel(ui);
        if(candidate_panel != NULL) {
            lv_obj_clear_flag(candidate_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else if(textarea == ui->password && ui->password_keyboard != NULL &&
            lv_obj_is_valid(ui->password_keyboard)) {
        lv_keyboard_set_mode(ui->password_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(ui->password_keyboard, ui->password);
        lv_obj_clear_flag(ui->password_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void keyboard_event_cb(lv_event_t *event)
{
    login_ui_t *ui = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);

    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        hide_input_panel(ui);
    }
}

static void screen_clicked_cb(lv_event_t *event)
{
    login_ui_t *ui = lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_target(event);
    lv_obj_t *current = lv_event_get_current_target(event);

    if(target == current) {
        hide_input_panel(ui);
    }
}

static void close_message_box(login_ui_t *ui)
{
    lv_obj_t *message_box;

    if(ui == NULL) {
        return;
    }

    message_box = ui->message_box;

    ui->message_box_close_requested = 0;
    ui->message_box = NULL;
    if(message_box != NULL && lv_obj_is_valid(message_box)) {
        lv_msgbox_close(message_box);
    }
}

static void message_box_event_cb(lv_event_t *event)
{
    login_ui_t *ui = lv_event_get_user_data(event);

    if(ui != NULL && lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        int should_enter_home = ui->pending_home_user_id > 0;

        ui->message_box_close_requested = 1;
        if(should_enter_home) {
            ui->home_transition_requested = 1;
        }
    }
}

static int show_validation_message(login_ui_t *ui, const char *message)
{
    lv_obj_t *backdrop;
    lv_obj_t *title;
    lv_obj_t *text;
    lv_obj_t *content;
    lv_obj_t *buttons;

    if(ui == NULL || message == NULL) {
        return -1;
    }
    if(ui->message_box != NULL) {
        if(lv_obj_is_valid(ui->message_box)) {
            return 0;
        }
        ui->message_box = NULL;
    }

    ui->message_box = lv_msgbox_create(NULL, "提示", message,
                                       validation_buttons, false);
    if(ui->message_box == NULL) {
        fprintf(stderr, "failed to create validation message box\n");
        return -1;
    }

    backdrop = lv_obj_get_parent(ui->message_box);
    title = lv_msgbox_get_title(ui->message_box);
    text = lv_msgbox_get_text(ui->message_box);
    content = lv_msgbox_get_content(ui->message_box);
    buttons = lv_msgbox_get_btns(ui->message_box);
    if(backdrop == NULL || title == NULL || text == NULL ||
       content == NULL || buttons == NULL) {
        fprintf(stderr, "failed to initialize validation message box\n");
        close_message_box(ui);
        return -1;
    }

    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_50, LV_PART_MAIN);

    lv_obj_set_width(ui->message_box, 460);
    lv_obj_set_style_bg_color(ui->message_box, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->message_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(ui->message_box, lv_color_hex(0x1C668C), LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->message_box, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->message_box, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ui->message_box, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_row(ui->message_box, 14, LV_PART_MAIN);

    lv_obj_set_style_text_font(title, ui->font, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x123F5A), LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(text, ui->font, LV_PART_MAIN);
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
    lv_obj_set_style_text_font(buttons, ui->font,
                               LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(buttons, lv_color_hex(0x04324B),
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(buttons, 2,
                                  LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(buttons, 8,
                            LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(buttons, lv_color_hex(0x073E5D),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_COVER,
                            LV_PART_ITEMS | LV_STATE_PRESSED);

    lv_obj_add_event_cb(ui->message_box, message_box_event_cb,
                        LV_EVENT_VALUE_CHANGED, ui);
    lv_obj_center(ui->message_box);
    return 0;
}

static void username_insert_cb(lv_event_t *event)
{
    login_ui_t *ui = lv_event_get_user_data(event);
    const char *inserted_text = lv_event_get_param(event);
    const char *current_text;
    size_t current_length;
    size_t inserted_length;

    if(ui == NULL || ui->username == NULL || inserted_text == NULL) {
        return;
    }
    current_text = lv_textarea_get_text(ui->username);
    current_length = strlen(current_text);
    inserted_length = strlen(inserted_text);
    if(current_length > CLINIC_USERNAME_MAX_LENGTH ||
       inserted_length > CLINIC_USERNAME_MAX_LENGTH - current_length) {
        lv_textarea_set_insert_replace(ui->username, "");
        (void)show_validation_message(ui, "用户名长度超过服务器限制");
    }
}

static void process_message_box_close(login_ui_t *ui)
{
    if(ui != NULL && ui->message_box_close_requested) {
        int should_enter_home = ui->home_transition_requested;

        close_message_box(ui);
        if(should_enter_home) {
            ui->auth_state = AUTH_UI_HOME_TRANSITION_PENDING;
        }
        else if(ui->auth_state == AUTH_UI_LOGIN_RESULT_PENDING) {
            ui->auth_state = AUTH_UI_LOGIN_IDLE;
        }
    }
}

static LoginState login_state_from_outcome(ClinicLoginOutcome outcome)
{
    switch(outcome) {
        case CLINIC_LOGIN_SUCCESS:
            return LOGIN_STATE_SUCCESS;
        case CLINIC_LOGIN_AUTH_FAILED:
            return LOGIN_STATE_AUTH_FAILED;
        case CLINIC_LOGIN_NETWORK_ERROR:
            return LOGIN_STATE_NETWORK_ERROR;
        case CLINIC_LOGIN_PROTOCOL_ERROR:
        default:
            return LOGIN_STATE_PROTOCOL_ERROR;
    }
}

static ClinicLoginOutcome login_outcome_from_register(
    ClinicRegisterOutcome outcome)
{
    switch(outcome) {
        case CLINIC_REGISTER_SUCCESS:
            return CLINIC_LOGIN_SUCCESS;
        case CLINIC_REGISTER_REJECTED:
            return CLINIC_LOGIN_AUTH_FAILED;
        case CLINIC_REGISTER_NETWORK_ERROR:
            return CLINIC_LOGIN_NETWORK_ERROR;
        case CLINIC_REGISTER_PROTOCOL_ERROR:
        default:
            return CLINIC_LOGIN_PROTOCOL_ERROR;
    }
}

/*
 * 认证网络工作线程入口。
 * 输入：主线程事先复制好的 LoginContext；处理：调用 login/register client；
 * 输出：加锁写回状态、用户 ID 和错误信息，并清除上下文里的密码副本。
 * 本函数没有任何 lv_* 调用，这是“网络线程不能操作 LVGL”的实际代码边界。
 */
static void *auth_worker(void *argument)
{
    LoginContext *context = argument;
    ClinicLoginResult result = {0};
    LoginState state;

    if(context->operation == AUTH_OPERATION_REGISTER) {
        ClinicRegisterResult register_result = {0};

        if(clinic_register_request(
               context->server_ip,
               context->server_port,
               context->username,
               context->password,
               context->request_id,
               REGISTER_TIMEOUT_MS,
               &register_result) != 0) {
            register_result.outcome = CLINIC_REGISTER_PROTOCOL_ERROR;
            register_result.user_id = 0;
            register_result.error_code[0] = '\0';
            (void)copy_string_checked(
                register_result.message,
                sizeof(register_result.message),
                "invalid register request",
                CLINIC_MESSAGE_MAX_LENGTH);
        }
        result.outcome = login_outcome_from_register(register_result.outcome);
        result.user_id = register_result.user_id;
        if(copy_string_checked(
               result.error_code,
               sizeof(result.error_code),
               register_result.error_code,
               CLINIC_ERROR_CODE_MAX_LENGTH) != 0 ||
           copy_string_checked(
               result.message,
               sizeof(result.message),
               register_result.message,
               CLINIC_MESSAGE_MAX_LENGTH) != 0) {
            result.outcome = CLINIC_LOGIN_PROTOCOL_ERROR;
            result.user_id = 0;
            result.error_code[0] = '\0';
            result.message[0] = '\0';
        }
    }
    else if(context->operation == AUTH_OPERATION_LOGIN &&
            clinic_login_request(
                context->server_ip,
                context->server_port,
                context->username,
                context->password,
                context->request_id,
                LOGIN_TIMEOUT_MS,
                &result) == 0) {
        /* The login client populated result. */
    }
    else {
        result.outcome = CLINIC_LOGIN_PROTOCOL_ERROR;
        result.user_id = 0;
        result.error_code[0] = '\0';
        (void)copy_string_checked(
            result.message,
            sizeof(result.message),
            "invalid authentication request",
            CLINIC_MESSAGE_MAX_LENGTH);
    }
    state = login_state_from_outcome(result.outcome);

    if(pthread_mutex_lock(&context->mutex) != 0) {
        secure_clear(context->password, sizeof(context->password));
        return NULL;
    }

    context->state = state;
    context->authenticated_user_id =
        state == LOGIN_STATE_SUCCESS ? result.user_id : 0;
    if(copy_string_checked(
           context->error_code,
           sizeof(context->error_code),
           result.error_code,
           CLINIC_ERROR_CODE_MAX_LENGTH) != 0 ||
       copy_string_checked(
           context->result_message,
           sizeof(context->result_message),
           result.message,
           CLINIC_MESSAGE_MAX_LENGTH) != 0) {
        context->state = LOGIN_STATE_PROTOCOL_ERROR;
        context->authenticated_user_id = 0;
        context->error_code[0] = '\0';
        context->result_message[0] = '\0';
    }
    secure_clear(context->password, sizeof(context->password));
    (void)pthread_mutex_unlock(&context->mutex);
    return NULL;
}

/* 以下四个函数都是短生命周期网络线程：同步请求可以阻塞这里，但不会阻塞界面。 */
static void *department_request_worker(void *argument)
{
    DepartmentRequestContext *context = argument;
    ClinicDepartmentListResult result = {0};

    if(clinic_department_list_request(
           context->server_ip,
           context->server_port,
           context->request_id,
           DEPARTMENT_TIMEOUT_MS,
           &result) != 0) {
        memset(&result, 0, sizeof(result));
        result.outcome = CLINIC_DEPARTMENT_LIST_PROTOCOL_ERROR;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return NULL;
    }
    context->result = result;
    context->state = DEPARTMENT_REQUEST_FINISHED;
    (void)pthread_mutex_unlock(&context->mutex);
    return NULL;
}

static void *doctor_request_worker(void *argument)
{
    DoctorRequestContext *context = argument;
    ClinicDoctorListResult result = {0};

    if(clinic_doctor_list_request(
           context->server_ip,
           context->server_port,
           context->request_id,
           context->department_id,
           DOCTOR_TIMEOUT_MS,
           &result) != 0) {
        memset(&result, 0, sizeof(result));
        result.outcome = CLINIC_DOCTOR_LIST_PROTOCOL_ERROR;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return NULL;
    }
    context->result = result;
    context->state = DOCTOR_REQUEST_FINISHED;
    (void)pthread_mutex_unlock(&context->mutex);
    return NULL;
}

static void *ticket_request_worker(void *argument)
{
    TicketRequestContext *context = argument;
    ClinicTicketCreateResult result = {0};

    if(clinic_ticket_create_request(
           context->server_ip,
           context->server_port,
           context->request_id,
           context->user_id,
           context->department_id,
           TICKET_TIMEOUT_MS,
           &result) != 0) {
        memset(&result, 0, sizeof(result));
        result.outcome = CLINIC_TICKET_CREATE_PROTOCOL_ERROR;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return NULL;
    }
    context->result = result;
    context->state = TICKET_REQUEST_FINISHED;
    (void)pthread_mutex_unlock(&context->mutex);
    return NULL;
}

static void *current_ticket_request_worker(void *argument)
{
    CurrentTicketRequestContext *context = argument;
    ClinicCurrentTicketResult result = {0};

    if(clinic_ticket_get_current_request(
           context->server_ip,
           context->server_port,
           context->request_id,
           context->user_id,
           CURRENT_TICKET_TIMEOUT_MS,
           &result) != 0) {
        memset(&result, 0, sizeof(result));
        result.outcome = CLINIC_CURRENT_TICKET_PROTOCOL_ERROR;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return NULL;
    }
    context->result = result;
    context->state = CURRENT_TICKET_REQUEST_FINISHED;
    (void)pthread_mutex_unlock(&context->mutex);
    return NULL;
}

static void update_remember_button_label(login_ui_t *ui)
{
    int remembered;

    if(ui == NULL || ui->remember_button == NULL ||
       ui->remember_button_label == NULL ||
       !lv_obj_is_valid(ui->remember_button) ||
       !lv_obj_is_valid(ui->remember_button_label)) {
        return;
    }

    remembered = lv_obj_has_state(ui->remember_button, LV_STATE_CHECKED);
    lv_label_set_text(
        ui->remember_button_label,
        remembered ? "记住密码：是" : "记住密码：否");
    lv_obj_set_style_text_color(
        ui->remember_button_label,
        remembered ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x173C35),
        LV_PART_MAIN);
    lv_obj_center(ui->remember_button_label);
}

static void remember_button_event_cb(lv_event_t *event)
{
    login_ui_t *ui = lv_event_get_user_data(event);

    if(lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        update_remember_button_label(ui);
    }
}

static void load_remembered_credentials(login_ui_t *ui)
{
    ClinicRememberedCredentials credentials = {0};
    ClinicCredentialStoreStatus status;

    if(ui == NULL || ui->username == NULL || ui->password == NULL ||
       ui->remember_button == NULL ||
       !lv_obj_is_valid(ui->username) || !lv_obj_is_valid(ui->password) ||
       !lv_obj_is_valid(ui->remember_button)) {
        return;
    }

    status = clinic_credential_store_load(
        REMEMBERED_CREDENTIALS_PATH,
        &credentials);
    if(status == CLINIC_CREDENTIAL_STORE_OK) {
        lv_textarea_set_text(ui->username, credentials.username);
        lv_textarea_set_text(ui->password, credentials.password);
        lv_obj_add_state(ui->remember_button, LV_STATE_CHECKED);
    }
    else {
        lv_obj_clear_state(ui->remember_button, LV_STATE_CHECKED);
        if(status == CLINIC_CREDENTIAL_STORE_INVALID_DATA) {
            (void)clinic_credential_store_remove(
                REMEMBERED_CREDENTIALS_PATH);
        }
    }
    update_remember_button_label(ui);
    secure_clear(&credentials, sizeof(credentials));
}

static int sync_remembered_credentials(login_ui_t *ui)
{
    ClinicCredentialStoreStatus status;

    if(ui == NULL || ui->username == NULL || ui->password == NULL ||
       ui->remember_button == NULL ||
       !lv_obj_is_valid(ui->username) || !lv_obj_is_valid(ui->password) ||
       !lv_obj_is_valid(ui->remember_button)) {
        return -1;
    }

    if(lv_obj_has_state(ui->remember_button, LV_STATE_CHECKED)) {
        status = clinic_credential_store_save(
            REMEMBERED_CREDENTIALS_PATH,
            lv_textarea_get_text(ui->username),
            lv_textarea_get_text(ui->password));
    }
    else {
        status = clinic_credential_store_remove(
            REMEMBERED_CREDENTIALS_PATH);
    }
    return status == CLINIC_CREDENTIAL_STORE_OK ? 0 : -1;
}

static void set_login_button_running(login_ui_t *ui, int running)
{
    if(running) {
        lv_label_set_text(ui->login_button_label, "登录中...");
        lv_obj_add_state(ui->login_button, LV_STATE_DISABLED);
        lv_obj_add_state(ui->register_entry_button, LV_STATE_DISABLED);
        if(ui->remember_button != NULL &&
           lv_obj_is_valid(ui->remember_button)) {
            lv_obj_add_state(ui->remember_button, LV_STATE_DISABLED);
        }
    }
    else {
        lv_obj_clear_state(ui->login_button, LV_STATE_DISABLED);
        lv_obj_clear_state(ui->register_entry_button, LV_STATE_DISABLED);
        if(ui->remember_button != NULL &&
           lv_obj_is_valid(ui->remember_button)) {
            lv_obj_clear_state(ui->remember_button, LV_STATE_DISABLED);
        }
        lv_label_set_text(ui->login_button_label, "登录");
    }
}

/*
 * LVGL 主线程消费登录结果。
 * 先在 mutex 内复制线程状态，确认 worker 已结束后 pthread_join()；join 完成才读取最终结果、
 * 恢复按钮、保存记住密码或安排切页。结果只消费一次，thread_pending_join 随后清零。
 */
static int process_login_result(login_ui_t *ui)
{
    LoginContext *context;
    LoginState state;
    AuthOperation operation;
    pthread_t worker;
    int thread_pending_join;
    int64_t user_id;
    char result_message[CLINIC_MESSAGE_MAX_LENGTH + 1U];
    const char *display_message;
    int join_result;
    int page_active;
    int remember_sync_failed = 0;

    if(ui == NULL ||
       ui->auth_state != AUTH_UI_LOGIN_REQUEST_RUNNING ||
       ui->login == NULL) {
        return 0;
    }
    context = ui->login;
    page_active = ui->login_page_active;

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    state = context->state;
    operation = context->operation;
    thread_pending_join = context->thread_pending_join;
    worker = context->worker;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(operation != AUTH_OPERATION_LOGIN || !thread_pending_join ||
       state == LOGIN_STATE_RUNNING) {
        return 0;
    }

    join_result = pthread_join(worker, NULL);
    if(join_result != 0) {
        fprintf(stderr, "failed to join login worker: %d\n", join_result);
        return -1;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    context->thread_pending_join = 0;
    context->operation = AUTH_OPERATION_NONE;
    state = context->state;
    user_id = context->authenticated_user_id;
    if(copy_string_checked(
           result_message,
           sizeof(result_message),
           context->result_message,
           CLINIC_MESSAGE_MAX_LENGTH) != 0) {
        result_message[0] = '\0';
        state = LOGIN_STATE_PROTOCOL_ERROR;
    }
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(!page_active) {
        ui->pending_home_user_id = 0;
        ui->home_transition_requested = 0;
        ui->auth_state = AUTH_UI_LOGIN_IDLE;
        return 0;
    }
    set_login_button_running(ui, 0);
    if(state == LOGIN_STATE_SUCCESS && user_id > 0) {
        remember_sync_failed = sync_remembered_credentials(ui) != 0;
        display_message = remember_sync_failed
            ? "登录成功，但记住密码保存失败"
            : "登录成功";
    }
    else if(state == LOGIN_STATE_AUTH_FAILED) {
        display_message = "用户名或密码错误";
    }
    else if(state == LOGIN_STATE_NETWORK_ERROR) {
        display_message = "无法连接服务器";
    }
    else {
        display_message = "服务器响应异常";
    }

    ui->pending_home_user_id =
        state == LOGIN_STATE_SUCCESS && user_id > 0 ? user_id : 0;
    ui->auth_state = AUTH_UI_LOGIN_RESULT_PENDING;
    if(show_validation_message(ui, display_message) != 0) {
        ui->pending_home_user_id = 0;
        ui->auth_state = AUTH_UI_LOGIN_IDLE;
        return -1;
    }
    return 0;
}

/*
 * 退出程序或销毁相关页面前的兜底等待：只要存在尚未回收的线程就 join。
 * join 不负责“杀死线程”，而是等待它正常结束并回收线程资源。
 */
static int wait_for_login_worker(LoginContext *context)
{
    pthread_t worker;
    int thread_pending_join;
    int join_result;

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    worker = context->worker;
    thread_pending_join = context->thread_pending_join;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(thread_pending_join) {
        join_result = pthread_join(worker, NULL);
        if(join_result != 0) {
            fprintf(stderr, "failed to join login worker: %d\n", join_result);
            return -1;
        }
        if(pthread_mutex_lock(&context->mutex) != 0) {
            return -1;
        }
        context->thread_pending_join = 0;
        context->operation = AUTH_OPERATION_NONE;
        secure_clear(context->password, sizeof(context->password));
        if(pthread_mutex_unlock(&context->mutex) != 0) {
            return -1;
        }
    }
    return 0;
}

static void register_submit_requested(
    const char *username,
    const char *password,
    void *user_data)
{
    RegisterUiController *controller = user_data;
    LoginContext *context;
    int create_result;

    if(controller == NULL || controller->auth == NULL ||
       controller->page == NULL || controller->login_ui == NULL ||
       controller->login_ui->auth_state != AUTH_UI_REGISTER_IDLE ||
       username == NULL || password == NULL) {
        return;
    }
    context = controller->auth;
    if(pthread_mutex_lock(&context->mutex) != 0) {
        clinic_register_page_show_status(
            controller->page,
            "无法启动注册任务");
        return;
    }
    if(context->state == LOGIN_STATE_RUNNING ||
       context->thread_pending_join) {
        (void)pthread_mutex_unlock(&context->mutex);
        return;
    }
    if(copy_string_checked(
           context->username,
           sizeof(context->username),
           username,
           CLINIC_USERNAME_MAX_LENGTH) != 0 ||
       copy_string_checked(
           context->password,
           sizeof(context->password),
           password,
           CLINIC_PASSWORD_MAX_LENGTH) != 0) {
        secure_clear(context->password, sizeof(context->password));
        (void)pthread_mutex_unlock(&context->mutex);
        clinic_register_page_show_status(controller->page, "输入内容过长");
        return;
    }
    context->request_id = context->request_id >= LOGIN_REQUEST_ID_MAX
        ? UINT64_C(1)
        : context->request_id + UINT64_C(1);
    context->operation = AUTH_OPERATION_REGISTER;
    context->state = LOGIN_STATE_RUNNING;
    context->thread_pending_join = 1;
    context->authenticated_user_id = 0;
    context->error_code[0] = '\0';
    context->result_message[0] = '\0';
    (void)pthread_mutex_unlock(&context->mutex);

    controller->login_ui->auth_state = AUTH_UI_REGISTER_REQUEST_RUNNING;
    clinic_register_page_set_busy(controller->page, 1);
    create_result = pthread_create(
        &context->worker,
        NULL,
        auth_worker,
        context);
    if(create_result == 0) {
        return;
    }

    fprintf(stderr, "failed to create register worker: %d\n", create_result);
    if(pthread_mutex_lock(&context->mutex) == 0) {
        context->thread_pending_join = 0;
        context->state = LOGIN_STATE_NETWORK_ERROR;
        context->operation = AUTH_OPERATION_NONE;
        secure_clear(context->password, sizeof(context->password));
        (void)pthread_mutex_unlock(&context->mutex);
    }
    else {
        context->thread_pending_join = 0;
        context->state = LOGIN_STATE_NETWORK_ERROR;
        context->operation = AUTH_OPERATION_NONE;
        secure_clear(context->password, sizeof(context->password));
    }
    controller->login_ui->auth_state = AUTH_UI_REGISTER_IDLE;
    clinic_register_page_set_busy(controller->page, 0);
    clinic_register_page_show_status(controller->page, "无法启动注册任务");
}

static void register_back_requested(void *user_data)
{
    RegisterUiController *controller = user_data;
    LoginContext *context;
    int busy;

    if(controller == NULL || controller->auth == NULL ||
       controller->return_requested == NULL || controller->login_ui == NULL ||
       controller->login_ui->auth_state != AUTH_UI_REGISTER_IDLE) {
        return;
    }
    context = controller->auth;
    if(pthread_mutex_lock(&context->mutex) != 0) {
        return;
    }
    busy = context->state == LOGIN_STATE_RUNNING ||
        context->thread_pending_join;
    (void)pthread_mutex_unlock(&context->mutex);
    if(!busy) {
        *controller->return_requested = 1;
        controller->login_ui->auth_state = AUTH_UI_REGISTER_RETURN_PENDING;
    }
}

static int process_register_result(
    login_ui_t *ui,
    ClinicRegisterPage *page,
    LoginContext *context,
    char *registered_username,
    size_t registered_username_capacity,
    int *return_requested,
    int *success_message_pending)
{
    LoginState state;
    AuthOperation operation;
    pthread_t worker;
    int thread_pending_join;
    int64_t user_id;
    char error_code[CLINIC_ERROR_CODE_MAX_LENGTH + 1U];
    char username[CLINIC_USERNAME_MAX_LENGTH + 1U];
    const char *display_message;
    int join_result;
    int page_active;

    if(ui == NULL || context == NULL ||
       ui->auth_state != AUTH_UI_REGISTER_REQUEST_RUNNING ||
       registered_username == NULL || registered_username_capacity == 0U ||
       return_requested == NULL || success_message_pending == NULL) {
        return 0;
    }
    page_active = clinic_register_page_is_active(page);
    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    state = context->state;
    operation = context->operation;
    thread_pending_join = context->thread_pending_join;
    worker = context->worker;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }
    if(operation != AUTH_OPERATION_REGISTER || !thread_pending_join ||
       state == LOGIN_STATE_RUNNING) {
        return 0;
    }

    join_result = pthread_join(worker, NULL);
    if(join_result != 0) {
        fprintf(stderr, "failed to join register worker: %d\n", join_result);
        return -1;
    }
    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    context->thread_pending_join = 0;
    state = context->state;
    user_id = context->authenticated_user_id;
    if(copy_string_checked(
           error_code,
           sizeof(error_code),
           context->error_code,
           CLINIC_ERROR_CODE_MAX_LENGTH) != 0 ||
       copy_string_checked(
           username,
           sizeof(username),
           context->username,
           CLINIC_USERNAME_MAX_LENGTH) != 0) {
        state = LOGIN_STATE_PROTOCOL_ERROR;
        error_code[0] = '\0';
        username[0] = '\0';
    }
    context->operation = AUTH_OPERATION_NONE;
    secure_clear(context->password, sizeof(context->password));
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }
    if(!page_active) {
        ui->auth_state = AUTH_UI_REGISTER_IDLE;
        *return_requested = 0;
        *success_message_pending = 0;
        registered_username[0] = '\0';
        return 0;
    }
    ui->auth_state = AUTH_UI_REGISTER_RESULT_PENDING;
    clinic_register_page_set_busy(page, 0);
    if(state == LOGIN_STATE_SUCCESS && user_id > 0 &&
       copy_string_checked(
           registered_username,
           registered_username_capacity,
           username,
           CLINIC_USERNAME_MAX_LENGTH) == 0) {
        *success_message_pending = 1;
        *return_requested = 1;
        ui->auth_state = AUTH_UI_REGISTER_RETURN_PENDING;
        return 0;
    }

    if(state == LOGIN_STATE_AUTH_FAILED &&
       strcmp(error_code, "USERNAME_EXISTS") == 0) {
        display_message = "用户名已存在";
    }
    else if(state == LOGIN_STATE_AUTH_FAILED &&
            strcmp(error_code, "INVALID_ARGUMENT") == 0) {
        display_message = "注册信息不合法";
    }
    else if(state == LOGIN_STATE_AUTH_FAILED &&
            strcmp(error_code, "DATABASE_ERROR") == 0) {
        display_message = "服务器暂时无法完成注册";
    }
    else if(state == LOGIN_STATE_AUTH_FAILED) {
        display_message = "注册失败，请稍后重试";
    }
    else if(state == LOGIN_STATE_NETWORK_ERROR) {
        display_message = "无法连接服务器";
    }
    else {
        display_message = "服务器响应异常";
    }
    clinic_register_page_show_status(page, display_message);
    ui->auth_state = AUTH_UI_REGISTER_IDLE;
    return 0;
}

/*
 * 页面点击事件的通用启动模式：拒绝重复请求，复制本次参数，标记 RUNNING，
 * 再创建 worker；创建失败则把状态恢复，保证按钮不会永久停在加载状态。
 */
static void department_request_clicked(
    void *user_data,
    ClinicServiceFlow flow)
{
    DepartmentUiController *controller = user_data;
    DepartmentRequestContext *context;
    int create_result;

    if(controller == NULL || controller->request == NULL ||
       !clinic_service_flow_is_valid(flow) ||
       controller->home_page == NULL) {
        return;
    }
    context = controller->request;

    if(pthread_mutex_lock(&context->mutex) != 0) {
        (void)clinic_home_page_show_message(
            controller->home_page,
            "获取科室失败");
        return;
    }
    if(context->state == DEPARTMENT_REQUEST_RUNNING ||
       context->thread_pending_join) {
        (void)pthread_mutex_unlock(&context->mutex);
        return;
    }

    context->request_id =
        context->request_id >= DEPARTMENT_REQUEST_ID_MAX
            ? UINT64_C(1)
            : context->request_id + UINT64_C(1);
    context->flow = flow;
    context->state = DEPARTMENT_REQUEST_RUNNING;
    context->thread_pending_join = 1;
    memset(&context->result, 0, sizeof(context->result));
    (void)pthread_mutex_unlock(&context->mutex);

    clinic_home_page_set_department_loading(controller->home_page, 1);
    create_result = pthread_create(
        &context->worker,
        NULL,
        department_request_worker,
        context);
    if(create_result == 0) {
        return;
    }

    fprintf(stderr, "failed to create department worker: %d\n", create_result);
    if(pthread_mutex_lock(&context->mutex) == 0) {
        context->thread_pending_join = 0;
        context->state = DEPARTMENT_REQUEST_IDLE;
        (void)pthread_mutex_unlock(&context->mutex);
    }
    else {
        context->thread_pending_join = 0;
        context->state = DEPARTMENT_REQUEST_IDLE;
    }
    clinic_home_page_set_department_loading(controller->home_page, 0);
    (void)clinic_home_page_show_message(
        controller->home_page,
        "获取科室失败");
}

static int wait_for_department_worker(DepartmentRequestContext *context)
{
    pthread_t worker;
    int thread_pending_join;
    int join_result;

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    worker = context->worker;
    thread_pending_join = context->thread_pending_join;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(!thread_pending_join) {
        return 0;
    }

    join_result = pthread_join(worker, NULL);
    if(join_result != 0) {
        fprintf(stderr, "failed to join department worker: %d\n", join_result);
        return -1;
    }
    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    context->thread_pending_join = 0;
    context->state = DEPARTMENT_REQUEST_IDLE;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }
    return 0;
}

static void doctor_request_clicked(
    int64_t department_id,
    const char *department_name,
    void *user_data)
{
    DoctorUiController *controller = user_data;
    DoctorRequestContext *context;
    int create_result;

    if(controller == NULL || controller->request == NULL ||
       controller->department_page == NULL ||
       controller->department_return_requested == NULL || department_id <= 0 ||
       department_name == NULL || department_name[0] == '\0') {
        return;
    }
    context = controller->request;
    *controller->department_return_requested = 0;

    if(pthread_mutex_lock(&context->mutex) != 0) {
        (void)clinic_department_page_show_message(
            controller->department_page,
            "获取医生失败");
        return;
    }
    if(context->state == DOCTOR_REQUEST_RUNNING ||
       context->thread_pending_join) {
        (void)pthread_mutex_unlock(&context->mutex);
        return;
    }
    if(copy_string_checked(
           context->department_name,
           sizeof(context->department_name),
           department_name,
           CLINIC_DEPARTMENT_NAME_MAX_LENGTH) != 0) {
        (void)pthread_mutex_unlock(&context->mutex);
        (void)clinic_department_page_show_message(
            controller->department_page,
            "医生数据异常");
        return;
    }
    context->request_id = context->request_id >= DOCTOR_REQUEST_ID_MAX
        ? UINT64_C(1)
        : context->request_id + UINT64_C(1);
    context->department_id = department_id;
    context->state = DOCTOR_REQUEST_RUNNING;
    context->thread_pending_join = 1;
    memset(&context->result, 0, sizeof(context->result));
    (void)pthread_mutex_unlock(&context->mutex);

    clinic_department_page_set_request_loading(
        controller->department_page,
        1);
    create_result = pthread_create(
        &context->worker,
        NULL,
        doctor_request_worker,
        context);
    if(create_result == 0) {
        return;
    }

    fprintf(stderr, "failed to create doctor worker: %d\n", create_result);
    if(pthread_mutex_lock(&context->mutex) == 0) {
        context->thread_pending_join = 0;
        context->state = DOCTOR_REQUEST_IDLE;
        (void)pthread_mutex_unlock(&context->mutex);
    }
    else {
        context->thread_pending_join = 0;
        context->state = DOCTOR_REQUEST_IDLE;
    }
    clinic_department_page_set_request_loading(
        controller->department_page,
        0);
    (void)clinic_department_page_show_message(
        controller->department_page,
        "获取医生失败");
}

static int wait_for_doctor_worker(DoctorRequestContext *context)
{
    pthread_t worker;
    int thread_pending_join;
    int join_result;

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    worker = context->worker;
    thread_pending_join = context->thread_pending_join;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(!thread_pending_join) {
        return 0;
    }

    join_result = pthread_join(worker, NULL);
    if(join_result != 0) {
        fprintf(stderr, "failed to join doctor worker: %d\n", join_result);
        return -1;
    }
    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    context->thread_pending_join = 0;
    context->state = DOCTOR_REQUEST_IDLE;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }
    return 0;
}

static void ticket_request_clicked(
    int64_t department_id,
    const char *department_name,
    void *user_data)
{
    TicketUiController *controller = user_data;
    TicketRequestContext *context;
    int64_t user_id;
    int create_result;

    if(controller == NULL || controller->request == NULL ||
       controller->department_page == NULL ||
       controller->authenticated_user_id == NULL ||
       controller->department_return_requested == NULL || department_id <= 0 ||
       department_name == NULL || department_name[0] == '\0') {
        return;
    }
    context = controller->request;
    user_id = *controller->authenticated_user_id;
    if(user_id <= 0) {
        (void)clinic_department_page_show_message(
            controller->department_page,
            "取号失败");
        return;
    }
    *controller->department_return_requested = 0;

    if(pthread_mutex_lock(&context->mutex) != 0) {
        (void)clinic_department_page_show_message(
            controller->department_page,
            "取号失败");
        return;
    }
    if(context->state == TICKET_REQUEST_RUNNING ||
       context->thread_pending_join) {
        (void)pthread_mutex_unlock(&context->mutex);
        return;
    }
    if(copy_string_checked(
           context->department_name,
           sizeof(context->department_name),
           department_name,
           CLINIC_DEPARTMENT_NAME_MAX_LENGTH) != 0) {
        (void)pthread_mutex_unlock(&context->mutex);
        (void)clinic_department_page_show_message(
            controller->department_page,
            "号单数据异常");
        return;
    }

    context->request_id = context->request_id >= TICKET_REQUEST_ID_MAX
        ? UINT64_C(1)
        : context->request_id + UINT64_C(1);
    context->user_id = user_id;
    context->department_id = department_id;
    context->state = TICKET_REQUEST_RUNNING;
    context->thread_pending_join = 1;
    memset(&context->result, 0, sizeof(context->result));
    (void)pthread_mutex_unlock(&context->mutex);

    clinic_department_page_set_request_loading(
        controller->department_page,
        1);
    create_result = pthread_create(
        &context->worker,
        NULL,
        ticket_request_worker,
        context);
    if(create_result == 0) {
        return;
    }

    fprintf(stderr, "failed to create ticket worker: %d\n", create_result);
    if(pthread_mutex_lock(&context->mutex) == 0) {
        context->thread_pending_join = 0;
        context->state = TICKET_REQUEST_IDLE;
        (void)pthread_mutex_unlock(&context->mutex);
    }
    else {
        context->thread_pending_join = 0;
        context->state = TICKET_REQUEST_IDLE;
    }
    clinic_department_page_set_request_loading(
        controller->department_page,
        0);
    (void)clinic_department_page_show_message(
        controller->department_page,
        "取号失败");
}

static int wait_for_ticket_worker(TicketRequestContext *context)
{
    pthread_t worker;
    int thread_pending_join;
    int join_result;

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    worker = context->worker;
    thread_pending_join = context->thread_pending_join;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(!thread_pending_join) {
        return 0;
    }

    join_result = pthread_join(worker, NULL);
    if(join_result != 0) {
        fprintf(stderr, "failed to join ticket worker: %d\n", join_result);
        return -1;
    }
    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    context->thread_pending_join = 0;
    context->state = TICKET_REQUEST_IDLE;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }
    return 0;
}

static void set_current_ticket_request_loading(
    CurrentTicketUiController *controller,
    CurrentTicketRequestSource source,
    int loading)
{
    if(source == CURRENT_TICKET_REQUEST_SOURCE_HOME) {
        clinic_home_page_set_current_ticket_loading(controller->home_page, loading);
    }
    else if(source == CURRENT_TICKET_REQUEST_SOURCE_QUEUE) {
        clinic_queue_page_set_refreshing(controller->queue_page, loading);
    }
}

static void show_current_ticket_request_error(
    CurrentTicketUiController *controller,
    CurrentTicketRequestSource source)
{
    if(source == CURRENT_TICKET_REQUEST_SOURCE_HOME) {
        (void)clinic_home_page_show_message(
            controller->home_page,
            "查询号单失败");
    }
    else if(source == CURRENT_TICKET_REQUEST_SOURCE_QUEUE) {
        (void)clinic_queue_page_show_message(
            controller->queue_page,
            "查询号单失败");
    }
}

static void start_current_ticket_request(
    CurrentTicketUiController *controller,
    CurrentTicketRequestSource source)
{
    CurrentTicketRequestContext *context;
    int64_t user_id;
    int create_result;

    if(controller == NULL || controller->request == NULL ||
       controller->home_page == NULL || controller->queue_page == NULL ||
       controller->authenticated_user_id == NULL) {
        return;
    }
    context = controller->request;
    user_id = *controller->authenticated_user_id;
    if(user_id <= 0) {
        show_current_ticket_request_error(controller, source);
        return;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        show_current_ticket_request_error(controller, source);
        return;
    }
    if(context->state == CURRENT_TICKET_REQUEST_RUNNING ||
       context->thread_pending_join) {
        (void)pthread_mutex_unlock(&context->mutex);
        return;
    }

    context->request_id =
        context->request_id >= CURRENT_TICKET_REQUEST_ID_MAX
            ? UINT64_C(1)
            : context->request_id + UINT64_C(1);
    context->user_id = user_id;
    context->source = source;
    context->state = CURRENT_TICKET_REQUEST_RUNNING;
    context->thread_pending_join = 1;
    memset(&context->result, 0, sizeof(context->result));
    (void)pthread_mutex_unlock(&context->mutex);

    if(source == CURRENT_TICKET_REQUEST_SOURCE_QUEUE) {
        (void)clinic_queue_page_show_message(controller->queue_page, "");
    }
    set_current_ticket_request_loading(controller, source, 1);
    create_result = pthread_create(
        &context->worker,
        NULL,
        current_ticket_request_worker,
        context);
    if(create_result == 0) {
        return;
    }

    fprintf(
        stderr,
        "failed to create current ticket worker: %d\n",
        create_result);
    if(pthread_mutex_lock(&context->mutex) == 0) {
        context->thread_pending_join = 0;
        context->state = CURRENT_TICKET_REQUEST_IDLE;
        context->source = CURRENT_TICKET_REQUEST_SOURCE_NONE;
        (void)pthread_mutex_unlock(&context->mutex);
    }
    else {
        context->thread_pending_join = 0;
        context->state = CURRENT_TICKET_REQUEST_IDLE;
        context->source = CURRENT_TICKET_REQUEST_SOURCE_NONE;
    }
    set_current_ticket_request_loading(controller, source, 0);
    show_current_ticket_request_error(controller, source);
}

static void current_ticket_request_clicked(void *user_data)
{
    start_current_ticket_request(
        user_data,
        CURRENT_TICKET_REQUEST_SOURCE_HOME);
}

static void current_ticket_refresh_clicked(void *user_data)
{
    start_current_ticket_request(
        user_data,
        CURRENT_TICKET_REQUEST_SOURCE_QUEUE);
}

static int wait_for_current_ticket_worker(CurrentTicketRequestContext *context)
{
    pthread_t worker;
    int thread_pending_join;
    int join_result;

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    worker = context->worker;
    thread_pending_join = context->thread_pending_join;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(!thread_pending_join) {
        return 0;
    }

    join_result = pthread_join(worker, NULL);
    if(join_result != 0) {
        fprintf(
            stderr,
            "failed to join current ticket worker: %d\n",
            join_result);
        return -1;
    }
    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    context->thread_pending_join = 0;
    context->state = CURRENT_TICKET_REQUEST_IDLE;
    context->source = CURRENT_TICKET_REQUEST_SOURCE_NONE;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }
    return 0;
}

/*
 * 登录按钮回调运行在 LVGL 主线程。
 * 它从控件读取并校验文本，然后复制到 LoginContext 后启动 auth_worker；
 * 回调本身不连接服务器，也不会 pthread_join() 阻塞触摸事件。
 */
static void login_clicked_cb(lv_event_t *event)
{
    login_ui_t *ui = lv_event_get_user_data(event);
    LoginContext *context;
    const char *message;
    const char *username;
    const char *password;
    int create_result;

    if(ui == NULL || !ui->login_page_active || ui->login == NULL ||
       ui->auth_state != AUTH_UI_LOGIN_IDLE || ui->message_box != NULL ||
       ui->username == NULL || ui->password == NULL ||
       !lv_obj_is_valid(ui->username) || !lv_obj_is_valid(ui->password)) {
        return;
    }
    context = ui->login;
    hide_input_panel(ui);
    ui->pending_home_user_id = 0;
    ui->home_transition_requested = 0;
    ui->register_transition_requested = 0;

    username = lv_textarea_get_text(ui->username);
    password = lv_textarea_get_text(ui->password);

    if(username[0] == '\0' && password[0] == '\0') {
        message = "请输入用户名和密码";
    }
    else if(username[0] == '\0') {
        message = "请输入用户名";
    }
    else if(password[0] == '\0') {
        message = "请输入密码";
    }
    else {
        message = NULL;
    }

    if(message != NULL) {
        (void)show_validation_message(ui, message);
        return;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        (void)show_validation_message(ui, "无法启动登录任务");
        return;
    }
    if(context->state == LOGIN_STATE_RUNNING ||
       context->thread_pending_join) {
        (void)pthread_mutex_unlock(&context->mutex);
        return;
    }
    if(copy_string_checked(
           context->username,
           sizeof(context->username),
           username,
           CLINIC_USERNAME_MAX_LENGTH) != 0 ||
       copy_string_checked(
           context->password,
           sizeof(context->password),
           password,
           CLINIC_PASSWORD_MAX_LENGTH) != 0) {
        secure_clear(context->password, sizeof(context->password));
        (void)pthread_mutex_unlock(&context->mutex);
        (void)show_validation_message(ui, "输入内容过长");
        return;
    }

    context->request_id = context->request_id >= LOGIN_REQUEST_ID_MAX
        ? UINT64_C(1)
        : context->request_id + UINT64_C(1);
    context->operation = AUTH_OPERATION_LOGIN;
    context->state = LOGIN_STATE_RUNNING;
    context->thread_pending_join = 1;
    context->authenticated_user_id = 0;
    context->error_code[0] = '\0';
    context->result_message[0] = '\0';
    (void)pthread_mutex_unlock(&context->mutex);

    ui->auth_state = AUTH_UI_LOGIN_REQUEST_RUNNING;
    set_login_button_running(ui, 1);
    create_result = pthread_create(
        &context->worker,
        NULL,
        auth_worker,
        context);
    if(create_result == 0) {
        return;
    }

    fprintf(stderr, "failed to create login worker: %d\n", create_result);
    if(pthread_mutex_lock(&context->mutex) == 0) {
        context->thread_pending_join = 0;
        context->state = LOGIN_STATE_NETWORK_ERROR;
        context->operation = AUTH_OPERATION_NONE;
        secure_clear(context->password, sizeof(context->password));
        (void)pthread_mutex_unlock(&context->mutex);
    }
    else {
        context->thread_pending_join = 0;
        context->state = LOGIN_STATE_NETWORK_ERROR;
        context->operation = AUTH_OPERATION_NONE;
        secure_clear(context->password, sizeof(context->password));
    }
    ui->auth_state = AUTH_UI_LOGIN_IDLE;
    set_login_button_running(ui, 0);
    (void)show_validation_message(ui, "无法启动登录任务");
}

static void register_entry_clicked_cb(lv_event_t *event)
{
    login_ui_t *ui = lv_event_get_user_data(event);
    LoginContext *context;
    int busy;

    if(ui == NULL || !ui->login_page_active || ui->login == NULL ||
       ui->auth_state != AUTH_UI_LOGIN_IDLE || ui->message_box != NULL) {
        return;
    }
    context = ui->login;
    if(pthread_mutex_lock(&context->mutex) != 0) {
        (void)show_validation_message(ui, "暂时无法进入注册页面");
        return;
    }
    busy = context->state == LOGIN_STATE_RUNNING ||
        context->thread_pending_join;
    (void)pthread_mutex_unlock(&context->mutex);
    if(busy) {
        return;
    }

    hide_input_panel(ui);
    close_message_box(ui);
    ui->pending_home_user_id = 0;
    ui->home_transition_requested = 0;
    ui->register_transition_requested = 1;
    ui->auth_state = AUTH_UI_REGISTER_ENTRY_PENDING;
}

/* 创建登录页全部控件，并建立 textarea、键盘、拼音 IME 和按钮事件之间的绑定关系。 */
static int create_login_page(
    lv_obj_t *screen,
    const lv_font_t *font,
    login_ui_t *ui,
    LoginContext *login)
{
    lv_obj_t *title;
    lv_obj_t *username_label;
    lv_obj_t *password_label;
    lv_obj_t *login_button;
    lv_obj_t *login_button_label;
    lv_obj_t *register_entry_button;
    lv_obj_t *register_entry_label;

    if(screen == NULL || font == NULL || ui == NULL || login == NULL) {
        return -1;
    }

    lv_obj_set_style_bg_color(screen, lv_color_hex(0xEAF3EF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);

    title = lv_label_create(screen);
    username_label = lv_label_create(screen);
    password_label = lv_label_create(screen);
    ui->username = lv_textarea_create(screen);
    ui->password = lv_textarea_create(screen);
    login_button = lv_btn_create(screen);
    login_button_label = lv_label_create(login_button);
    register_entry_button = lv_btn_create(screen);
    register_entry_label = lv_label_create(register_entry_button);
    ui->remember_button = lv_btn_create(screen);
    ui->remember_button_label = ui->remember_button == NULL
        ? NULL
        : lv_label_create(ui->remember_button);
    ui->pinyin_ime = lv_ime_pinyin_create(screen);
    ui->username_keyboard = lv_keyboard_create(screen);
    ui->password_keyboard = lv_keyboard_create(screen);
    ui->candidate_panel = NULL;
    if(ui->pinyin_ime != NULL) {
        ui->candidate_panel = lv_ime_pinyin_get_cand_panel(ui->pinyin_ime);
    }

    if(title == NULL || username_label == NULL || password_label == NULL ||
       ui->username == NULL || ui->password == NULL || login_button == NULL ||
       login_button_label == NULL || register_entry_button == NULL ||
       register_entry_label == NULL || ui->remember_button == NULL ||
       ui->remember_button_label == NULL ||
       ui->pinyin_ime == NULL ||
       ui->username_keyboard == NULL || ui->password_keyboard == NULL ||
       ui->candidate_panel == NULL) {
        fprintf(stderr, "failed to create login page widgets\n");
        return -1;
    }

    ui->login_button = login_button;
    ui->login_button_label = login_button_label;
    ui->register_entry_button = register_entry_button;
    ui->login = login;
    ui->font = font;
    ui->pending_home_user_id = 0;
    ui->home_transition_requested = 0;
    ui->register_transition_requested = 0;
    ui->message_box_close_requested = 0;
    ui->login_page_active = 1;

    lv_label_set_text(title, "医路通智慧医疗终端");
    lv_obj_set_style_text_font(title, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x0D5A4C), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    lv_label_set_text(username_label, "用户名");
    lv_obj_set_style_text_font(username_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(username_label, lv_color_hex(0x173C35), LV_PART_MAIN);
    lv_obj_set_pos(username_label, 44, 55);

    lv_textarea_set_one_line(ui->username, true);
    lv_textarea_set_max_length(ui->username, CLINIC_USERNAME_MAX_LENGTH);
    lv_textarea_set_placeholder_text(ui->username, "请输入用户名");
    lv_obj_set_pos(ui->username, 160, 43);
    lv_obj_set_size(ui->username, 600, 48);
    style_textarea(ui->username, font);

    lv_label_set_text(password_label, "密码");
    lv_obj_set_style_text_font(password_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(password_label, lv_color_hex(0x173C35), LV_PART_MAIN);
    lv_obj_set_pos(password_label, 44, 108);

    lv_textarea_set_one_line(ui->password, true);
    lv_textarea_set_max_length(ui->password, CLINIC_PASSWORD_MAX_LENGTH);
    lv_textarea_set_password_mode(ui->password, true);
    lv_textarea_set_placeholder_text(ui->password, "请输入密码");
    lv_obj_set_pos(ui->password, 160, 96);
    lv_obj_set_size(ui->password, 600, 48);
    style_textarea(ui->password, font);

    lv_obj_set_size(login_button, 180, 48);
    lv_obj_align(login_button, LV_ALIGN_TOP_MID, -110, 151);
    lv_obj_set_style_bg_color(login_button, lv_color_hex(0x0B5D8C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(login_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(login_button, lv_color_hex(0x04324B), LV_PART_MAIN);
    lv_obj_set_style_border_width(login_button, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(login_button, lv_color_hex(0x073E5D),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(login_button, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(login_button, 10, LV_PART_MAIN);

    lv_label_set_text(login_button_label, "登录");
    lv_obj_set_style_text_font(login_button_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(login_button_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(login_button_label);

    lv_obj_set_size(register_entry_button, 180, 48);
    lv_obj_align(register_entry_button, LV_ALIGN_TOP_MID, 110, 151);
    lv_obj_set_style_bg_color(register_entry_button, lv_color_hex(0x53736A),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(register_entry_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(register_entry_button, lv_color_hex(0x29473F),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(register_entry_button, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(register_entry_button, lv_color_hex(0x36544C),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(register_entry_button, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(register_entry_button, 10, LV_PART_MAIN);
    lv_label_set_text(register_entry_label, "注册账号");
    lv_obj_set_style_text_font(register_entry_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(register_entry_label, lv_color_hex(0xFFFFFF),
                                LV_PART_MAIN);
    lv_obj_center(register_entry_label);

    lv_obj_set_size(ui->remember_button, 240, 36);
    lv_obj_align(ui->remember_button, LV_ALIGN_TOP_MID, 0, 203);
    lv_obj_add_flag(ui->remember_button, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(
        ui->remember_button,
        lv_color_hex(0xE0ECE7),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(
        ui->remember_button,
        LV_OPA_COVER,
        LV_PART_MAIN);
    lv_obj_set_style_border_color(
        ui->remember_button,
        lv_color_hex(0x6F9186),
        LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->remember_button, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->remember_button, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        ui->remember_button,
        lv_color_hex(0x0D806B),
        LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(
        ui->remember_button,
        LV_OPA_COVER,
        LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_font(
        ui->remember_button_label,
        font,
        LV_PART_MAIN);
    lv_obj_set_style_text_color(
        ui->remember_button_label,
        lv_color_hex(0x173C35),
        LV_PART_MAIN);
    lv_obj_add_event_cb(
        ui->remember_button,
        remember_button_event_cb,
        LV_EVENT_VALUE_CHANGED,
        ui);
    update_remember_button_label(ui);

    lv_obj_clear_flag(ui->pinyin_ime, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(ui->pinyin_ime, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ui->pinyin_ime, 0, 0);
    lv_obj_set_style_text_font(ui->pinyin_ime, font, LV_PART_MAIN);

    lv_obj_set_size(ui->username_keyboard, DISPLAY_WIDTH, 200);
    lv_obj_align(ui->username_keyboard, LV_ALIGN_TOP_MID, 0, 280);
    style_keyboard(ui->username_keyboard);
    lv_keyboard_set_mode(ui->username_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(ui->username_keyboard, NULL);
    lv_obj_add_flag(ui->username_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_event_cb(ui->username_keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(ui->username_keyboard,
                        login_pinyin_keyboard_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        ui);

    lv_obj_set_size(ui->password_keyboard, DISPLAY_WIDTH, 200);
    lv_obj_align(ui->password_keyboard, LV_ALIGN_TOP_MID, 0, 280);
    style_keyboard(ui->password_keyboard);
    lv_keyboard_set_mode(ui->password_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(ui->password_keyboard, NULL);
    lv_obj_add_flag(ui->password_keyboard, LV_OBJ_FLAG_HIDDEN);

    lv_ime_pinyin_set_keyboard(ui->pinyin_ime, ui->username_keyboard);
    lv_obj_add_event_cb(ui->username_keyboard,
                        login_pinyin_candidate_guard_event_cb,
                        LV_EVENT_VALUE_CHANGED,
                        ui);
    lv_ime_pinyin_set_mode(ui->pinyin_ime, LV_IME_PINYIN_MODE_K26);
    lv_obj_set_size(ui->candidate_panel, DISPLAY_WIDTH, 40);
    lv_obj_align_to(ui->candidate_panel, ui->username_keyboard,
                    LV_ALIGN_OUT_TOP_MID, 0, 0);
    style_candidate_panel(ui->candidate_panel, font);
    lv_obj_add_flag(ui->candidate_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(ui->username, username_insert_cb, LV_EVENT_INSERT, ui);
    lv_obj_add_event_cb(ui->username, show_keyboard_cb, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->password, show_keyboard_cb, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->username_keyboard, keyboard_event_cb, LV_EVENT_READY, ui);
    lv_obj_add_event_cb(ui->username_keyboard, keyboard_event_cb, LV_EVENT_CANCEL, ui);
    lv_obj_add_event_cb(ui->password_keyboard, keyboard_event_cb, LV_EVENT_READY, ui);
    lv_obj_add_event_cb(ui->password_keyboard, keyboard_event_cb, LV_EVENT_CANCEL, ui);
    lv_obj_add_event_cb(login_button, login_clicked_cb, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(register_entry_button, register_entry_clicked_cb,
                        LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(screen, screen_clicked_cb, LV_EVENT_CLICKED, ui);

    load_remembered_credentials(ui);

    return 0;
}

static void clear_login_page_objects(login_ui_t *ui)
{
    if(ui == NULL) {
        return;
    }
    ui->username = NULL;
    ui->password = NULL;
    ui->username_keyboard = NULL;
    ui->password_keyboard = NULL;
    ui->pinyin_ime = NULL;
    ui->candidate_panel = NULL;
    ui->message_box = NULL;
    ui->active_input = NULL;
    ui->login_button = NULL;
    ui->login_button_label = NULL;
    ui->register_entry_button = NULL;
    ui->remember_button = NULL;
    ui->remember_button_label = NULL;
    ui->pending_home_user_id = 0;
    ui->home_transition_requested = 0;
    ui->register_transition_requested = 0;
    ui->message_box_close_requested = 0;
    ui->login_page_active = 0;
}

static int process_register_transition(
    lv_obj_t **screen,
    login_ui_t *ui,
    ClinicRegisterPage *register_page,
    RegisterUiController *register_controller)
{
    lv_obj_t *login_screen;
    lv_obj_t *register_screen;
    LoginContext *context;
    int busy;

    if(screen == NULL || *screen == NULL || ui == NULL ||
       register_page == NULL || register_controller == NULL ||
       register_controller->auth == NULL) {
        return -1;
    }
    if(!ui->register_transition_requested ||
       ui->auth_state != AUTH_UI_REGISTER_ENTRY_PENDING ||
       !ui->login_page_active) {
        return 0;
    }

    context = register_controller->auth;
    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    busy = context->state == LOGIN_STATE_RUNNING ||
        context->thread_pending_join;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }
    if(busy) {
        return 0;
    }

    login_screen = *screen;
    detach_login_input_bindings(ui);
    close_message_box(ui);
    if(ui->password != NULL && lv_obj_is_valid(ui->password)) {
        lv_textarea_set_text(ui->password, "");
    }

    register_screen = lv_obj_create(NULL);
    if(register_screen == NULL) {
        ui->auth_state = AUTH_UI_LOGIN_IDLE;
        ui->register_transition_requested = 0;
        return -1;
    }

    /* lv_ime_pinyin_create() creates its candidate panel under lv_scr_act(). */
    lv_scr_load(register_screen);
    if(clinic_register_page_create(
           register_page,
           register_screen,
           ui->font,
           register_submit_requested,
           register_back_requested,
           register_controller) != 0) {
        lv_scr_load(login_screen);
        if(lv_obj_is_valid(register_screen)) {
            lv_obj_del(register_screen);
        }
        clear_login_page_objects(ui);
        ui->auth_state = AUTH_UI_LOGIN_IDLE;
        return -1;
    }

    *screen = register_screen;
    if(lv_obj_is_valid(login_screen)) {
        lv_obj_del(login_screen);
    }
    clear_login_page_objects(ui);
    ui->auth_state = AUTH_UI_REGISTER_IDLE;
    return 0;
}

static int process_register_return(
    lv_obj_t **screen,
    login_ui_t *ui,
    ClinicRegisterPage *register_page,
    LoginContext *context,
    int *return_requested,
    char *registered_username,
    int *success_message_pending)
{
    lv_obj_t *register_screen;
    lv_obj_t *login_screen;
    int busy;
    int show_success;
    int credential_clear_failed = 0;

    if(screen == NULL || *screen == NULL || ui == NULL ||
       register_page == NULL || context == NULL || return_requested == NULL ||
       registered_username == NULL || success_message_pending == NULL) {
        return -1;
    }
    if(!*return_requested) {
        return 0;
    }
    if(ui->auth_state != AUTH_UI_REGISTER_RETURN_PENDING ||
       !clinic_register_page_is_active(register_page) ||
       register_page->screen != *screen) {
        return -1;
    }
    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    busy = context->state == LOGIN_STATE_RUNNING ||
        context->thread_pending_join;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }
    if(busy) {
        return 0;
    }

    register_screen = *screen;
    login_screen = lv_obj_create(NULL);
    if(login_screen == NULL) {
        return -1;
    }
    /* Keep the new login page's IME-owned objects on the new screen. */
    lv_scr_load(login_screen);
    if(create_login_page(login_screen, ui->font, ui, context) != 0) {
        lv_scr_load(register_screen);
        if(lv_obj_is_valid(login_screen)) {
            lv_obj_del(login_screen);
        }
        clear_login_page_objects(ui);
        return -1;
    }
    show_success = *success_message_pending;
    if(show_success) {
        credential_clear_failed = clinic_credential_store_remove(
            REMEMBERED_CREDENTIALS_PATH) != CLINIC_CREDENTIAL_STORE_OK;
        if(ui->remember_button != NULL &&
           lv_obj_is_valid(ui->remember_button)) {
            lv_obj_clear_state(ui->remember_button, LV_STATE_CHECKED);
            update_remember_button_label(ui);
        }
        if(registered_username[0] != '\0') {
            lv_textarea_set_text(ui->username, registered_username);
        }
        lv_textarea_set_text(ui->password, "");
    }

    clinic_register_page_cleanup(register_page);
    *screen = login_screen;
    if(lv_obj_is_valid(register_screen)) {
        lv_obj_del(register_screen);
    }
    *return_requested = 0;
    *success_message_pending = 0;
    ui->auth_state = AUTH_UI_LOGIN_IDLE;
    registered_username[0] = '\0';
    if(show_success &&
        show_validation_message(
            ui,
            credential_clear_failed
                ? "注册成功，但旧记住密码清除失败"
                : "注册成功，请登录") != 0) {
        return -1;
    }
    return 0;
}

/*
 * 登录成功后的切页动作集中在主循环执行：创建主页、加载新 screen、清理登录页引用，
 * 最后删除旧 screen。集中切页比在按钮回调里立即删除对象更容易闭合生命周期。
 */
static int process_home_transition(
    lv_obj_t **screen,
    login_ui_t *ui,
    ClinicHomePage *home_page,
    DepartmentUiController *department_controller,
    CurrentTicketUiController *current_ticket_controller,
    int64_t *authenticated_user_id)
{
    lv_obj_t *login_screen;
    lv_obj_t *home_screen;
    int64_t user_id;

    if(screen == NULL || *screen == NULL || ui == NULL || home_page == NULL ||
        department_controller == NULL || current_ticket_controller == NULL ||
        authenticated_user_id == NULL) {
        return -1;
    }
    if(!ui->home_transition_requested ||
       ui->auth_state != AUTH_UI_HOME_TRANSITION_PENDING ||
       !ui->login_page_active) {
        return 0;
    }

    user_id = ui->pending_home_user_id;
    ui->home_transition_requested = 0;
    if(user_id <= 0) {
        return -1;
    }

    home_screen = lv_obj_create(NULL);
    if(home_screen == NULL ||
       clinic_home_page_create(
           home_page,
           home_screen,
           ui->font,
           user_id,
           department_request_clicked,
           department_controller,
           current_ticket_request_clicked,
           current_ticket_controller) != 0) {
        if(home_screen != NULL && lv_obj_is_valid(home_screen)) {
            clinic_home_page_cleanup(home_page);
            lv_obj_del(home_screen);
        }
        return -1;
    }

    detach_login_input_bindings(ui);
    close_message_box(ui);
    login_screen = *screen;
    lv_scr_load(home_screen);
    *screen = home_screen;
    *authenticated_user_id = user_id;
    if(lv_obj_is_valid(login_screen)) {
        lv_obj_del(login_screen);
    }
    clear_login_page_objects(ui);
    ui->auth_state = AUTH_UI_HOME_ACTIVE;
    return 0;
}

static void request_department_return(void *user_data)
{
    int *return_requested = user_data;

    if(return_requested != NULL) {
        *return_requested = 1;
    }
}

static int enter_department_page(
    lv_obj_t **screen,
    const lv_font_t *font,
    ClinicHomePage *home_page,
    ClinicDepartmentPage *department_page,
    ClinicServiceFlow flow,
    const ClinicDepartmentListResult *result,
    DoctorUiController *doctor_controller,
    TicketUiController *ticket_controller,
    int *return_requested)
{
    lv_obj_t *home_screen;
    lv_obj_t *department_screen;
    ClinicDepartmentSelectCallback select_callback = 0;
    void *select_user_data = NULL;

    if(screen == NULL || *screen == NULL || font == NULL ||
       home_page == NULL || department_page == NULL || result == NULL ||
       !clinic_service_flow_is_valid(flow) ||
       doctor_controller == NULL || ticket_controller == NULL ||
       return_requested == NULL ||
       home_page->screen != *screen) {
        return -1;
    }
    if(flow == CLINIC_SERVICE_FLOW_DOCTOR_QUERY) {
        select_callback = doctor_request_clicked;
        select_user_data = doctor_controller;
    }
    else if(flow == CLINIC_SERVICE_FLOW_TICKET) {
        select_callback = ticket_request_clicked;
        select_user_data = ticket_controller;
    }

    department_screen = lv_obj_create(NULL);
    if(department_screen == NULL ||
       clinic_department_page_create(
           department_page,
           department_screen,
           font,
           flow,
           result->departments,
           result->department_count,
           select_callback,
           select_user_data,
           request_department_return,
           return_requested) != 0) {
        clinic_department_page_cleanup(department_page);
        if(department_screen != NULL && lv_obj_is_valid(department_screen)) {
            lv_obj_del(department_screen);
        }
        return -1;
    }

    home_screen = *screen;
    clinic_home_page_cleanup(home_page);
    lv_scr_load(department_screen);
    *screen = department_screen;
    if(lv_obj_is_valid(home_screen)) {
        lv_obj_del(home_screen);
    }
    return 0;
}

static int process_department_result(
    lv_obj_t **screen,
    const lv_font_t *font,
    DepartmentRequestContext *context,
    ClinicHomePage *home_page,
    ClinicDepartmentPage *department_page,
    DoctorUiController *doctor_controller,
    TicketUiController *ticket_controller,
    ClinicDepartmentListResult *department_cache,
    int *department_cache_valid,
    int *return_requested)
{
    DepartmentRequestState state;
    pthread_t worker;
    int thread_pending_join;
    int join_result;
    ClinicDepartmentListResult result;
    ClinicServiceFlow flow;
    const char *message;

    if(screen == NULL || font == NULL || context == NULL ||
       home_page == NULL || department_page == NULL ||
       doctor_controller == NULL || ticket_controller == NULL ||
       department_cache == NULL ||
       department_cache_valid == NULL || return_requested == NULL) {
        return -1;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    state = context->state;
    worker = context->worker;
    thread_pending_join = context->thread_pending_join;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(!thread_pending_join || state == DEPARTMENT_REQUEST_RUNNING) {
        return 0;
    }
    if(state != DEPARTMENT_REQUEST_FINISHED) {
        return -1;
    }

    join_result = pthread_join(worker, NULL);
    if(join_result != 0) {
        fprintf(stderr, "failed to join department worker: %d\n", join_result);
        return -1;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    context->thread_pending_join = 0;
    context->state = DEPARTMENT_REQUEST_IDLE;
    result = context->result;
    flow = context->flow;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(home_page->screen == NULL || home_page->screen != *screen) {
        return -1;
    }
    clinic_home_page_set_department_loading(home_page, 0);

    if(result.outcome == CLINIC_DEPARTMENT_LIST_SUCCESS &&
       clinic_service_flow_is_valid(flow)) {
        *department_cache = result;
        *department_cache_valid = 1;
        *return_requested = 0;
        return enter_department_page(
            screen,
            font,
            home_page,
            department_page,
            flow,
            &result,
            doctor_controller,
            ticket_controller,
            return_requested);
    }

    if(result.outcome == CLINIC_DEPARTMENT_LIST_NETWORK_ERROR) {
        message = "无法连接服务器";
    }
    else if(result.outcome == CLINIC_DEPARTMENT_LIST_SERVER_ERROR) {
        message = "获取科室失败";
    }
    else {
        message = "科室数据异常";
    }
    return clinic_home_page_show_message(home_page, message);
}

static int process_department_return(
    lv_obj_t **screen,
    const lv_font_t *font,
    int64_t authenticated_user_id,
    ClinicHomePage *home_page,
    ClinicDepartmentPage *department_page,
    DepartmentUiController *department_controller,
    CurrentTicketUiController *current_ticket_controller,
    int *return_requested)
{
    lv_obj_t *department_screen;
    lv_obj_t *home_screen;

    if(screen == NULL || font == NULL || home_page == NULL ||
       department_page == NULL || department_controller == NULL ||
       current_ticket_controller == NULL || return_requested == NULL) {
        return -1;
    }
    if(!*return_requested) {
        return 0;
    }
    *return_requested = 0;
    if(*screen == NULL || department_page->screen != *screen ||
       authenticated_user_id <= 0) {
        return -1;
    }

    home_screen = lv_obj_create(NULL);
    if(home_screen == NULL ||
       clinic_home_page_create(
           home_page,
           home_screen,
           font,
           authenticated_user_id,
           department_request_clicked,
           department_controller,
           current_ticket_request_clicked,
           current_ticket_controller) != 0) {
        if(home_screen != NULL && lv_obj_is_valid(home_screen)) {
            clinic_home_page_cleanup(home_page);
            lv_obj_del(home_screen);
        }
        return -1;
    }

    department_screen = *screen;
    clinic_department_page_cleanup(department_page);
    lv_scr_load(home_screen);
    *screen = home_screen;
    if(lv_obj_is_valid(department_screen)) {
        lv_obj_del(department_screen);
    }
    return 0;
}

static void request_doctor_return(void *user_data)
{
    int *return_requested = user_data;

    if(return_requested != NULL) {
        *return_requested = 1;
    }
}

static int enter_doctor_page(
    lv_obj_t **screen,
    const lv_font_t *font,
    ClinicDepartmentPage *department_page,
    ClinicDoctorPage *doctor_page,
    const char *department_name,
    const ClinicDoctorListResult *result,
    int *return_requested)
{
    lv_obj_t *department_screen;
    lv_obj_t *doctor_screen;

    if(screen == NULL || *screen == NULL || font == NULL ||
       department_page == NULL || doctor_page == NULL ||
       department_name == NULL ||
       result == NULL || return_requested == NULL ||
       department_page->screen != *screen) {
        return -1;
    }

    doctor_screen = lv_obj_create(NULL);
    if(doctor_screen == NULL ||
       clinic_doctor_page_create(
           doctor_page,
           doctor_screen,
           font,
           department_name,
           result->doctors,
           result->doctor_count,
           request_doctor_return,
           return_requested) != 0) {
        clinic_doctor_page_cleanup(doctor_page);
        if(doctor_screen != NULL && lv_obj_is_valid(doctor_screen)) {
            lv_obj_del(doctor_screen);
        }
        return -1;
    }

    department_screen = *screen;
    clinic_department_page_cleanup(department_page);
    lv_scr_load(doctor_screen);
    *screen = doctor_screen;
    if(lv_obj_is_valid(department_screen)) {
        lv_obj_del(department_screen);
    }
    return 0;
}

static int process_doctor_result(
    lv_obj_t **screen,
    const lv_font_t *font,
    DoctorRequestContext *context,
    ClinicDepartmentPage *department_page,
    ClinicDoctorPage *doctor_page,
    int *return_requested)
{
    DoctorRequestState state;
    pthread_t worker;
    int thread_pending_join;
    int join_result;
    ClinicDoctorListResult result;
    char department_name[CLINIC_DEPARTMENT_NAME_MAX_LENGTH + 1U];
    const char *message;

    if(screen == NULL || font == NULL || context == NULL ||
       department_page == NULL || doctor_page == NULL ||
       return_requested == NULL) {
        return -1;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    state = context->state;
    worker = context->worker;
    thread_pending_join = context->thread_pending_join;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(!thread_pending_join || state == DOCTOR_REQUEST_RUNNING) {
        return 0;
    }
    if(state != DOCTOR_REQUEST_FINISHED) {
        return -1;
    }

    join_result = pthread_join(worker, NULL);
    if(join_result != 0) {
        fprintf(stderr, "failed to join doctor worker: %d\n", join_result);
        return -1;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    context->thread_pending_join = 0;
    context->state = DOCTOR_REQUEST_IDLE;
    result = context->result;
    if(copy_string_checked(
           department_name,
           sizeof(department_name),
           context->department_name,
           CLINIC_DEPARTMENT_NAME_MAX_LENGTH) != 0) {
        department_name[0] = '\0';
        result.outcome = CLINIC_DOCTOR_LIST_PROTOCOL_ERROR;
    }
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(department_page->screen == NULL || department_page->screen != *screen) {
        return -1;
    }
    clinic_department_page_set_request_loading(department_page, 0);

    if(result.outcome == CLINIC_DOCTOR_LIST_SUCCESS &&
       department_name[0] != '\0') {
        *return_requested = 0;
        return enter_doctor_page(
            screen,
            font,
            department_page,
            doctor_page,
            department_name,
            &result,
            return_requested);
    }

    if(result.outcome == CLINIC_DOCTOR_LIST_NETWORK_ERROR) {
        message = "无法连接服务器";
    }
    else if(result.outcome == CLINIC_DOCTOR_LIST_SERVER_ERROR) {
        message = "获取医生失败";
    }
    else {
        message = "医生数据异常";
    }
    return clinic_department_page_show_message(department_page, message);
}

static int process_doctor_return(
    lv_obj_t **screen,
    const lv_font_t *font,
    int64_t authenticated_user_id,
    ClinicDepartmentPage *department_page,
    ClinicDoctorPage *doctor_page,
    DoctorUiController *doctor_controller,
    const ClinicDepartmentListResult *department_cache,
    int department_cache_valid,
    int *department_return_requested,
    int *doctor_return_requested)
{
    lv_obj_t *doctor_screen;
    lv_obj_t *department_screen;

    if(screen == NULL || font == NULL || department_page == NULL ||
       doctor_page == NULL || doctor_controller == NULL ||
       department_cache == NULL || department_return_requested == NULL ||
       doctor_return_requested == NULL) {
        return -1;
    }
    if(!*doctor_return_requested) {
        return 0;
    }
    *doctor_return_requested = 0;
    if(*screen == NULL || doctor_page->screen != *screen ||
       authenticated_user_id <= 0 || !department_cache_valid ||
       department_cache->outcome != CLINIC_DEPARTMENT_LIST_SUCCESS) {
        return -1;
    }

    department_screen = lv_obj_create(NULL);
    if(department_screen == NULL ||
       clinic_department_page_create(
           department_page,
           department_screen,
           font,
           CLINIC_SERVICE_FLOW_DOCTOR_QUERY,
           department_cache->departments,
           department_cache->department_count,
           doctor_request_clicked,
           doctor_controller,
           request_department_return,
           department_return_requested) != 0) {
        clinic_department_page_cleanup(department_page);
        if(department_screen != NULL && lv_obj_is_valid(department_screen)) {
            lv_obj_del(department_screen);
        }
        return -1;
    }

    *department_return_requested = 0;
    doctor_screen = *screen;
    clinic_doctor_page_cleanup(doctor_page);
    lv_scr_load(department_screen);
    *screen = department_screen;
    if(lv_obj_is_valid(doctor_screen)) {
        lv_obj_del(doctor_screen);
    }
    return 0;
}

static void request_ticket_return(void *user_data)
{
    int *return_requested = user_data;

    if(return_requested != NULL) {
        *return_requested = 1;
    }
}

static int enter_ticket_page(
    lv_obj_t **screen,
    const lv_font_t *font,
    ClinicDepartmentPage *department_page,
    ClinicTicketPage *ticket_page,
    const ClinicTicketCreateResult *result,
    const char *department_name,
    int *return_requested)
{
    lv_obj_t *department_screen;
    lv_obj_t *ticket_screen;

    if(screen == NULL || *screen == NULL || font == NULL ||
       department_page == NULL || ticket_page == NULL || result == NULL ||
       department_name == NULL || return_requested == NULL ||
       department_page->screen != *screen) {
        return -1;
    }

    ticket_screen = lv_obj_create(NULL);
    if(ticket_screen == NULL ||
       clinic_ticket_page_create(
           ticket_page,
           ticket_screen,
            font,
            &result->ticket,
            department_name,
            result->outcome == CLINIC_TICKET_CREATE_EXISTING,
            request_ticket_return,
           return_requested) != 0) {
        clinic_ticket_page_cleanup(ticket_page);
        if(ticket_screen != NULL && lv_obj_is_valid(ticket_screen)) {
            lv_obj_del(ticket_screen);
        }
        return -1;
    }

    department_screen = *screen;
    clinic_department_page_cleanup(department_page);
    lv_scr_load(ticket_screen);
    *screen = ticket_screen;
    if(lv_obj_is_valid(department_screen)) {
        lv_obj_del(department_screen);
    }
    if(result->outcome == CLINIC_TICKET_CREATE_EXISTING &&
       clinic_ticket_page_show_existing_notice(ticket_page) != 0) {
        fprintf(stderr, "failed to show existing ticket notice\n");
    }
    return 0;
}

static int process_ticket_result(
    lv_obj_t **screen,
    const lv_font_t *font,
    TicketRequestContext *context,
    ClinicDepartmentPage *department_page,
    ClinicTicketPage *ticket_page,
    int *return_requested)
{
    TicketRequestState state;
    pthread_t worker;
    int thread_pending_join;
    int join_result;
    ClinicTicketCreateResult result;
    char department_name[CLINIC_DEPARTMENT_NAME_MAX_LENGTH + 1U];
    const char *message;

    if(screen == NULL || font == NULL || context == NULL ||
       department_page == NULL || ticket_page == NULL ||
       return_requested == NULL) {
        return -1;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    state = context->state;
    worker = context->worker;
    thread_pending_join = context->thread_pending_join;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(!thread_pending_join || state == TICKET_REQUEST_RUNNING) {
        return 0;
    }
    if(state != TICKET_REQUEST_FINISHED) {
        return -1;
    }

    join_result = pthread_join(worker, NULL);
    if(join_result != 0) {
        fprintf(stderr, "failed to join ticket worker: %d\n", join_result);
        return -1;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    context->thread_pending_join = 0;
    context->state = TICKET_REQUEST_IDLE;
    result = context->result;
    if(copy_string_checked(
           department_name,
           sizeof(department_name),
           context->department_name,
           CLINIC_DEPARTMENT_NAME_MAX_LENGTH) != 0) {
        department_name[0] = '\0';
        result.outcome = CLINIC_TICKET_CREATE_PROTOCOL_ERROR;
    }
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(department_page->screen == NULL || department_page->screen != *screen) {
        return -1;
    }
    clinic_department_page_set_request_loading(department_page, 0);

    if((result.outcome == CLINIC_TICKET_CREATE_SUCCESS ||
        result.outcome == CLINIC_TICKET_CREATE_EXISTING) &&
       department_name[0] != '\0') {
        *return_requested = 0;
        return enter_ticket_page(
            screen,
            font,
            department_page,
            ticket_page,
            &result,
            department_name,
            return_requested);
    }

    if(result.outcome == CLINIC_TICKET_CREATE_NETWORK_ERROR) {
        message = "无法连接服务器";
    }
    else if(result.outcome == CLINIC_TICKET_CREATE_SERVER_ERROR) {
        message = "取号失败";
    }
    else {
        message = "号单数据异常";
    }
    return clinic_department_page_show_message(department_page, message);
}

static int process_ticket_return(
    lv_obj_t **screen,
    const lv_font_t *font,
    int64_t authenticated_user_id,
    ClinicHomePage *home_page,
    ClinicTicketPage *ticket_page,
    DepartmentUiController *department_controller,
    CurrentTicketUiController *current_ticket_controller,
    int *department_return_requested,
    int *doctor_return_requested,
    int *ticket_return_requested)
{
    lv_obj_t *ticket_screen;
    lv_obj_t *home_screen;

    if(screen == NULL || font == NULL || home_page == NULL ||
       ticket_page == NULL || department_controller == NULL ||
       current_ticket_controller == NULL ||
       department_return_requested == NULL || doctor_return_requested == NULL ||
       ticket_return_requested == NULL) {
        return -1;
    }
    if(!*ticket_return_requested) {
        return 0;
    }
    *ticket_return_requested = 0;
    if(*screen == NULL || ticket_page->screen != *screen ||
       authenticated_user_id <= 0) {
        return -1;
    }

    home_screen = lv_obj_create(NULL);
    if(home_screen == NULL ||
       clinic_home_page_create(
           home_page,
           home_screen,
           font,
           authenticated_user_id,
           department_request_clicked,
           department_controller,
           current_ticket_request_clicked,
           current_ticket_controller) != 0) {
        if(home_screen != NULL && lv_obj_is_valid(home_screen)) {
            clinic_home_page_cleanup(home_page);
            lv_obj_del(home_screen);
        }
        return -1;
    }

    *department_return_requested = 0;
    *doctor_return_requested = 0;
    ticket_screen = *screen;
    clinic_ticket_page_cleanup(ticket_page);
    lv_scr_load(home_screen);
    *screen = home_screen;
    if(lv_obj_is_valid(ticket_screen)) {
        lv_obj_del(ticket_screen);
    }
    return 0;
}

static void request_queue_return(void *user_data)
{
    int *return_requested = user_data;

    if(return_requested != NULL) {
        *return_requested = 1;
    }
}

static int enter_queue_page(
    lv_obj_t **screen,
    const lv_font_t *font,
    ClinicHomePage *home_page,
    ClinicQueuePage *queue_page,
    const ClinicCurrentTicketResult *result,
    CurrentTicketUiController *current_ticket_controller,
    int *return_requested)
{
    lv_obj_t *home_screen;
    lv_obj_t *queue_screen;

    if(screen == NULL || *screen == NULL || font == NULL ||
       home_page == NULL || queue_page == NULL || result == NULL ||
       current_ticket_controller == NULL || return_requested == NULL ||
       home_page->screen != *screen) {
        return -1;
    }

    queue_screen = lv_obj_create(NULL);
    if(queue_screen == NULL ||
       clinic_queue_page_create(
           queue_page,
           queue_screen,
           font,
           &result->ticket,
           &result->queue_summary,
           current_ticket_refresh_clicked,
           current_ticket_controller,
           request_queue_return,
           return_requested) != 0) {
        clinic_queue_page_cleanup(queue_page);
        if(queue_screen != NULL && lv_obj_is_valid(queue_screen)) {
            lv_obj_del(queue_screen);
        }
        return -1;
    }

    home_screen = *screen;
    clinic_home_page_cleanup(home_page);
    lv_scr_load(queue_screen);
    *screen = queue_screen;
    if(lv_obj_is_valid(home_screen)) {
        lv_obj_del(home_screen);
    }
    return 0;
}

static int process_current_ticket_result(
    lv_obj_t **screen,
    const lv_font_t *font,
    CurrentTicketUiController *controller,
    int *return_requested)
{
    CurrentTicketRequestContext *context;
    ClinicHomePage *home_page;
    ClinicQueuePage *queue_page;
    CurrentTicketRequestState state;
    CurrentTicketRequestSource source;
    pthread_t worker;
    int thread_pending_join;
    int join_result;
    ClinicCurrentTicketResult result;
    const char *message;

    if(screen == NULL || font == NULL || controller == NULL ||
       controller->request == NULL || controller->home_page == NULL ||
       controller->queue_page == NULL || return_requested == NULL) {
        return -1;
    }
    context = controller->request;
    home_page = controller->home_page;
    queue_page = controller->queue_page;

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    state = context->state;
    worker = context->worker;
    thread_pending_join = context->thread_pending_join;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(!thread_pending_join || state == CURRENT_TICKET_REQUEST_RUNNING) {
        return 0;
    }
    if(state != CURRENT_TICKET_REQUEST_FINISHED) {
        return -1;
    }

    join_result = pthread_join(worker, NULL);
    if(join_result != 0) {
        fprintf(
            stderr,
            "failed to join current ticket worker: %d\n",
            join_result);
        return -1;
    }

    if(pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    context->thread_pending_join = 0;
    context->state = CURRENT_TICKET_REQUEST_IDLE;
    source = context->source;
    context->source = CURRENT_TICKET_REQUEST_SOURCE_NONE;
    result = context->result;
    if(pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if(source == CURRENT_TICKET_REQUEST_SOURCE_HOME) {
        if(home_page->screen == NULL || home_page->screen != *screen) {
            return -1;
        }
        clinic_home_page_set_current_ticket_loading(home_page, 0);
        if(result.outcome == CLINIC_CURRENT_TICKET_SUCCESS) {
            *return_requested = 0;
            return enter_queue_page(
                screen,
                font,
                home_page,
                queue_page,
                &result,
                controller,
                return_requested);
        }
        if(result.outcome == CLINIC_CURRENT_TICKET_NO_TICKET) {
            message = "当前没有号单";
        }
        else if(result.outcome == CLINIC_CURRENT_TICKET_NETWORK_ERROR) {
            message = "无法连接服务器";
        }
        else if(result.outcome == CLINIC_CURRENT_TICKET_SERVER_ERROR) {
            message = "查询号单失败";
        }
        else {
            message = "号单数据异常";
        }
        return clinic_home_page_show_message(home_page, message);
    }

    if(source != CURRENT_TICKET_REQUEST_SOURCE_QUEUE ||
       queue_page->screen == NULL || queue_page->screen != *screen) {
        return -1;
    }
    clinic_queue_page_set_refreshing(queue_page, 0);
    if(result.outcome == CLINIC_CURRENT_TICKET_SUCCESS) {
        return clinic_queue_page_update_ticket(
            queue_page,
            &result.ticket,
            &result.queue_summary);
    }
    if(result.outcome == CLINIC_CURRENT_TICKET_NO_TICKET) {
        return clinic_queue_page_show_no_ticket(queue_page);
    }
    if(result.outcome == CLINIC_CURRENT_TICKET_NETWORK_ERROR) {
        message = "无法连接服务器";
    }
    else if(result.outcome == CLINIC_CURRENT_TICKET_SERVER_ERROR) {
        message = "查询号单失败";
    }
    else {
        message = "号单数据异常";
    }
    return clinic_queue_page_show_message(queue_page, message);
}

static int process_queue_return(
    lv_obj_t **screen,
    const lv_font_t *font,
    int64_t authenticated_user_id,
    ClinicHomePage *home_page,
    ClinicQueuePage *queue_page,
    DepartmentUiController *department_controller,
    CurrentTicketUiController *current_ticket_controller,
    int *return_requested)
{
    lv_obj_t *queue_screen;
    lv_obj_t *home_screen;

    if(screen == NULL || font == NULL || home_page == NULL ||
       queue_page == NULL || department_controller == NULL ||
       current_ticket_controller == NULL || return_requested == NULL) {
        return -1;
    }
    if(!*return_requested) {
        return 0;
    }
    *return_requested = 0;
    if(*screen == NULL || queue_page->screen != *screen ||
       authenticated_user_id <= 0) {
        return -1;
    }

    home_screen = lv_obj_create(NULL);
    if(home_screen == NULL ||
       clinic_home_page_create(
           home_page,
           home_screen,
           font,
           authenticated_user_id,
           department_request_clicked,
           department_controller,
           current_ticket_request_clicked,
           current_ticket_controller) != 0) {
        if(home_screen != NULL && lv_obj_is_valid(home_screen)) {
            clinic_home_page_cleanup(home_page);
            lv_obj_del(home_screen);
        }
        return -1;
    }

    queue_screen = *screen;
    clinic_queue_page_cleanup(queue_page);
    lv_scr_load(home_screen);
    *screen = home_screen;
    if(lv_obj_is_valid(queue_screen)) {
        lv_obj_del(queue_screen);
    }
    return 0;
}

/*
 * 板端程序总入口。
 * 初始化顺序：参数与共享上下文 -> mutex -> LVGL -> framebuffer -> 触摸 -> 字体 -> 登录页。
 * 主循环每轮先让 LVGL 处理触摸/定时器，再依次消费各 worker 结果和页面切换请求。
 * cleanup 路径则反向等待线程、清理页面、销毁 mutex 和字体，保证资源生命周期闭合。
 */
int main(int argc, char **argv)
{
    static lv_color_t draw_buffer_pixels[DISPLAY_WIDTH * DRAW_BUFFER_ROWS];
    lv_disp_draw_buf_t draw_buffer;
    lv_disp_drv_t display_driver;
    lv_indev_drv_t input_driver;
    lv_ft_info_t font_info;
    login_ui_t ui = {0};
    ClinicRegisterPage register_page = {0};
    ClinicHomePage home_page = {0};
    ClinicDepartmentPage department_page = {0};
    ClinicDoctorPage doctor_page = {0};
    ClinicTicketPage ticket_page = {0};
    ClinicQueuePage queue_page = {0};
    LoginContext login = {0};
    RegisterUiController register_controller = {0};
    DepartmentRequestContext department_request = {0};
    DepartmentUiController department_controller = {0};
    DoctorRequestContext doctor_request = {0};
    DoctorUiController doctor_controller = {0};
    TicketRequestContext ticket_request = {0};
    TicketUiController ticket_controller = {0};
    CurrentTicketRequestContext current_ticket_request = {0};
    CurrentTicketUiController current_ticket_controller = {0};
    ClinicDepartmentListResult department_cache = {0};
    char registered_username[CLINIC_USERNAME_MAX_LENGTH + 1U] = {0};
    lv_obj_t *screen = NULL;
    const char *server_ip;
    const char *server_port;
    int login_mutex_initialized = 0;
    int department_mutex_initialized = 0;
    int doctor_mutex_initialized = 0;
    int ticket_mutex_initialized = 0;
    int current_ticket_mutex_initialized = 0;
    int font_initialized = 0;
    int department_cache_valid = 0;
    int department_return_requested = 0;
    int doctor_return_requested = 0;
    int ticket_return_requested = 0;
    int queue_return_requested = 0;
    int register_return_requested = 0;
    int register_success_message_pending = 0;
    int64_t authenticated_user_id = 0;
    int exit_code = EXIT_FAILURE;

    ui.auth_state = AUTH_UI_LOGIN_IDLE;
    department_controller.request = &department_request;
    department_controller.home_page = &home_page;
    doctor_controller.request = &doctor_request;
    doctor_controller.department_page = &department_page;
    doctor_controller.department_return_requested =
        &department_return_requested;
    ticket_controller.request = &ticket_request;
    ticket_controller.department_page = &department_page;
    ticket_controller.authenticated_user_id = &authenticated_user_id;
    ticket_controller.department_return_requested =
        &department_return_requested;
    current_ticket_controller.request = &current_ticket_request;
    current_ticket_controller.home_page = &home_page;
    current_ticket_controller.queue_page = &queue_page;
    current_ticket_controller.authenticated_user_id = &authenticated_user_id;
    register_controller.auth = &login;
    register_controller.page = &register_page;
    register_controller.return_requested = &register_return_requested;
    register_controller.login_ui = &ui;

    if(argc == 1) {
        server_ip = DEFAULT_SERVER_IP;
        server_port = DEFAULT_SERVER_PORT;
    }
    else if(argc == 3 && argv[1][0] != '\0' && port_is_valid(argv[2])) {
        server_ip = argv[1];
        server_port = argv[2];
    }
    else {
        fprintf(stderr, "usage: %s [<server_ip> <port>]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if(copy_string_checked(
           login.server_ip,
           sizeof(login.server_ip),
           server_ip,
           SERVER_IP_MAX_LENGTH) != 0 ||
       copy_string_checked(
           login.server_port,
           sizeof(login.server_port),
           server_port,
           SERVER_PORT_MAX_LENGTH) != 0 ||
       copy_string_checked(
           department_request.server_ip,
           sizeof(department_request.server_ip),
           server_ip,
           SERVER_IP_MAX_LENGTH) != 0 ||
       copy_string_checked(
           department_request.server_port,
           sizeof(department_request.server_port),
           server_port,
           SERVER_PORT_MAX_LENGTH) != 0 ||
       copy_string_checked(
           doctor_request.server_ip,
           sizeof(doctor_request.server_ip),
           server_ip,
           SERVER_IP_MAX_LENGTH) != 0 ||
       copy_string_checked(
           doctor_request.server_port,
           sizeof(doctor_request.server_port),
           server_port,
           SERVER_PORT_MAX_LENGTH) != 0 ||
       copy_string_checked(
           ticket_request.server_ip,
           sizeof(ticket_request.server_ip),
           server_ip,
           SERVER_IP_MAX_LENGTH) != 0 ||
       copy_string_checked(
           ticket_request.server_port,
           sizeof(ticket_request.server_port),
           server_port,
           SERVER_PORT_MAX_LENGTH) != 0 ||
       copy_string_checked(
           current_ticket_request.server_ip,
           sizeof(current_ticket_request.server_ip),
           server_ip,
           SERVER_IP_MAX_LENGTH) != 0 ||
       copy_string_checked(
           current_ticket_request.server_port,
           sizeof(current_ticket_request.server_port),
           server_port,
           SERVER_PORT_MAX_LENGTH) != 0) {
        fprintf(stderr, "invalid server address\n");
        return EXIT_FAILURE;
    }
    login.state = LOGIN_STATE_IDLE;
    login.operation = AUTH_OPERATION_NONE;
    department_request.state = DEPARTMENT_REQUEST_IDLE;
    department_request.flow = CLINIC_SERVICE_FLOW_DEPARTMENT_QUERY;
    doctor_request.state = DOCTOR_REQUEST_IDLE;
    ticket_request.state = TICKET_REQUEST_IDLE;
    current_ticket_request.state = CURRENT_TICKET_REQUEST_IDLE;

    if(install_signal_handlers() != 0) {
        return EXIT_FAILURE;
    }
    if(pthread_mutex_init(&login.mutex, NULL) != 0) {
        fprintf(stderr, "failed to initialize login mutex\n");
        return EXIT_FAILURE;
    }
    login_mutex_initialized = 1;
    if(pthread_mutex_init(&department_request.mutex, NULL) != 0) {
        fprintf(stderr, "failed to initialize department mutex\n");
        (void)pthread_mutex_destroy(&login.mutex);
        return EXIT_FAILURE;
    }
    department_mutex_initialized = 1;
    if(pthread_mutex_init(&doctor_request.mutex, NULL) != 0) {
        fprintf(stderr, "failed to initialize doctor mutex\n");
        (void)pthread_mutex_destroy(&department_request.mutex);
        (void)pthread_mutex_destroy(&login.mutex);
        return EXIT_FAILURE;
    }
    doctor_mutex_initialized = 1;
    if(pthread_mutex_init(&ticket_request.mutex, NULL) != 0) {
        fprintf(stderr, "failed to initialize ticket mutex\n");
        (void)pthread_mutex_destroy(&doctor_request.mutex);
        (void)pthread_mutex_destroy(&department_request.mutex);
        (void)pthread_mutex_destroy(&login.mutex);
        return EXIT_FAILURE;
    }
    ticket_mutex_initialized = 1;
    if(pthread_mutex_init(&current_ticket_request.mutex, NULL) != 0) {
        fprintf(stderr, "failed to initialize current ticket mutex\n");
        (void)pthread_mutex_destroy(&ticket_request.mutex);
        (void)pthread_mutex_destroy(&doctor_request.mutex);
        (void)pthread_mutex_destroy(&department_request.mutex);
        (void)pthread_mutex_destroy(&login.mutex);
        return EXIT_FAILURE;
    }
    current_ticket_mutex_initialized = 1;

    /* 从这里开始初始化只允许主线程使用的 LVGL、显示和输入设备。 */
    lv_init();

    fbdev_init();
    lv_disp_draw_buf_init(&draw_buffer, draw_buffer_pixels, NULL,
                          DISPLAY_WIDTH * DRAW_BUFFER_ROWS);
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = DISPLAY_WIDTH;
    display_driver.ver_res = DISPLAY_HEIGHT;
    display_driver.flush_cb = fbdev_flush;
    display_driver.draw_buf = &draw_buffer;
    if(lv_disp_drv_register(&display_driver) == NULL) {
        fprintf(stderr, "failed to register framebuffer display driver\n");
        goto cleanup;
    }

    evdev_init();
    lv_indev_drv_init(&input_driver);
    input_driver.type = LV_INDEV_TYPE_POINTER;
    input_driver.read_cb = evdev_read;
    if(lv_indev_drv_register(&input_driver) == NULL) {
        fprintf(stderr, "failed to register evdev input driver\n");
        goto cleanup;
    }

    font_info.name = FONT_PATH;
    font_info.weight = FONT_SIZE;
    font_info.style = FT_FONT_STYLE_NORMAL;
    font_info.mem = NULL;
    if(!lv_ft_font_init(&font_info)) {
        fprintf(stderr, "failed to initialize FreeType font: %s\n", FONT_PATH);
        goto cleanup;
    }
    font_initialized = 1;

    screen = lv_scr_act();
    if(screen == NULL ||
       create_login_page(screen, font_info.font, &ui, &login) != 0) {
        goto cleanup;
    }

    exit_code = EXIT_SUCCESS;
    /*
     * 这是唯一的 LVGL 主循环。所有 process_* 函数都在本线程调用，
     * 因此页面更新、screen 切换和对象删除不会发生在网络线程中。
     */
    while(keep_running) {
        lv_timer_handler();
        process_message_box_close(&ui);
        if(process_login_result(&ui) != 0) {
            fprintf(stderr, "failed to process login result\n");
            exit_code = EXIT_FAILURE;
            break;
        }
        if(process_register_result(
               &ui,
               &register_page,
               &login,
               registered_username,
               sizeof(registered_username),
               &register_return_requested,
               &register_success_message_pending) != 0) {
            fprintf(stderr, "failed to process register result\n");
            exit_code = EXIT_FAILURE;
            break;
        }
        if(process_register_transition(
               &screen,
               &ui,
               &register_page,
               &register_controller) != 0) {
            fprintf(stderr, "failed to enter register page\n");
            exit_code = EXIT_FAILURE;
            break;
        }
        if(process_register_return(
               &screen,
               &ui,
               &register_page,
               &login,
               &register_return_requested,
               registered_username,
               &register_success_message_pending) != 0) {
            fprintf(stderr, "failed to return to login page\n");
            exit_code = EXIT_FAILURE;
            break;
        }
        if(process_home_transition(
               &screen,
               &ui,
               &home_page,
               &department_controller,
               &current_ticket_controller,
               &authenticated_user_id) != 0) {
            fprintf(stderr, "failed to enter home page\n");
            exit_code = EXIT_FAILURE;
            break;
        }
        /*
         * 退出登录前先 join 全部可能存在的业务线程，再删除主页及子页面对象。
         * 如果反过来先删页面，尚未结束的流程可能继续引用已释放状态，造成段错误。
         */
        if(home_page.logout_requested) {
            lv_obj_t *old_screen = screen;
            lv_obj_t *login_screen;

            home_page.logout_requested = 0;
            if(wait_for_department_worker(&department_request) != 0 ||
               wait_for_doctor_worker(&doctor_request) != 0 ||
               wait_for_ticket_worker(&ticket_request) != 0 ||
               wait_for_current_ticket_worker(&current_ticket_request) != 0) {
                fprintf(stderr, "failed to return to login page\n");
                exit_code = EXIT_FAILURE;
                break;
            }
            detach_login_input_bindings(&ui);
            close_message_box(&ui);
            clinic_home_page_cleanup(&home_page);
            clinic_department_page_cleanup(&department_page);
            clinic_doctor_page_cleanup(&doctor_page);
            clinic_ticket_page_cleanup(&ticket_page);
            clinic_queue_page_cleanup(&queue_page);
            department_cache_valid = 0;
            department_return_requested = 0;
            doctor_return_requested = 0;
            ticket_return_requested = 0;
            queue_return_requested = 0;
            authenticated_user_id = 0;
            login_screen = lv_obj_create(NULL);
            if(login_screen == NULL) {
                fprintf(stderr, "failed to create login page after logout\n");
                exit_code = EXIT_FAILURE;
                break;
            }
            /* lv_ime_pinyin_create() attaches its candidate panel to the active screen. */
            lv_scr_load(login_screen);
            if(create_login_page(login_screen, font_info.font, &ui, &login) != 0) {
                if(old_screen != NULL && lv_obj_is_valid(old_screen)) {
                    lv_scr_load(old_screen);
                }
                if(login_screen != NULL && lv_obj_is_valid(login_screen)) {
                    lv_obj_del(login_screen);
                }
                fprintf(stderr, "failed to create login page after logout\n");
                exit_code = EXIT_FAILURE;
                break;
            }
            screen = login_screen;
            ui.auth_state = AUTH_UI_LOGIN_IDLE;
            if(old_screen != NULL && lv_obj_is_valid(old_screen)) {
                lv_obj_del(old_screen);
            }
        }
        if(process_current_ticket_result(
               &screen,
               font_info.font,
               &current_ticket_controller,
               &queue_return_requested) != 0) {
            fprintf(stderr, "failed to process current ticket result\n");
            exit_code = EXIT_FAILURE;
            break;
        }
        if(process_department_result(
               &screen,
               font_info.font,
               &department_request,
               &home_page,
               &department_page,
               &doctor_controller,
               &ticket_controller,
               &department_cache,
               &department_cache_valid,
               &department_return_requested) != 0) {
            fprintf(stderr, "failed to process department result\n");
            exit_code = EXIT_FAILURE;
            break;
        }
        if(process_department_return(
               &screen,
               font_info.font,
               authenticated_user_id,
               &home_page,
               &department_page,
               &department_controller,
               &current_ticket_controller,
               &department_return_requested) != 0) {
            fprintf(stderr, "failed to return to home page\n");
            exit_code = EXIT_FAILURE;
            break;
        }
        if(process_doctor_result(
               &screen,
               font_info.font,
               &doctor_request,
               &department_page,
               &doctor_page,
               &doctor_return_requested) != 0) {
            fprintf(stderr, "failed to process doctor result\n");
            exit_code = EXIT_FAILURE;
            break;
        }
        if(process_doctor_return(
               &screen,
               font_info.font,
               authenticated_user_id,
               &department_page,
               &doctor_page,
               &doctor_controller,
               &department_cache,
               department_cache_valid,
               &department_return_requested,
               &doctor_return_requested) != 0) {
            fprintf(stderr, "failed to return to department page\n");
            exit_code = EXIT_FAILURE;
            break;
        }
        if(process_ticket_result(
               &screen,
               font_info.font,
               &ticket_request,
               &department_page,
               &ticket_page,
               &ticket_return_requested) != 0) {
            fprintf(stderr, "failed to process ticket result\n");
            exit_code = EXIT_FAILURE;
            break;
        }
        if(process_ticket_return(
               &screen,
               font_info.font,
               authenticated_user_id,
               &home_page,
               &ticket_page,
               &department_controller,
               &current_ticket_controller,
               &department_return_requested,
               &doctor_return_requested,
               &ticket_return_requested) != 0) {
            fprintf(stderr, "failed to return to home page\n");
            exit_code = EXIT_FAILURE;
            break;
        }
        if(process_queue_return(
               &screen,
               font_info.font,
               authenticated_user_id,
               &home_page,
               &queue_page,
               &department_controller,
               &current_ticket_controller,
               &queue_return_requested) != 0) {
            fprintf(stderr, "failed to return from queue page\n");
            exit_code = EXIT_FAILURE;
            break;
        }
        lv_tick_inc(LOOP_DELAY_MS);
        sleep_ms(LOOP_DELAY_MS);
    }

cleanup:
    if(login_mutex_initialized && wait_for_login_worker(&login) != 0) {
        exit_code = EXIT_FAILURE;
    }
    if(department_mutex_initialized &&
       wait_for_department_worker(&department_request) != 0) {
        exit_code = EXIT_FAILURE;
    }
    if(doctor_mutex_initialized && wait_for_doctor_worker(&doctor_request) != 0) {
        exit_code = EXIT_FAILURE;
    }
    if(ticket_mutex_initialized && wait_for_ticket_worker(&ticket_request) != 0) {
        exit_code = EXIT_FAILURE;
    }
    if(current_ticket_mutex_initialized &&
       wait_for_current_ticket_worker(&current_ticket_request) != 0) {
        exit_code = EXIT_FAILURE;
    }
    detach_login_input_bindings(&ui);
    clinic_home_page_cleanup(&home_page);
    clinic_department_page_cleanup(&department_page);
    clinic_doctor_page_cleanup(&doctor_page);
    clinic_ticket_page_cleanup(&ticket_page);
    clinic_queue_page_cleanup(&queue_page);
    clinic_register_page_cleanup(&register_page);
    close_message_box(&ui);
    if(screen != NULL) {
        lv_obj_clean(screen);
    }
    clear_login_page_objects(&ui);
    if(font_initialized) {
        lv_ft_font_destroy(&font_info);
        lv_freetype_destroy();
    }
    secure_clear(login.password, sizeof(login.password));
    secure_clear(registered_username, sizeof(registered_username));
    if(current_ticket_mutex_initialized &&
       pthread_mutex_destroy(&current_ticket_request.mutex) != 0) {
        fprintf(stderr, "failed to destroy current ticket mutex\n");
        exit_code = EXIT_FAILURE;
    }
    if(ticket_mutex_initialized &&
       pthread_mutex_destroy(&ticket_request.mutex) != 0) {
        fprintf(stderr, "failed to destroy ticket mutex\n");
        exit_code = EXIT_FAILURE;
    }
    if(doctor_mutex_initialized &&
       pthread_mutex_destroy(&doctor_request.mutex) != 0) {
        fprintf(stderr, "failed to destroy doctor mutex\n");
        exit_code = EXIT_FAILURE;
    }
    if(department_mutex_initialized &&
       pthread_mutex_destroy(&department_request.mutex) != 0) {
        fprintf(stderr, "failed to destroy department mutex\n");
        exit_code = EXIT_FAILURE;
    }
    if(login_mutex_initialized && pthread_mutex_destroy(&login.mutex) != 0) {
        fprintf(stderr, "failed to destroy login mutex\n");
        exit_code = EXIT_FAILURE;
    }

    return exit_code;
}
