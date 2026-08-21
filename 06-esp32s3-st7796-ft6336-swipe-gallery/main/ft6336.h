#ifndef FT6336_H
#define FT6336_H

#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FT6336_I2C_ADDR       0x38
#define FT6336_I2C_SPEED_HZ   100000
#define FT6336_I2C_TIMEOUT_MS 100

typedef struct {
    uint8_t count;
    uint16_t x;
    uint16_t y;
} ft6336_touch_t;

esp_err_t ft6336_init(i2c_master_bus_handle_t bus_handle);
esp_err_t ft6336_read_touch(ft6336_touch_t *touch);

#ifdef __cplusplus
}
#endif

#endif
