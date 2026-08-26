CC := /usr/local/arm/5.4.0/usr/bin/arm-none-linux-gnueabi-gcc

ROOT_DIR := ../../..
LVGL_DIR := $(ROOT_DIR)/reference/six/six/lvgl
LV_DRIVERS_DIR := $(ROOT_DIR)/reference/six/six/lv_drivers
LV_FREETYPE_DIR := $(LVGL_DIR)/src/extra/libs/freetype
FREETYPE_DIR := $(ROOT_DIR)/reference/six/six/freetypelib
FREETYPE_INCLUDE_DIR := $(FREETYPE_DIR)/include/freetype2
FREETYPE_LIB_DIR := $(FREETYPE_DIR)/lib

BUILD_DIR := $(ROOT_DIR)/build/board
TARGET := $(BUILD_DIR)/clinic_lvgl_smoke

CPPFLAGS := \
	-DLV_CONF_INCLUDE_SIMPLE \
	-DLV_LVGL_H_INCLUDE_SIMPLE \
	-I. \
	-I$(ROOT_DIR)/include \
	-I$(LVGL_DIR) \
	-I$(LV_DRIVERS_DIR) \
	-I$(LV_DRIVERS_DIR)/indev \
	-I$(LV_FREETYPE_DIR) \
	-I$(FREETYPE_INCLUDE_DIR)
CFLAGS := \
	-std=c11 \
	-O2 \
	-Wall \
	-Wextra \
	-Wpedantic \
	-pthread \
	-finput-charset=UTF-8 \
	-fexec-charset=UTF-8 \
	-mcpu=cortex-a15 \
	-mfloat-abi=softfp
LDFLAGS := \
	-L$(FREETYPE_LIB_DIR) \
	-Wl,-rpath,/IOT
LDLIBS := -pthread -lfreetype -lm

APP_SRC := main.c
LV_FREETYPE_SRC := $(LV_FREETYPE_DIR)/lv_freetype.c
LVGL_SRCS := $(filter-out $(LV_FREETYPE_SRC),$(shell find $(LVGL_DIR)/src -type f -name '*.c'))
FBDEV_SRC := $(LV_DRIVERS_DIR)/display/fbdev.c
EVDEV_SRC := $(LV_DRIVERS_DIR)/indev/evdev.c
NET_SRC := $(ROOT_DIR)/common/clinic_net.c
SOURCES := $(APP_SRC) $(LVGL_SRCS) $(LV_FREETYPE_SRC) $(FBDEV_SRC) $(EVDEV_SRC) $(NET_SRC)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES) lv_conf.h lv_drv_conf.h Makefile
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

clean:
	$(RM) $(TARGET)
