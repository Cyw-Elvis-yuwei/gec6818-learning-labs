#ifndef QMI8658_H
#define QMI8658_H

#include <stdint.h>
#include <stddef.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 本实验固定参数 ---------- */
#define QMI8658_I2C_ADDR           0x6A   /* QMI8658 7 位 I2C 地址 */
#define QMI8658_I2C_SPEED_HZ       100000 /* 100 kHz，与 PCA9557 共用同一 bus */
#define QMI8658_I2C_TIMEOUT_MS     100    /* 单次传输超时 */

#define QMI8658_WHO_AM_I_VALUE     0x05   /* WHO_AM_I 期望芯片 ID */
#define QMI8658_WHO_AM_I_RETRIES   5      /* WHO_AM_I 校验最大重试次数 */
#define QMI8658_WHO_AM_I_RETRY_MS  100    /* 两次重试间隔 */
#define QMI8658_DATA_READY_WAIT_MS 100    /* STATUS0 数据就绪有限等待总时长 */

/* ---------- 寄存器枚举（课件《11-I2C-姿态传感器》1.2.3，与数据手册一致） ---------- */
typedef enum {
    QMI8658_WHO_AM_I       = 0x00,
    QMI8658_REVISION_ID    = 0x01,
    QMI8658_CTRL1          = 0x02,
    QMI8658_CTRL2          = 0x03,
    QMI8658_CTRL3          = 0x04,
    QMI8658_CTRL4          = 0x05,
    QMI8658_CTRL5          = 0x06,
    QMI8658_CTRL6          = 0x07,
    QMI8658_CTRL7          = 0x08,
    QMI8658_CTRL8          = 0x09,
    QMI8658_CTRL9          = 0x0A,
    QMI8658_CATL1_L        = 0x0B,
    QMI8658_CATL1_H        = 0x0C,
    QMI8658_CATL2_L        = 0x0D,
    QMI8658_CATL2_H        = 0x0E,
    QMI8658_CATL3_L        = 0x0F,
    QMI8658_CATL3_H        = 0x10,
    QMI8658_CATL4_L        = 0x11,
    QMI8658_CATL4_H        = 0x12,
    QMI8658_FIFO_WTM_TH    = 0x13,
    QMI8658_FIFO_CTRL      = 0x14,
    QMI8658_FIFO_SMPL_CNT  = 0x15,
    QMI8658_FIFO_STATUS    = 0x16,
    QMI8658_FIFO_DATA      = 0x17,
    QMI8658_STATUSINT      = 0x2D,
    QMI8658_STATUS0        = 0x2E,
    QMI8658_STATUS1        = 0x2F,
    QMI8658_TIMESTAMP_LOW  = 0x30,
    QMI8658_TIMESTAMP_MID  = 0x31,
    QMI8658_TIMESTAMP_HIGH = 0x32,
    QMI8658_TEMP_L         = 0x33,
    QMI8658_TEMP_H         = 0x34,
    QMI8658_AX_L           = 0x35,
    QMI8658_AX_H           = 0x36,
    QMI8658_AY_L           = 0x37,
    QMI8658_AY_H           = 0x38,
    QMI8658_AZ_L           = 0x39,
    QMI8658_AZ_H           = 0x3A,
    QMI8658_GX_L           = 0x3B,
    QMI8658_GX_H           = 0x3C,
    QMI8658_GY_L           = 0x3D,
    QMI8658_GY_H           = 0x3E,
    QMI8658_GZ_L           = 0x3F,
    QMI8658_GZ_H           = 0x40,
    QMI8658_COD_STATUS     = 0x46,
    QMI8658_dQW_L          = 0x49,
    QMI8658_dQW_H          = 0x4A,
    QMI8658_dQX_L          = 0x4B,
    QMI8658_dQX_H          = 0x4C,
    QMI8658_dQY_L          = 0x4D,
    QMI8658_dQY_H          = 0x4E,
    QMI8658_dQZ_L          = 0x4F,
    QMI8658_dQZ_H          = 0x50,
    QMI8658_dVX_L          = 0x51,
    QMI8658_dVX_H          = 0x52,
    QMI8658_dVY_L          = 0x53,
    QMI8658_dVY_H          = 0x54,
    QMI8658_dVZ_L          = 0x55,
    QMI8658_dVZ_H          = 0x56,
    QMI8658_TAP_STATUS     = 0x59,
    QMI8658_RESET          = 0x60,
} qmi8658_reg_t;

