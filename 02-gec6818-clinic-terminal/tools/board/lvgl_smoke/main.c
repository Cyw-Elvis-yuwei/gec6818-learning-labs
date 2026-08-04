/*
 * 文件作用（答辩）：GEC6818 的最小 LVGL 综合探针，不是正式医疗终端。
 * 它单独验证 framebuffer 显示、evdev 触摸、FreeType 中文字体以及 pthread 网络请求，
 * 用于在完整业务接入前证明硬件和基础运行环境可用。
 */
#define _POSIX_C_SOURCE 200809L

#include "clinic_net.h"
#include "lvgl.h"
#include "display/fbdev.h"
#include "indev/evdev.h"
#include "src/extra/libs/freetype/lv_freetype.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
    DISPLAY_WIDTH = 800,
    DISPLAY_HEIGHT = 480,
    DRAW_BUFFER_ROWS = 40,
    CHINESE_FONT_SIZE = 32,
    LOOP_DELAY_MS = 5,
    NETWORK_TIMEOUT_SECONDS = 5,
    RESPONSE_MAX_BYTES = 4096
};

typedef enum {
    NETWORK_STATE_IDLE = 0,
    NETWORK_STATE_RUNNING,
    NETWORK_STATE_SUCCESS,
    NETWORK_STATE_FAILED
} NetworkState;

typedef struct {
    pthread_mutex_t mutex;
    pthread_t worker;
    NetworkState state;
    const char *server_ip;
    const char *server_port;
    int thread_pending_join;
} NetworkContext;

typedef struct {
    NetworkContext *network;
    lv_obj_t *status_label;
    NetworkState *displayed_state;
} NetworkButtonContext;

static const char *const chinese_font_path = "/font/simkai.ttf";
static const char ping_request[] =
    "{\"type\":\"ping\",\"request_id\":1}\n";

static volatile sig_atomic_t keep_running = 1;
static volatile sig_atomic_t network_timeout_expired = 0;
static lv_color_t draw_buffer[DISPLAY_WIDTH * DRAW_BUFFER_ROWS];

static void handle_shutdown_signal(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
}

static void handle_network_timeout(int signal_number)
{
    (void)signal_number;
    network_timeout_expired = 1;
}

static int set_network_timeout_signal_mask(int operation)
{
    sigset_t timeout_set;

    if (sigemptyset(&timeout_set) != 0 ||
        sigaddset(&timeout_set, SIGALRM) != 0) {
        return -1;
    }

    return pthread_sigmask(operation, &timeout_set, NULL) == 0 ? 0 : -1;
}

static int install_signal_handlers(void)
{
    struct sigaction shutdown_action = {0};
    struct sigaction timeout_action = {0};
    struct sigaction pipe_action = {0};

    shutdown_action.sa_handler = handle_shutdown_signal;
    if (sigemptyset(&shutdown_action.sa_mask) != 0 ||
        sigaction(SIGINT, &shutdown_action, NULL) != 0 ||
        sigaction(SIGTERM, &shutdown_action, NULL) != 0) {
        return -1;
    }

    timeout_action.sa_handler = handle_network_timeout;
    if (sigemptyset(&timeout_action.sa_mask) != 0 ||
        sigaction(SIGALRM, &timeout_action, NULL) != 0) {
        return -1;
    }

    pipe_action.sa_handler = SIG_IGN;
    if (sigemptyset(&pipe_action.sa_mask) != 0 ||
        sigaction(SIGPIPE, &pipe_action, NULL) != 0) {
        return -1;
    }

    return set_network_timeout_signal_mask(SIG_BLOCK);
}

static int port_is_valid(const char *port)
{
    unsigned long value = 0UL;
    const unsigned char *cursor = (const unsigned char *)port;

    if (port == NULL || *port == '\0') {
        return 0;
    }

    while (*cursor != '\0') {
        if (*cursor < (unsigned char)'0' ||
            *cursor > (unsigned char)'9') {
            return 0;
        }

        value = value * 10UL + (unsigned long)(*cursor - (unsigned char)'0');
        if (value > 65535UL) {
            return 0;
        }
        ++cursor;
    }

    return value >= 1UL;
}

