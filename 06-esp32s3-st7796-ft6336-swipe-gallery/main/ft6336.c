#include "ft6336.h"

#include <stdbool.h>
#include "esp_log.h"

#define FT6336_REG_TD_STATUS        0x02
#define FT6336_REG_TOUCH1_XH        0x03
#define FT6336_REG_CIPHER_MID       0x9F
#define FT6336_REG_CIPHER_HIGH      0xA3
#define FT6336_REG_FOCALTECH_ID     0xA8

static const char *TAG = "ft6336";
static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t len) {
    if (s_dev == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len,
                                       FT6336_I2C_TIMEOUT_MS);
}

esp_err_t ft6336_init(i2c_master_bus_handle_t bus_handle) {
    if (bus_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dev != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FT6336_I2C_ADDR,
        .scl_speed_hz = FT6336_I2C_SPEED_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t focaltech_id = 0;
    uint8_t cipher_mid_low[2] = { 0 };
    uint8_t cipher_high = 0;
    err = read_regs(FT6336_REG_FOCALTECH_ID, &focaltech_id, 1);
    if (err != ESP_OK) {
        return err;
    }
    err = read_regs(FT6336_REG_CIPHER_MID, cipher_mid_low, sizeof(cipher_mid_low));
    if (err != ESP_OK) {
        return err;
    }
    err = read_regs(FT6336_REG_CIPHER_HIGH, &cipher_high, 1);
    if (err != ESP_OK) {
        return err;
    }

    const bool chip_id_valid = focaltech_id == 0x11 &&
                               cipher_mid_low[0] == 0x26 &&
                               cipher_mid_low[1] <= 0x02 &&
                               cipher_high == 0x64;
    if (!chip_id_valid) {
        ESP_LOGE(TAG, "Unexpected IDs: focal=0x%02X cipher=%02X-%02X-%02X",
                 (unsigned)focaltech_id, (unsigned)cipher_high,
                 (unsigned)cipher_mid_low[0], (unsigned)cipher_mid_low[1]);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "FT6336 detected at I2C address 0x%02X",
             (unsigned)FT6336_I2C_ADDR);
    return ESP_OK;
}

esp_err_t ft6336_read_touch(ft6336_touch_t *touch) {
    if (touch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t status = 0;
    esp_err_t err = read_regs(FT6336_REG_TD_STATUS, &status, 1);
    if (err != ESP_OK) {
        return err;
    }

    touch->count = status & 0x0F;
    touch->x = 0;
    touch->y = 0;
    if (touch->count == 0 || touch->count > 2) {
        touch->count = 0;
        return ESP_OK;
    }

    uint8_t point[4] = { 0 };
    err = read_regs(FT6336_REG_TOUCH1_XH, point, sizeof(point));
    if (err != ESP_OK) {
        return err;
    }
    touch->x = (uint16_t)(((point[0] & 0x0F) << 8) | point[1]);
    touch->y = (uint16_t)(((point[2] & 0x0F) << 8) | point[3]);
    return ESP_OK;
}
