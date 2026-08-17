#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"

/* =====================================================================
 * 阶段目标：GPIO11 双边沿中断 → ISR 记录事件 → FreeRTOS Queue
 *           → Key Task 消抖 + 状态机（单击/双击/长按）
 *           → LED Task 执行反馈（GPIO10）
 *
 * 硬件：
 *   按键 GPIO11：松开=高电平，按下=低电平（内部上拉），双边沿中断
 *   LED  GPIO10：低电平点亮
 * ===================================================================== */

/* ---------- 硬件定义 ---------- */
#define KEY_PIN     GPIO_NUM_11
#define LED_PIN     GPIO_NUM_10

/* ---------- 时间参数（一律用 pdMS_TO_TICKS 换算，不假设 tick=1ms） ---------- */
#define KEY_DEBOUNCE_MS      20     /* 消抖：事件后延时重读确认 */
#define KEY_MIN_PRESS_MS     30     /* 完整按压 < 30ms 视为毛刺 */
#define KEY_LONG_PRESS_MS    1500   /* 长按阈值 */
#define KEY_DOUBLE_WINDOW_MS 300    /* 第一次 UP 后等待第二次 DOWN 的窗口 */
#define LED_BLINK_PHASE_MS   100    /* LED 快闪单个亮/灭阶段 */

/* ---------- 队列长度 ---------- */
#define KEY_QUEUE_LEN 16
#define LED_QUEUE_LEN 8

/* 原始按键事件（ISR 产生，Key Task 消费） */
typedef struct {
    int level;       /* 事件边沿后的电平：0=DOWN（按下），1=UP（松开） */
    TickType_t tick; /* ISR 记录的 tick，作为所有时间判断的基准 */
} key_event_t;

/* LED 命令（Key Task 发出，LED Task 执行） */
typedef enum {
    LED_CMD_TOGGLE = 0, /* 翻转一次（单击） */
    LED_CMD_BLINK_2,    /* 快闪 2 次（双击） */
    LED_CMD_BLINK_3,    /* 快闪 3 次（长按） */
} led_cmd_t;

static QueueHandle_t s_key_queue = NULL;              /* 按键原始事件队列 */
static QueueHandle_t s_led_queue = NULL;              /* LED 命令队列 */
static volatile uint32_t s_key_drop_count = 0;        /* ISR 内累计丢弃的按键事件数 */

static bool led_on = false;                           /* LED 当前亮灭状态（true=亮） */

/* ---------- LED 底层控制（低电平点亮） ---------- */
static void set_led(bool on) {
    gpio_set_level(LED_PIN, on ? 0 : 1);
    led_on = on;
}

/* ---------- GPIO ISR：只做四件事，立即退出 ---------- */
static void IRAM_ATTR key_isr_handler(void *arg) {
    key_event_t ev;
    BaseType_t woken = pdFALSE;

    ev.level = gpio_get_level(KEY_PIN);            /* 1. 读当前按键电平 */
    ev.tick  = xTaskGetTickCountFromISR();         /* 2. 记录事件 tick */
    if (xQueueSendFromISR(s_key_queue, &ev, &woken) != pdTRUE) {
        s_key_drop_count++;                        /* 3. 入队；满则丢弃并计数 */
    }
    if (woken) {
        portYIELD_FROM_ISR(woken);                 /* 4. 需要时切换任务 */
    }
}

/* ---------- 向 LED Task 发送命令（非阻塞，满则丢弃，不拖累 Key Task） ---------- */
static void send_led_cmd(led_cmd_t cmd) {
    xQueueSend(s_led_queue, &cmd, 0);
}

/* ---------- 按键状态机 ---------- */
typedef enum {
    KEY_ST_IDLE,          /* 空闲，等待第一次 DOWN */
    KEY_ST_WAIT_UP,       /* 第一次按下，等待 UP */
    KEY_ST_WAIT_SECOND,   /* 第一次 UP 后，300ms 窗口内等待第二次 DOWN */
    KEY_ST_WAIT_SECOND_UP /* 第二次按下，等待 UP */
} key_state_t;

/* 队列溢出信息由 Task 输出（ISR 内禁止打印） */
static void report_overflow(uint32_t *last_seen) {
    uint32_t cur = s_key_drop_count;
    if (cur != *last_seen) {
        printf("[KEY] queue overflow, dropped=%lu\n", (unsigned long)cur);
        *last_seen = cur;
    }
}