static int response_is_pong(const char *response, size_t response_length)
{
    if (memchr(response, '\0', response_length) != NULL) {
        return 0;
    }

    return strstr(response, "\"ok\":true") != NULL &&
           strstr(response, "\"type\":\"pong\"") != NULL &&
           strstr(response, "\"request_id\":1") != NULL;
}

static int receive_pong(clinic_socket_t socket_fd)
{
    char response[RESPONSE_MAX_BYTES + 1U];
    size_t response_length = 0U;
    int received_line = 0;

    while (response_length < RESPONSE_MAX_BYTES) {
        size_t available = RESPONSE_MAX_BYTES - response_length;
        ssize_t received = recv(
            socket_fd,
            response + response_length,
            available,
            0
        );

        if (received == 0) {
            return -1;
        }
        if (received < 0) {
            if (errno == EINTR && network_timeout_expired == 0) {
                continue;
            }
            return -1;
        }

        {
            char *newline = memchr(
                response + response_length,
                '\n',
                (size_t)received
            );

            response_length += (size_t)received;
            if (newline != NULL) {
                response_length = (size_t)(newline - response) + 1U;
                received_line = 1;
                break;
            }
        }
    }

    if (!received_line || network_timeout_expired != 0) {
        return -1;
    }

    response[response_length] = '\0';
    return response_is_pong(response, response_length) ? 0 : -1;
}

static int run_network_probe(const NetworkContext *context)
{
    clinic_socket_t socket_fd = CLINIC_SOCKET_INVALID;
    int network_started = 0;
    int result = -1;

    if (set_network_timeout_signal_mask(SIG_UNBLOCK) != 0) {
        return -1;
    }

    network_timeout_expired = 0;
    (void)alarm(NETWORK_TIMEOUT_SECONDS);

    if (clinic_net_startup() != 0) {
        goto cleanup;
    }
    network_started = 1;

    if (clinic_net_connect(
            context->server_ip,
            context->server_port,
            &socket_fd) != 0 ||
        network_timeout_expired != 0) {
        goto cleanup;
    }

    if (clinic_net_send_all(
            socket_fd,
            ping_request,
            sizeof(ping_request) - 1U) != 0 ||
        network_timeout_expired != 0) {
        goto cleanup;
    }

    if (receive_pong(socket_fd) != 0) {
        goto cleanup;
    }

    result = 0;

cleanup:
    (void)alarm(0U);
    if (socket_fd != CLINIC_SOCKET_INVALID) {
        clinic_socket_close(socket_fd);
    }
    if (network_started != 0) {
        clinic_net_cleanup();
    }
    if (set_network_timeout_signal_mask(SIG_BLOCK) != 0) {
        result = -1;
    }
    return result;
}

static void *network_worker(void *argument)
{
    NetworkContext *context = argument;
    NetworkState result_state;

    result_state = run_network_probe(context) == 0
        ? NETWORK_STATE_SUCCESS
        : NETWORK_STATE_FAILED;

    if (pthread_mutex_lock(&context->mutex) == 0) {
        context->state = result_state;
        (void)pthread_mutex_unlock(&context->mutex);
    }

    return NULL;
}

static void handle_network_button_clicked(lv_event_t *event)
{
    NetworkButtonContext *button_context;
    NetworkContext *context;
    NetworkState state_snapshot;
    int pending_join_snapshot;
    int create_result;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    button_context = lv_event_get_user_data(event);
    if (button_context == NULL ||
        button_context->network == NULL ||
        button_context->status_label == NULL ||
        button_context->displayed_state == NULL) {
        return;
    }
    context = button_context->network;
    if (pthread_mutex_lock(&context->mutex) != 0) {
        return;
    }

    state_snapshot = context->state;
    pending_join_snapshot = context->thread_pending_join;

    if (state_snapshot == NETWORK_STATE_RUNNING) {
        (void)pthread_mutex_unlock(&context->mutex);
        return;
    }
    if (pending_join_snapshot != 0) {
        (void)pthread_mutex_unlock(&context->mutex);
        return;
    }

    context->state = NETWORK_STATE_RUNNING;
    context->thread_pending_join = 1;
    (void)pthread_mutex_unlock(&context->mutex);

    lv_label_set_text(button_context->status_label, "连接中...");
    *button_context->displayed_state = NETWORK_STATE_RUNNING;

    create_result = pthread_create(
        &context->worker,
        NULL,
        network_worker,
        context
    );
    if (create_result == 0) {
        return;
    }

    if (pthread_mutex_lock(&context->mutex) == 0) {
        context->thread_pending_join = 0;
        context->state = NETWORK_STATE_FAILED;
        (void)pthread_mutex_unlock(&context->mutex);
    }
}

