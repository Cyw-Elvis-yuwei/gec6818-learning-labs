#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_log.h"
#include "ft6336.h"
#include "pca9557.h"
#include "qmi8658.h"

/* =====================================================================
 * 第二阶段：ST7796 纯色显示 + FT6336 原始触摸坐标验证
 *
 * 硬件：
 *   GPIO1 → I2C SDA
 *   GPIO2 → I2C SCL
 *   I2C bus 100 kHz，三个设备共用同一 bus：
 *     PCA9557 @ 0x19、FT6336 @ 0x38、QMI8658 @ 0x6A
 *
 * 本阶段只打印 FT6336 原始坐标；坐标映射、滑动和图片留到后续阶段。
 * ===================================================================== */

#define I2C_SDA_GPIO     GPIO_NUM_1
#define I2C_SCL_GPIO     GPIO_NUM_2

#define LCD_H_RES        480
#define LCD_V_RES        320
#define LCD_SPI_HOST     SPI2_HOST
#define LCD_SCK_GPIO     GPIO_NUM_41
#define LCD_MOSI_GPIO    GPIO_NUM_40
#define LCD_DC_GPIO      GPIO_NUM_39
#define LCD_BL_GPIO      GPIO_NUM_42
#define LCD_PIXEL_CLOCK_HZ (40 * 1000 * 1000)
#define SWIPE_MIN_DISTANCE 80
#define DEMO_IMAGE_COUNT 3

static i2c_master_bus_handle_t s_bus = NULL;
static const char *TAG = "lcd_demo";
static uint16_t s_lcd_line_buffers[2][LCD_H_RES];

/* 3.5-inch MSP3525/MSP3526 ST7796S module initialization sequence supplied
 * with the panel. The generic ST7796 sequence alone does not configure this
 * module's power and gamma registers. */
static const uint8_t s_st7796_madctl[] = { 0x48 };
static const uint8_t s_st7796_colmod[] = { 0x55 };
static const uint8_t s_st7796_f0_c3[] = { 0xC3 };
static const uint8_t s_st7796_f0_96[] = { 0x96 };
static const uint8_t s_st7796_b4[] = { 0x02 };
static const uint8_t s_st7796_b7[] = { 0xC6 };
static const uint8_t s_st7796_c0[] = { 0xC0, 0x00 };
static const uint8_t s_st7796_c1[] = { 0x13 };
static const uint8_t s_st7796_c2[] = { 0xA7 };
static const uint8_t s_st7796_c5[] = { 0x21 };
static const uint8_t s_st7796_e8[] = { 0x40, 0x8A, 0x1B, 0x1B, 0x23, 0x0A, 0xAC, 0x33 };
static const uint8_t s_st7796_e0[] = { 0xD2, 0x05, 0x08, 0x06, 0x05, 0x02, 0x2A, 0x44, 0x46, 0x39, 0x15, 0x15, 0x2D, 0x32 };
static const uint8_t s_st7796_e1[] = { 0x96, 0x08, 0x0C, 0x09, 0x09, 0x25, 0x2E, 0x43, 0x42, 0x35, 0x11, 0x11, 0x28, 0x2E };
static const uint8_t s_st7796_f0_3c[] = { 0x3C };
static const uint8_t s_st7796_f0_69[] = { 0x69 };
static const st7796_lcd_init_cmd_t s_st7796_init_cmds[] = {
    { 0x11, NULL, 0, 120 },
    { 0x36, s_st7796_madctl, sizeof(s_st7796_madctl), 0 },
    { 0x3A, s_st7796_colmod, sizeof(s_st7796_colmod), 0 },
    { 0xF0, s_st7796_f0_c3, sizeof(s_st7796_f0_c3), 0 },
    { 0xF0, s_st7796_f0_96, sizeof(s_st7796_f0_96), 0 },
    { 0xB4, s_st7796_b4, sizeof(s_st7796_b4), 0 },
    { 0xB7, s_st7796_b7, sizeof(s_st7796_b7), 0 },
    { 0xC0, s_st7796_c0, sizeof(s_st7796_c0), 0 },
    { 0xC1, s_st7796_c1, sizeof(s_st7796_c1), 0 },
    { 0xC2, s_st7796_c2, sizeof(s_st7796_c2), 0 },
    { 0xC5, s_st7796_c5, sizeof(s_st7796_c5), 0 },
    { 0xE8, s_st7796_e8, sizeof(s_st7796_e8), 0 },
    { 0xE0, s_st7796_e0, sizeof(s_st7796_e0), 0 },
    { 0xE1, s_st7796_e1, sizeof(s_st7796_e1), 0 },
    { 0xF0, s_st7796_f0_3c, sizeof(s_st7796_f0_3c), 0 },
    { 0xF0, s_st7796_f0_69, sizeof(s_st7796_f0_69), 120 },
    { 0x21, NULL, 0, 0 },
    { 0x29, NULL, 0, 0 },
};

