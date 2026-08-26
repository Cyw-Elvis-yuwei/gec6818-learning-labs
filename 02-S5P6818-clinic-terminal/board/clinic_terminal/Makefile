CC := /usr/local/arm/5.4.0/usr/bin/arm-none-linux-gnueabi-gcc

ROOT_DIR := ../..
LVGL_PARENT_DIR := $(ROOT_DIR)/reference/six/six
LVGL_DIR := $(LVGL_PARENT_DIR)/lvgl
LV_DRIVERS_DIR := $(ROOT_DIR)/reference/six/six/lv_drivers
FREETYPE_ROOT := $(ROOT_DIR)/reference/six/six/freetypelib
FREETYPE_INCLUDE := $(FREETYPE_ROOT)/include
FREETYPE_LIB := $(FREETYPE_ROOT)/lib
CJSON_DIR := $(ROOT_DIR)/third_party/cjson

BUILD_DIR := $(ROOT_DIR)/build/board
TARGET := $(BUILD_DIR)/clinic_terminal

CPPFLAGS := \
	-I. \
	-I$(LVGL_PARENT_DIR) \
	-I$(LVGL_DIR) \
	-I$(LVGL_DIR)/src \
	-I$(LV_DRIVERS_DIR) \
	-I$(ROOT_DIR)/include \
	-I$(CJSON_DIR) \
	-I$(FREETYPE_INCLUDE) \
	-I$(FREETYPE_INCLUDE)/freetype2

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

LDFLAGS := -L$(FREETYPE_LIB) -Wl,-rpath,/IOT
LDLIBS := -pthread -lfreetype -lm

LVGL_FREETYPE_SRC := $(LVGL_DIR)/src/extra/libs/freetype/lv_freetype.c
LVGL_SRC := $(filter-out $(LVGL_FREETYPE_SRC),$(shell find $(LVGL_DIR)/src -type f -name '*.c'))
CJSON_SRC := $(CJSON_DIR)/cJSON.c
DRIVER_SRC := \
	$(LV_DRIVERS_DIR)/display/fbdev.c \
	$(LV_DRIVERS_DIR)/indev/evdev.c
COMMON_SRC := \
	$(ROOT_DIR)/common/clinic_net.c \
	$(ROOT_DIR)/common/clinic_frame.c
SRC := main.c home_page.c department_page.c doctor_page.c ticket_page.c \
	queue_page.c register_page.c \
	board_transport.c login_client.c department_client.c doctor_client.c \
	ticket_client.c \
	pinyin_guard.c credential_store.c \
	$(COMMON_SRC) $(CJSON_SRC) $(LVGL_SRC) $(LVGL_FREETYPE_SRC) $(DRIVER_SRC)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) board_transport.h login_client.h department_client.h doctor_client.h \
	ticket_client.h home_page.h department_page.h doctor_page.h ticket_page.h \
	queue_page.h register_page.h pinyin_guard.h credential_store.h \
	service_flow.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) $(LDFLAGS) $(LDLIBS) -o $@

clean:
	rm -f $(TARGET)
