#include "pca9557.h"

/* ---------- 驱动内部状态 ---------- */
static i2c_master_dev_handle_t s_dev = NULL;          /* PCA9557 设备句柄（0x19） */
static uint8_t s_output_shadow = PCA9557_OUTPUT_SAFE; /* Output Port 影子值，初始即安全初值 */

/* ---------- 寄存器读写（新版 I2C master 传输接口） ---------- */

/* 写寄存器：一次 transmit 发送 [寄存器地址, 数据] 两个字节 */
static esp_err_t write_reg(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), PCA9557_I2C_TIMEOUT_MS);
}

/* 读寄存器：先写寄存器地址，再读 1 字节数据（transmit_receive 事务） */
static esp_err_t read_reg(uint8_t reg, uint8_t *value) {
    return i2c_master_transmit_receive(s_dev, &reg, 1, value, 1, PCA9557_I2C_TIMEOUT_MS);
}

/* ---------- 公开 API ---------- */

esp_err_t pca9557_init(i2c_master_bus_handle_t bus_handle) {
    if (bus_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dev != NULL) {
        return ESP_ERR_INVALID_STATE; /* 重复初始化 */
    }

    /* 1. 在总线上添加 PCA9557 设备（7 位地址 0x19，100 kHz） */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCA9557_I2C_ADDR,
        .scl_speed_hz = PCA9557_I2C_SPEED_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    /* 2. 先把 Output Port 准备成安全初值（IO0=1 IO1=0 IO2=1）。
     *    此刻 IO 尚未切换为输出（仍是输入），写 Output 不会产生任何输出电平。 */
    err = write_reg(PCA9557_REG_OUTPUT, PCA9557_OUTPUT_SAFE);
    if (err != ESP_OK) {
        return err;
    }
    s_output_shadow = PCA9557_OUTPUT_SAFE;

    /* 3. 极性不反转：IO5~IO7 读取值与实际物理电平同极性。
     *    不依赖 PCA9557 上电默认值，显式写 0x00。 */
    err = write_reg(PCA9557_REG_POLARITY, PCA9557_POLARITY_NORMAL);
    if (err != ESP_OK) {
        return err;
    }

    /* 4. 最后切换方向：IO0~IO4=输出，IO5~IO7=输入 */
    return write_reg(PCA9557_REG_CONFIG, PCA9557_CONFIG_DIR);
}

esp_err_t pca9557_set_level(uint8_t io, uint8_t level) {
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 只允许写输出 IO0~IO4；IO5~IO7 是输入，拒绝写入 */
    if (io > PCA9557_IO4 || level > 1) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 只改影子值中目标位，其余位（含 IO0=1 IO1=0 IO2=1）原样保留 */
    uint8_t shadow = s_output_shadow;
    if (level) {
        shadow |= (uint8_t)(1u << io);
    } else {
        shadow &= (uint8_t)~(1u << io);
    }

    esp_err_t err = write_reg(PCA9557_REG_OUTPUT, shadow);
    if (err == ESP_OK) {
        s_output_shadow = shadow; /* 写成功才更新影子，失败保持旧值 */
    }
    return err;
}

esp_err_t pca9557_get_level(uint8_t io, uint8_t *level) {
    if (s_dev == NULL || level == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (io > PCA9557_IO7) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t port = 0;
    esp_err_t err = read_reg(PCA9557_REG_INPUT, &port);
    if (err != ESP_OK) {
        return err;
    }
    *level = (uint8_t)((port >> io) & 0x01u);
    return ESP_OK;
}