/* ---------- 创建新版 I2C master bus（GPIO1=SDA, GPIO2=SCL, 100 kHz） ---------- */
static esp_err_t i2c_bus_init(void) {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_cfg, &s_bus);
}

static esp_err_t lcd_backlight_on(void) {
    const gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << LCD_BL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&bl_cfg);
    if (err != ESP_OK) {
        return err;
    }

    return gpio_set_level(LCD_BL_GPIO, 1);
}

static esp_err_t lcd_init(esp_lcd_panel_handle_t *out_panel) {
    if (out_panel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* LCD_CS 由 PCA9557 IO0 控制。保持低电平后，SPI 外设不再需要原生 CS 引脚。 */
    esp_err_t err = pca9557_set_level(PCA9557_IO0, 0);
    if (err != ESP_OK) {
        return err;
    }

    err = lcd_backlight_on();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "LCD backlight enabled (active-high)");

    const spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_SCK_GPIO,
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * sizeof(uint16_t),
    };
    err = spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = LCD_DC_GPIO,
        .cs_gpio_num = GPIO_NUM_NC,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 1,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &io_handle);
    if (err != ESP_OK) {
        return err;
    }

    st7796_vendor_config_t vendor_cfg = {
        .init_cmds = s_st7796_init_cmds,
        .init_cmds_size = sizeof(s_st7796_init_cmds) / sizeof(s_st7796_init_cmds[0]),
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    err = esp_lcd_new_panel_st7796(io_handle, &panel_cfg, out_panel);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_lcd_panel_reset(*out_panel);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_lcd_panel_init(*out_panel);
    if (err != ESP_OK) {
        return err;
    }

    /* 课程屏幕以横屏 480x320 使用。 */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(*out_panel, true), TAG, "LCD swap XY failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(*out_panel, true, false), TAG, "LCD mirror failed");
    return esp_lcd_panel_disp_on_off(*out_panel, true);
}

static uint16_t rgb565_be(uint8_t red, uint8_t green, uint8_t blue) {
    const uint16_t color = (uint16_t)(((red & 0xF8) << 8) |
                                      ((green & 0xFC) << 3) |
                                      (blue >> 3));
    return (uint16_t)((color >> 8) | (color << 8));
}

static uint16_t demo_image_pixel(uint8_t image_index, int x, int y) {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;

    if (image_index == 0) {
        /* 暖色日落：渐变天空、太阳和深色地平线。 */
        red = 245;
        green = (uint8_t)(55 + y * 115 / LCD_V_RES);
        blue = (uint8_t)(35 + y * 45 / LCD_V_RES);
        const int dx = x - 360;
        const int dy = y - 92;
        if (dx * dx + dy * dy < 48 * 48) {
            red = 255;
            green = 238;
            blue = 120;
        }
        if (y > 235) {
            red = 72;
            green = (uint8_t)(38 + (x / 24) % 20);
            blue = 48;
        }
    } else if (image_index == 1) {
        /* 蓝色海浪：深浅渐变和错位波纹。 */
        red = (uint8_t)(15 + y * 20 / LCD_V_RES);
        green = (uint8_t)(70 + y * 105 / LCD_V_RES);
        blue = (uint8_t)(155 + y * 90 / LCD_V_RES);
        const int wave = (y + x / 7) % 52;
        if (wave < 8) {
            red = 185;
            green = 238;
            blue = 250;
        }
    } else {
        /* 彩色棋盘：绿色方格和橙色对角带。 */
        const bool alternate = ((x / 48) + (y / 40)) % 2;
        red = alternate ? 42 : 18;
        green = alternate ? 178 : 105;
        blue = alternate ? 92 : 62;
        const int diagonal = y - (x * LCD_V_RES / LCD_H_RES);
        if (diagonal > -12 && diagonal < 12) {
            red = 248;
            green = 152;
            blue = 42;
        }
    }

    /* 底部三格页码指示器：当前图片为白色，其余为深色。 */
    if (y >= LCD_V_RES - 24 && y < LCD_V_RES - 12) {
        for (int i = 0; i < DEMO_IMAGE_COUNT; ++i) {
            const int left = LCD_H_RES / 2 - 34 + i * 24;
            if (x >= left && x < left + 14) {
                if (i == image_index) {
                    red = green = blue = 255;
                } else {
                    red = green = blue = 45;
                }
            }
        }
    }
    return rgb565_be(red, green, blue);
}

