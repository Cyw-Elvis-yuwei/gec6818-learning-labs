idf_component_register(
    SRCS "main.c" "pca9557.c" "qmi8658.c" "ft6336.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_lcd esp_lcd_st7796
)
