#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "pca9557.h"
#include "qmi8658.h"

/* =====================================================================
 * QMI8658 姿态传感器实验（新版 I2C Master API）
 *
 * 硬件：
 *   GPIO1 → I2C SDA
 *   GPIO2 → I2C SCL
 *   I2C bus 100 kHz，两个设备共用同一 bus：
 *     PCA9557 @ 0x19：仅初始化（驱动不变）
 *     QMI8658 @ 0x6A：初始化 + 每秒打印 XYZ 倾角
 *
 * 实验内容：
 *   qmi8658_init → 每约 1 秒 qmi8658_fetch_angleFromAcc → 串口打印倾角。
 * ===================================================================== */

#define I2C_SDA_GPIO     GPIO_NUM_1
#define I2C_SCL_GPIO     GPIO_NUM_2
#define ANGLE_PRINT_PERIOD_MS 1000   /* 倾角打印周期 1 秒 */

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

    /* 3. 初始化 QMI8658（同一 bus，0x6A；WHO_AM_I 校验 + 寄存器配置） */
    err = qmi8658_init(s_bus);
    if (err != ESP_OK) {
        printf("QMI8658 init failed: %s\n", esp_err_to_name(err));
        return;
    }
    printf("QMI8658 OK\n");

    /* 4. 每约 1 秒读取并打印 XYZ 倾角（读取失败打印错误，不打印旧角度） */
    t_sQMI8658 sensor;
    for (;;) {
        err = qmi8658_fetch_angleFromAcc(&sensor);
        if (err == ESP_OK) {
            printf("angle_x = %.1f  angle_y = %.1f angle_z = %.1f\n",
                   sensor.AngleX, sensor.AngleY, sensor.AngleZ);
        } else {
            printf("QMI8658 read failed: %s\n", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(ANGLE_PRINT_PERIOD_MS));
    }
}