static esp_err_t lcd_draw_demo_image(esp_lcd_panel_handle_t panel, uint8_t image_index) {
    if (panel == NULL || image_index >= DEMO_IMAGE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int y = 0; y < LCD_V_RES; ++y) {
        uint16_t *line = s_lcd_line_buffers[y & 1];
        for (int x = 0; x < LCD_H_RES; ++x) {
            line[x] = demo_image_pixel(image_index, x, y);
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(panel, 0, y, LCD_H_RES, y + 1, line);
        if (err != ESP_OK) {
            return err;
        }
    }
    ESP_LOGI(TAG, "Demo image %u/%u displayed",
             (unsigned)(image_index + 1), (unsigned)DEMO_IMAGE_COUNT);
    return ESP_OK;
}

static void touch_raw_to_screen(uint16_t raw_x, uint16_t raw_y,
                                uint16_t *screen_x, uint16_t *screen_y) {
    /* 真机标定结果：横屏 X 随 raw_y 反向变化，横屏 Y 随 raw_x 反向变化。 */
    const uint16_t clamped_raw_x = raw_x < LCD_V_RES ? raw_x : LCD_V_RES - 1;
    const uint16_t clamped_raw_y = raw_y < LCD_H_RES ? raw_y : LCD_H_RES - 1;
    *screen_x = (LCD_H_RES - 1) - clamped_raw_y;
    *screen_y = (LCD_V_RES - 1) - clamped_raw_x;
}

void app_main(void) {
    /* 1. 创建 I2C master bus */
    esp_err_t err = i2c_bus_init();
    if (err != ESP_OK) {
        printf("I2C bus init failed: %s\n", esp_err_to_name(err));
        return;
    }

    /* 2. 初始化 PCA9557（仅初始化，不再做 A/B 回环测试） */
    err = pca9557_init(s_bus);
    if (err != ESP_OK) {
        printf("PCA9557 init failed: %s\n", esp_err_to_name(err));
        return;
    }
    printf("PCA9557 initialized\n");

    esp_lcd_panel_handle_t lcd_panel = NULL;
    err = lcd_init(&lcd_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD initialization failed: %s", esp_err_to_name(err));
        return;
    }
    uint8_t current_image = 0;
    err = lcd_draw_demo_image(lcd_panel, current_image);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD draw failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "LCD initialized: 3 built-in demo images, 480x320");

    /* 触摸芯片与姿态传感器共用同一条 I2C master bus。 */
    err = ft6336_init(s_bus);
    if (err != ESP_OK) {
        printf("FT6336 init failed: %s\n", esp_err_to_name(err));
        return;
    }
    printf("FT6336 OK: swipe left/right to test gestures\n");

    err = qmi8658_init(s_bus);
    if (err != ESP_OK) {
        printf("QMI8658 init failed: %s\n", esp_err_to_name(err));
        return;
    }
    printf("QMI8658 OK\n");

    /* QMI8658 周期读取在滑动测试阶段暂停，避免超时阻塞触摸采样。 */
    printf("QMI8658 periodic reads paused during swipe test\n");

    /* 每 20 ms 轮询触摸，用按下点和松开前最后一点判断手势。 */
    bool was_touched = false;
    uint16_t start_x = 0;
    uint16_t start_y = 0;
    uint16_t end_x = 0;
    uint16_t end_y = 0;
    for (;;) {
        ft6336_touch_t touch;
        err = ft6336_read_touch(&touch);
        if (err != ESP_OK) {
            printf("FT6336 read failed: %s\n", esp_err_to_name(err));
        } else if (touch.count > 0) {
            uint16_t screen_x = 0;
            uint16_t screen_y = 0;
            touch_raw_to_screen(touch.x, touch.y, &screen_x, &screen_y);
            if (!was_touched) {
                start_x = screen_x;
                start_y = screen_y;
                printf("touch start: x=%u y=%u\n",
                       (unsigned)start_x, (unsigned)start_y);
            }
            was_touched = true;
            end_x = screen_x;
            end_y = screen_y;
        } else {
            if (was_touched) {
                const int32_t dx = (int32_t)end_x - (int32_t)start_x;
                const int32_t dy = (int32_t)end_y - (int32_t)start_y;
                const int32_t abs_dx = dx >= 0 ? dx : -dx;
                const int32_t abs_dy = dy >= 0 ? dy : -dy;
                if (abs_dx >= SWIPE_MIN_DISTANCE && abs_dx > abs_dy) {
                    if (dx < 0) {
                        printf("gesture: SWIPE_LEFT -> NEXT (dx=%ld dy=%ld)\n",
                               (long)dx, (long)dy);
                        current_image = (uint8_t)((current_image + 1) % DEMO_IMAGE_COUNT);
                    } else {
                        printf("gesture: SWIPE_RIGHT -> PREVIOUS (dx=%ld dy=%ld)\n",
                               (long)dx, (long)dy);
                        current_image = (uint8_t)((current_image + DEMO_IMAGE_COUNT - 1) %
                                                  DEMO_IMAGE_COUNT);
                    }
                    err = lcd_draw_demo_image(lcd_panel, current_image);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Demo image draw failed: %s", esp_err_to_name(err));
                    }
                } else {
                    printf("gesture: ignored (dx=%ld dy=%ld)\n",
                           (long)dx, (long)dy);
                }
            }
            was_touched = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