static const char *network_state_text(NetworkState state)
{
    switch (state) {
        case NETWORK_STATE_RUNNING:
            return "连接中...";
        case NETWORK_STATE_SUCCESS:
            return "服务器在线";
        case NETWORK_STATE_FAILED:
            return "连接失败";
        case NETWORK_STATE_IDLE:
        default:
            return "等待检测";
    }
}

static int read_network_state(
    NetworkContext *context,
    NetworkState *state_out)
{
    if (pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }

    *state_out = context->state;
    return pthread_mutex_unlock(&context->mutex) == 0 ? 0 : -1;
}

static int join_network_worker(NetworkContext *context)
{
    pthread_t worker;
    int thread_pending_join;
    int join_result;

    if (pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }

    worker = context->worker;
    thread_pending_join = context->thread_pending_join;
    if (pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    if (thread_pending_join == 0) {
        return 0;
    }

    join_result = pthread_join(worker, NULL);
    if (join_result != 0) {
        return -1;
    }

    if (pthread_mutex_lock(&context->mutex) != 0) {
        return -1;
    }
    context->thread_pending_join = 0;
    if (pthread_mutex_unlock(&context->mutex) != 0) {
        return -1;
    }

    return 0;
}

static int sync_network_status(
    NetworkContext *context,
    lv_obj_t *status_label,
    NetworkState *displayed_state)
{
    NetworkState current_state;

    if (read_network_state(context, &current_state) != 0) {
        return -1;
    }

    if (current_state != *displayed_state) {
        lv_label_set_text(status_label, network_state_text(current_state));
        *displayed_state = current_state;
    }

    if (current_state != NETWORK_STATE_RUNNING) {
        return join_network_worker(context);
    }

    return 0;
}

static void set_chinese_label_style(lv_obj_t *label, const lv_font_t *font)
{
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
}

int main(int argc, char **argv)
{
    static lv_disp_draw_buf_t draw_buffer_descriptor;
    static lv_disp_drv_t display_driver;
    static lv_indev_drv_t input_driver;
    NetworkContext network_context = {
        .state = NETWORK_STATE_IDLE,
        .server_ip = NULL,
        .server_port = NULL,
        .thread_pending_join = 0
    };
    NetworkButtonContext button_context = {0};
    lv_ft_info_t chinese_font = {
        .name = chinese_font_path,
        .mem = NULL,
        .mem_size = 0U,
        .font = NULL,
        .weight = CHINESE_FONT_SIZE,
        .style = FT_FONT_STYLE_NORMAL
    };
    const struct timespec loop_delay = {
        .tv_sec = 0,
        .tv_nsec = LOOP_DELAY_MS * 1000L * 1000L
    };
    lv_obj_t *screen = NULL;
    lv_obj_t *title_label = NULL;
    lv_obj_t *button = NULL;
    lv_obj_t *button_label = NULL;
    lv_obj_t *status_label = NULL;
    NetworkState displayed_state = NETWORK_STATE_IDLE;
    int mutex_initialized = 0;
    int font_initialized = 0;
    int exit_code = EXIT_FAILURE;

    if (argc != 3 || argv[1][0] == '\0' || !port_is_valid(argv[2])) {
        fprintf(stderr, "usage: %s <server_ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    network_context.server_ip = argv[1];
    network_context.server_port = argv[2];

    if (install_signal_handlers() != 0) {
        fputs("failed to install signal handlers\n", stderr);
        return EXIT_FAILURE;
    }
    if (pthread_mutex_init(&network_context.mutex, NULL) != 0) {
        fputs("failed to initialize network mutex\n", stderr);
        return EXIT_FAILURE;
    }
    mutex_initialized = 1;

    lv_init();
    fbdev_init();

    lv_disp_draw_buf_init(
        &draw_buffer_descriptor,
        draw_buffer,
        NULL,
        DISPLAY_WIDTH * DRAW_BUFFER_ROWS
    );

    lv_disp_drv_init(&display_driver);
    display_driver.draw_buf = &draw_buffer_descriptor;
    display_driver.flush_cb = fbdev_flush;
    display_driver.hor_res = DISPLAY_WIDTH;
    display_driver.ver_res = DISPLAY_HEIGHT;

    if (lv_disp_drv_register(&display_driver) == NULL) {
        fputs("failed to register LVGL display driver\n", stderr);
        goto cleanup;
    }

    evdev_init();

    lv_indev_drv_init(&input_driver);
    input_driver.type = LV_INDEV_TYPE_POINTER;
    input_driver.read_cb = evdev_read;

    if (lv_indev_drv_register(&input_driver) == NULL) {
        fputs("failed to register LVGL input driver\n", stderr);
        goto cleanup;
    }

    screen = lv_scr_act();
    if (screen == NULL) {
        fputs("failed to obtain LVGL screen\n", stderr);
        goto cleanup;
    }

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x123A4A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    if (!lv_ft_font_init(&chinese_font) || chinese_font.font == NULL) {
        fprintf(
            stderr,
            "failed to initialize FreeType font: %s\n",
            chinese_font_path
        );
        goto cleanup;
    }
    font_initialized = 1;

    title_label = lv_label_create(screen);
    if (title_label == NULL) {
        fputs("failed to create title label\n", stderr);
        goto cleanup;
    }
    lv_label_set_text(title_label, "医路通 网络验证");
    set_chinese_label_style(title_label, chinese_font.font);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 60);

    button = lv_btn_create(screen);
    if (button == NULL) {
        fputs("failed to create network button\n", stderr);
        goto cleanup;
    }
    lv_obj_set_size(button, 260, 100);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x197A8C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        button,
        lv_color_hex(0x105866),
        LV_PART_MAIN | LV_STATE_PRESSED
    );
    lv_obj_set_style_radius(button, 12, LV_PART_MAIN);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 20);

    button_label = lv_label_create(button);
    if (button_label == NULL) {
        fputs("failed to create network button label\n", stderr);
        goto cleanup;
    }
    lv_label_set_text(button_label, "检测服务器");
    set_chinese_label_style(button_label, chinese_font.font);
    lv_obj_center(button_label);

    status_label = lv_label_create(screen);
    if (status_label == NULL) {
        fputs("failed to create network status label\n", stderr);
        goto cleanup;
    }
    lv_label_set_text(status_label, "等待检测");
    set_chinese_label_style(status_label, chinese_font.font);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -65);

    button_context.network = &network_context;
    button_context.status_label = status_label;
    button_context.displayed_state = &displayed_state;
    lv_obj_add_event_cb(
        button,
        handle_network_button_clicked,
        LV_EVENT_CLICKED,
        &button_context
    );

    exit_code = EXIT_SUCCESS;
    while (keep_running != 0) {
        (void)lv_timer_handler();
        if (sync_network_status(
                &network_context,
                status_label,
                &displayed_state) != 0) {
            fputs("failed to synchronize network worker\n", stderr);
            exit_code = EXIT_FAILURE;
            break;
        }
        if (nanosleep(&loop_delay, NULL) != 0 && keep_running == 0) {
            break;
        }
        lv_tick_inc(LOOP_DELAY_MS);
    }

cleanup:
    if (join_network_worker(&network_context) != 0) {
        fputs("failed to join network worker\n", stderr);
        exit_code = EXIT_FAILURE;
    }
    if (status_label != NULL) {
        lv_obj_del(status_label);
    }
    if (button != NULL) {
        lv_obj_del(button);
    }
    if (title_label != NULL) {
        lv_obj_del(title_label);
    }
    if (font_initialized != 0) {
        lv_ft_font_destroy(chinese_font.font);
        lv_freetype_destroy();
    }
    if (mutex_initialized != 0 &&
        pthread_mutex_destroy(&network_context.mutex) != 0) {
        fputs("failed to destroy network mutex\n", stderr);
        exit_code = EXIT_FAILURE;
    }

    return exit_code;
}
