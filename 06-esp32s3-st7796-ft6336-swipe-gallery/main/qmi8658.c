#include "qmi8658.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---------- 驱动内部状态 ---------- */
static i2c_master_dev_handle_t s_dev = NULL; /* QMI8658 设备句柄（0x6A） */

/* ---------- 寄存器读写（新版 I2C master 传输接口） ---------- */

esp_err_t qmi8658_register_write_byte(uint8_t reg_addr, uint8_t data) {
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[2] = { reg_addr, data };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), QMI8658_I2C_TIMEOUT_MS);
}

esp_err_t qmi8658_register_read(uint8_t reg_addr, uint8_t *data, size_t len) {
    if (s_dev == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(s_dev, &reg_addr, 1, data, len, QMI8658_I2C_TIMEOUT_MS);
}

/* ---------- 公开 API ---------- */

esp_err_t qmi8658_init(i2c_master_bus_handle_t bus_handle) {
    if (bus_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dev != NULL) {
        return ESP_ERR_INVALID_STATE; /* 重复初始化 */
    }

    /* 1. 在总线上添加 QMI8658 设备（7 位地址 0x6A，100 kHz） */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = QMI8658_I2C_ADDR,
        .scl_speed_hz = QMI8658_I2C_SPEED_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    /* 2. WHO_AM_I 校验：最多重试 5 次，每次间隔约 100 ms */
    uint8_t id = 0;
    err = ESP_FAIL;
    for (int i = 0; i < QMI8658_WHO_AM_I_RETRIES; i++) {
        err = qmi8658_register_read(QMI8658_WHO_AM_I, &id, 1);
        if (err == ESP_OK && id == QMI8658_WHO_AM_I_VALUE) {
            break; /* 校验成功 */
        }
        if (i < QMI8658_WHO_AM_I_RETRIES - 1) {
            vTaskDelay(pdMS_TO_TICKS(QMI8658_WHO_AM_I_RETRY_MS));
        }
    }
    if (err != ESP_OK) {
        return err;               /* 读取失败：返回传输错误 */
    }
    if (id != QMI8658_WHO_AM_I_VALUE) {
        return ESP_ERR_NOT_FOUND; /* 5 次后仍非 0x05 */
    }

    /* 3. 芯片复位，等待 10 ms */
    err = qmi8658_register_write_byte(QMI8658_RESET, 0xB0);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 4. 配置控制寄存器：先配 CTRL1（地址自动递增），再配 Acc/Gyro，
     *    最后 CTRL7 启动 Acc + Gyro，启动后等待约 200 ms 稳定。 */
    err = qmi8658_register_write_byte(QMI8658_CTRL1, 0x40); /* 地址自动递增模式 */
    if (err != ESP_OK) {
        return err;
    }
    err = qmi8658_register_write_byte(QMI8658_CTRL2, 0x15); /* 加速度计配置 */
    if (err != ESP_OK) {
        return err;
    }
    err = qmi8658_register_write_byte(QMI8658_CTRL3, 0x55); /* 陀螺仪配置 */
    if (err != ESP_OK) {
        return err;
    }
    err = qmi8658_register_write_byte(QMI8658_CTRL7, 0x03); /* 启用加速度计 + 陀螺仪 */
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(200)); /* 启动稳定等待约 200 ms */

    return ESP_OK;
}

esp_err_t qmi8658_Read_AccAndGry(t_sQMI8658 *p) {
    if (s_dev == NULL || p == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 有限等待 STATUS0 低 2 位就绪，总等待约 100 ms */
    uint8_t status = 0;
    int waited_ms = 0;
    while (waited_ms < QMI8658_DATA_READY_WAIT_MS) {
        esp_err_t err = qmi8658_register_read(QMI8658_STATUS0, &status, 1);
        if (err != ESP_OK) {
            return err;
        }
        if (status & 0x03) {
            break; /* 加速度/陀螺仪数据就绪 */
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        waited_ms += 10;
    }
    if (!(status & 0x03)) {
        return ESP_ERR_TIMEOUT; /* 100 ms 内未就绪 */
    }

    /* 从 AX_L 开始连续读取 12 字节（6 个 int16，低字节在前） */
    int16_t buf[6];
    esp_err_t err = qmi8658_register_read(QMI8658_AX_L, (uint8_t *)buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }

    p->acc_x = buf[0]; /* X 轴加速度 */
    p->acc_y = buf[1]; /* Y 轴加速度 */
    p->acc_z = buf[2]; /* Z 轴加速度 */
    p->gyr_x = buf[3]; /* X 轴陀螺仪 */
    p->gyr_y = buf[4]; /* Y 轴陀螺仪 */
    p->gyr_z = buf[5]; /* Z 轴陀螺仪 */
    return ESP_OK;
}

esp_err_t qmi8658_fetch_angleFromAcc(t_sQMI8658 *p) {
    if (p == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 读取失败直接返回，不计算/不产生旧角度 */
    esp_err_t err = qmi8658_Read_AccAndGry(p);
    if (err != ESP_OK) {
        return err;
    }

    float temp; /* 临时计算变量 */

    /* X 轴倾角：atan(acc_x / sqrt(acc_y² + acc_z²))，弧度×57.29578f 转角度 */
    temp = (float)p->acc_x / sqrtf((float)p->acc_y * (float)p->acc_y +
                                   (float)p->acc_z * (float)p->acc_z);
    p->AngleX = atanf(temp) * 57.29578f;

    /* Y 轴倾角：atan(acc_y / sqrt(acc_x² + acc_z²)) */
    temp = (float)p->acc_y / sqrtf((float)p->acc_x * (float)p->acc_x +
                                   (float)p->acc_z * (float)p->acc_z);
    p->AngleY = atanf(temp) * 57.29578f;

    /* Z 轴倾角：atan(sqrt(acc_x² + acc_y²) / acc_z) */
    temp = sqrtf((float)p->acc_x * (float)p->acc_x +
                 (float)p->acc_y * (float)p->acc_y) / (float)p->acc_z;
    p->AngleZ = atanf(temp) * 57.29578f;

    return ESP_OK;
}
