#ifndef PCA9557_H
#define PCA9557_H

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- PCA9557 寄存器地址（TI 数据手册） ---------- */
#define PCA9557_REG_INPUT        0x00   /* Input Port：读输入电平（1 字节，位 0~7 = IO0~IO7） */
#define PCA9557_REG_OUTPUT       0x01   /* Output Port：写输出电平（1 字节） */
#define PCA9557_REG_POLARITY     0x02   /* Polarity Inversion：1=该位读取时取反 */
#define PCA9557_REG_CONFIG       0x03   /* Configuration：0=输出，1=输入 */

/* ---------- 本实验固定参数 ---------- */
#define PCA9557_I2C_ADDR         0x19   /* PCA9557 板载地址 */
#define PCA9557_I2C_SPEED_HZ     100000 /* 100 kHz */
#define PCA9557_I2C_TIMEOUT_MS   100    /* 单次传输超时 */

/* 方向：IO7..IO0 = 1110 0000 → IO0~IO4=输出，IO5~IO7=输入 */
#define PCA9557_CONFIG_DIR       0xE0

/* 极性：不反转（IO5~IO7 读取与实际物理电平同极性） */
#define PCA9557_POLARITY_NORMAL  0x00

/* 输出安全初值：IO0=1(LCD_CS) IO1=0(PA_EN) IO2=1(DVP_PWDN)，IO3/IO4 初始 0
 * bit2..0 = 101 → 0x05。在切换为输出前先写入 Output Port。 */
#define PCA9557_OUTPUT_SAFE      0x05

/* ---------- IO 编号（位索引，bit0=IO0 ... bit7=IO7） ---------- */
#define PCA9557_IO0  0
#define PCA9557_IO1  1
#define PCA9557_IO2  2
#define PCA9557_IO3  3
#define PCA9557_IO4  4
#define PCA9557_IO5  5
#define PCA9557_IO6  6
#define PCA9557_IO7  7

/**
 * @brief 初始化 PCA9557：在总线上添加设备并完成安全初始化。
 *
 * 初始化顺序（避免先切输出再出现不受控电平）：
 *   1. i2c_master_bus_add_device（地址 0x19，100 kHz）
 *   2. 写 Output Port = 0x05（IO0=1 IO1=0 IO2=1，此刻 IO 仍为输入，不产生电平）
 *   3. 写 Polarity = 0x00（不反转）
 *   4. 最后写 Configuration = 0xE0（IO0~IO4=输出，IO5~IO7=输入）
 *
 * 任一步失败立即返回对应 esp_err_t，不继续。
 *
 * @param[in] bus_handle 由 main.c 创建的新版 I2C master bus 句柄
 * @return ESP_OK 成功；否则为失败步骤的 esp_err_t
 */
esp_err_t pca9557_init(i2c_master_bus_handle_t bus_handle);

/**
 * @brief 设置指定输出 IO（IO0~IO4）电平。
 *
 * 驱动内部维护 Output Port 影子值：只修改目标位后整字节回写，
 * 保证 IO0=1 / IO1=0 / IO2=1 不会被其他 IO 的写操作意外覆盖。
 *
 * @param[in] io    输出 IO 编号（0~4；5~7 为输入，拒绝写入）
 * @param[in] level 0 或 1
 * @return ESP_OK 成功；ESP_ERR_INVALID_ARG 参数非法；ESP_ERR_INVALID_STATE 未初始化
 */
esp_err_t pca9557_set_level(uint8_t io, uint8_t level);

/**
 * @brief 读取指定 IO（IO0~IO7）电平。
 *
 * 读 Input Port 寄存器（极性不反转，读取值与实际电平同极性）。
 *
 * @param[in]  io    IO 编号（0~7）
 * @param[out] level 输出电平 0 或 1
 * @return ESP_OK 成功；ESP_ERR_INVALID_ARG 参数非法；其他为 I2C 传输错误
 */
esp_err_t pca9557_get_level(uint8_t io, uint8_t *level);

#ifdef __cplusplus
}
#endif

#endif /* PCA9557_H */
