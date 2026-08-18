idf_component_register(
    SRCS "main.c" "pca9557.c" "qmi8658.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2c
)