/* ---------- 数据结构（课件 1.2.4/1.2.5） ---------- */
typedef struct {
    int16_t acc_x;   /* X 轴加速度原始值 */
    int16_t acc_y;   /* Y 轴加速度原始值 */
    int16_t acc_z;   /* Z 轴加速度原始值 */
    int16_t gyr_x;   /* X 轴陀螺仪原始值 */
    int16_t gyr_y;   /* Y 轴陀螺仪原始值 */
    int16_t gyr_z;   /* Z 轴陀螺仪原始值 */
    float   AngleX;  /* X 轴倾角（度） */
    float   AngleY;  /* Y 轴倾角（度） */
    float   AngleZ;  /* Z 轴倾角（度） */
} t_sQMI8658;

/**
 * @brief 初始化 QMI8658（同一 i2c_master_bus_handle_t 下添加 0x6A 设备）。
 *
 * 流程：添加设备 → WHO_AM_I 校验（最多 5 次，每次间隔 100 ms）→
 *       RESET=0xB0 + 10ms → CTRL1/CTRL7/CTRL2/CTRL3 配置。
 * 任一步失败返回对应 esp_err_t。
 *
 * @param[in] bus_handle 由 main.c 创建的新版 I2C master bus 句柄
 * @return ESP_OK 成功；ESP_ERR_INVALID_ARG / ESP_ERR_INVALID_STATE /
 *         ESP_ERR_NOT_FOUND（WHO_AM_I 5 次仍非 0x05）/ 其他 I2C 传输错误
 */
esp_err_t qmi8658_init(i2c_master_bus_handle_t bus_handle);

/**
 * @brief 向 QMI8658 寄存器写入单字节（课件 qmi8658_register_write_byte）。
 * @param reg_addr 寄存器地址
 * @param data     要写入的数据
 * @return esp_err_t 操作结果
 */
esp_err_t qmi8658_register_write_byte(uint8_t reg_addr, uint8_t data);

/**
 * @brief 读取 QMI8658 寄存器值（课件 qmi8658_register_read）。
 * @param reg_addr 寄存器地址
 * @param data     数据缓冲区指针
 * @param len      要读取的数据长度
 * @return esp_err_t 操作结果
 */
esp_err_t qmi8658_register_read(uint8_t reg_addr, uint8_t *data, size_t len);

/**
 * @brief 读取加速度和陀螺仪原始值（课件 qmi8658_Read_AccAndGry）。
 *
 * 有限等待 STATUS0 & 0x03（总等待约 100 ms）；就绪后从 AX_L 连续读 12 字节。
 * 未就绪返回 ESP_ERR_TIMEOUT，读取失败返回对应错误，不填充结构体。
 *
 * @param[in,out] p 指向 t_sQMI8658 的指针
 * @return ESP_OK 成功；ESP_ERR_TIMEOUT 数据未就绪；其他为 I2C 传输错误
 */
esp_err_t qmi8658_Read_AccAndGry(t_sQMI8658 *p);

/**
 * @brief 根据加速度数据计算 XYZ 轴倾角（课件 qmi8658_fetch_angleFromAcc）。
 *
 * 先调用 qmi8658_Read_AccAndGry；读取失败直接返回错误，不计算倾角。
 * 公式（弧度×57.29578f 转角度）：
 *   AngleX = atan(acc_x / sqrt(acc_y²+acc_z²))
 *   AngleY = atan(acc_y / sqrt(acc_x²+acc_z²))
 *   AngleZ = atan(sqrt(acc_x²+acc_y²) / acc_z)
 *
 * @param[in,out] p 指向 t_sQMI8658 的指针
 * @return ESP_OK 成功；其他为读取/传输错误
 */
esp_err_t qmi8658_fetch_angleFromAcc(t_sQMI8658 *p);

#ifdef __cplusplus
}
#endif

#endif /* QMI8658_H */
