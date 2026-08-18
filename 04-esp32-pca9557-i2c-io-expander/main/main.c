#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "pca9557.h"

/* =====================================================================
 * PCA9557 回环实验（新版 I2C Master API）
 *
 * 硬件：
 *   GPIO1 → I2C SDA
 *   GPIO2 → I2C SCL
 *   PCA9557 @ 0x19（板载 I2C 总线）
 *
 *   IO0~IO4 → 输出（IO0=LCD_CS, IO1=PA_EN, IO2=DVP_PWDN, IO3/IO4=实验输出）
 *   IO5~IO7 → 输入（杜邦线回环读取 EXT-IO3/4/5）
 *
 * 实验内容：
 *   IO3/IO4 每 ~1s 在 A(IO3=0,IO4=1) / B(IO3=1,IO4=0) 间切换，
 *   读取 IO5/IO6/IO7 并串口打印。IO0~IO2 固定 IO0=1 IO1=0 IO2=1，禁止翻转。
 * ===================================================================== */

#define I2C_SDA_GPIO     GPIO_NUM_1
#define I2C_SCL_GPIO     GPIO_NUM_2
#define TEST_PERIOD_MS   1000   /* 每个状态保持 1 秒 */

static i2c_master_bus_handle_t s_bus = NULL;

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

/* ---------- 执行一个 A/B 状态：设置 IO3/IO4，读取并打印 IO5/6/7。
 * IO5/IO6/IO7 任一读取失败：打印错误并立即返回，不打印本轮 OUT/IN 数据行。 ---------- */
static void run_phase(uint8_t io3, uint8_t io4) {
    esp_err_t err = pca9557_set_level(PCA9557_IO3, io3);
    if (err == ESP_OK) {
        err = pca9557_set_level(PCA9557_IO4, io4);
    }
    if (err != ESP_OK) {
        printf("PCA9557 set level failed: %s\n", esp_err_to_name(err));
        return;
    }

    uint8_t in5 = 0, in6 = 0, in7 = 0;
    err = pca9557_get_level(PCA9557_IO5, &in5);
    if (err != ESP_OK) {
        printf("PCA9557 read IO5 failed: %s\n", esp_err_to_name(err));
        return;
    }
    err = pca9557_get_level(PCA9557_IO6, &in6);
    if (err != ESP_OK) {
        printf("PCA9557 read IO6 failed: %s\n", esp_err_to_name(err));
        return;
    }
    err = pca9557_get_level(PCA9557_IO7, &in7);
    if (err != ESP_OK) {
        printf("PCA9557 read IO7 failed: %s\n", esp_err_to_name(err));
        return;
    }

    printf("OUT: IO3=%d IO4=%d | IN: IO5=%d IO6=%d IO7=%d\n",
           io3, io4, in5, in6, in7);
}

void app_main(void) {
    /* 1. 创建 I2C master bus */
    esp_err_t err = i2c_bus_init();
    if (err != ESP_OK) {
        printf("I2C bus init failed: %s\n", esp_err_to_name(err));
        return;
    }

    /* 2. 初始化 PCA9557（任一步失败即停止，不进入回环测试） */
    err = pca9557_init(s_bus);
    if (err != ESP_OK) {
        printf("PCA9557 init failed: %s\n", esp_err_to_name(err));
        return;
    }

    printf("PCA9557 initialized\n");
    printf("IO0~IO4: OUTPUT\n");
    printf("IO5~IO7: INPUT\n");

    /* 3. 回环测试：A(IO3=0,IO4=1) ↔ B(IO3=1,IO4=0)，每 1s 切换 */
    for (;;) {
        run_phase(0, 1);   /* 状态 A */
        vTaskDelay(pdMS_TO_TICKS(TEST_PERIOD_MS));

        run_phase(1, 0);   /* 状态 B */
        vTaskDelay(pdMS_TO_TICKS(TEST_PERIOD_MS));
    }
}