static void key_task(void *arg) {
    (void)arg;
    key_event_t ev;
    key_state_t state = KEY_ST_IDLE;
    TickType_t press_start = 0;       /* 当前（第一次）按压开始 tick */
    TickType_t first_up_tick = 0;     /* 第一次 UP 的 tick */
    TickType_t second_down_tick = 0;  /* 第二次 DOWN 的 tick */
    uint32_t last_drop = 0;

    for (;;) {
        /* 双击窗口：WAIT_SECOND 状态用 300ms 超时等待第二次事件，
         * 而不是把任务直接睡死 300ms。 */
        TickType_t wait = portMAX_DELAY;
        if (state == KEY_ST_WAIT_SECOND) {
            wait = pdMS_TO_TICKS(KEY_DOUBLE_WINDOW_MS);
        }

        if (xQueueReceive(s_key_queue, &ev, wait) != pdPASS) {
            /* 超时：只会在 WAIT_SECOND 发生 → 300ms 内无第二次有效 DOWN，
             * 确认一次单击（此时才报告，不提前）。 */
            if (state == KEY_ST_WAIT_SECOND) {
                TickType_t dur = first_up_tick - press_start;
                printf("[KEY] SINGLE_CLICK duration=%lu ms\n",
                       (unsigned long)pdTICKS_TO_MS(dur));
                send_led_cmd(LED_CMD_TOGGLE);
                state = KEY_ST_IDLE;
            }
            report_overflow(&last_drop);
            continue;
        }

        /* 消抖：等待约 20ms 后重读 GPIO11，与事件电平一致才有效；
         * 时间判断仍使用 ISR 记录的原始 tick，不用确认时刻替代。 */
        vTaskDelay(pdMS_TO_TICKS(KEY_DEBOUNCE_MS));
        if (gpio_get_level(KEY_PIN) != ev.level) {
            report_overflow(&last_drop);   /* 抖动/毛刺，丢弃 */
            continue;
        }

        switch (state) {
        case KEY_ST_IDLE:
            if (ev.level == 0) {           /* 有效 DOWN */
                press_start = ev.tick;
                state = KEY_ST_WAIT_UP;
            }
            break;

        case KEY_ST_WAIT_UP:
            if (ev.level == 1) {           /* 有效 UP，完整按压结束 */
                TickType_t dur = ev.tick - press_start;
                if (dur < pdMS_TO_TICKS(KEY_MIN_PRESS_MS)) {
                    state = KEY_ST_IDLE;   /* <30ms：毛刺，不产生任何事件 */
                } else if (dur >= pdMS_TO_TICKS(KEY_LONG_PRESS_MS)) {
                    /* 长按：优先级最高，只产生 LONG_PRESS，立即复位 */
                    printf("[KEY] LONG_PRESS duration=%lu ms\n",
                           (unsigned long)pdTICKS_TO_MS(dur));
                    send_led_cmd(LED_CMD_BLINK_3);
                    state = KEY_ST_IDLE;
                } else {
                    /* 有效短按 → 双击候选，等待第二次 DOWN */
                    first_up_tick = ev.tick;
                    state = KEY_ST_WAIT_SECOND;
                }
            }
            break;

        case KEY_ST_WAIT_SECOND:
            if (ev.level == 0) {           /* 第二次有效 DOWN → 双击候选 */
                second_down_tick = ev.tick;
                state = KEY_ST_WAIT_SECOND_UP; /* 不能立即宣布 DOUBLE */
            }
            /* 异常出现的 UP 忽略 */
            break;

        case KEY_ST_WAIT_SECOND_UP:
            if (ev.level == 1) {           /* 第二次有效 UP，最终判断 */
                TickType_t dur2 = ev.tick - second_down_tick;
                if (dur2 < pdMS_TO_TICKS(KEY_MIN_PRESS_MS)) {
                    /* 第二次是毛刺 → 回退为第一次按压的单击 */
                    TickType_t dur1 = first_up_tick - press_start;
                    printf("[KEY] SINGLE_CLICK duration=%lu ms\n",
                           (unsigned long)pdTICKS_TO_MS(dur1));
                    send_led_cmd(LED_CMD_TOGGLE);
                } else if (dur2 >= pdMS_TO_TICKS(KEY_LONG_PRESS_MS)) {
                    /* 第二次长按 → 最终只产生 LONG_PRESS，不产生 DOUBLE */
                    printf("[KEY] LONG_PRESS duration=%lu ms\n",
                           (unsigned long)pdTICKS_TO_MS(dur2));
                    send_led_cmd(LED_CMD_BLINK_3);
                } else {
                    /* 两次有效短按 → 只产生一次 DOUBLE_CLICK */
                    TickType_t interval = second_down_tick - first_up_tick;
                    printf("[KEY] DOUBLE_CLICK interval=%lu ms\n",
                           (unsigned long)pdTICKS_TO_MS(interval));
                    send_led_cmd(LED_CMD_BLINK_2);
                }
                state = KEY_ST_IDLE;
            }
            break;
        }

        report_overflow(&last_drop);
    }
}

/* ---------- LED Task：只执行反馈，不参与按键判断 ---------- */
static void led_task(void *arg) {
    (void)arg;
    led_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_led_queue, &cmd, portMAX_DELAY) == pdPASS) {
            switch (cmd) {
            case LED_CMD_TOGGLE:
                set_led(!led_on);                 /* 单击：翻转一次 */
                break;
            case LED_CMD_BLINK_2:
            case LED_CMD_BLINK_3: {
                bool prev = led_on;               /* 记住闪烁前状态 */
                int n = (cmd == LED_CMD_BLINK_2) ? 2 : 3;
                for (int i = 0; i < n; i++) {
                    set_led(true);                /* 亮 100ms */
                    vTaskDelay(pdMS_TO_TICKS(LED_BLINK_PHASE_MS));
                    set_led(false);               /* 灭 100ms */
                    vTaskDelay(pdMS_TO_TICKS(LED_BLINK_PHASE_MS));
                }
                set_led(prev);                    /* 闪烁结束恢复之前状态 */
                break;
            }
            default:
                break;
            }
        }
    }
}

/* ---------- 初始化 ---------- */
static void led_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    set_led(false);   /* 初始熄灭 */
}

static void key_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << KEY_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  /* 松开=高电平，需上拉 */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,    /* 双边沿中断 */
    };
    gpio_config(&cfg);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(KEY_PIN, key_isr_handler, NULL));
}

void app_main(void) {
    s_key_queue = xQueueCreate(KEY_QUEUE_LEN, sizeof(key_event_t));
    s_led_queue = xQueueCreate(LED_QUEUE_LEN, sizeof(led_cmd_t));

    led_init();
    key_init();

    xTaskCreate(key_task, "key_task", 3072, NULL, 10, NULL);
    xTaskCreate(led_task, "led_task", 2048, NULL, 8, NULL);

    printf("按键状态机已启动：GPIO11 单击/双击/长按，GPIO10 LED 反馈\n");
}
