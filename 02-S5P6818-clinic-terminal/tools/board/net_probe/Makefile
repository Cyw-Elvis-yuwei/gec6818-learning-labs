CC := /usr/local/arm/5.4.0/usr/bin/arm-none-linux-gnueabi-gcc

ROOT_DIR := ../../..
BUILD_DIR := $(ROOT_DIR)/build/board
TARGET := $(BUILD_DIR)/clinic_net_probe

CPPFLAGS := -I$(ROOT_DIR)/include
CFLAGS := \
	-std=c11 \
	-O2 \
	-Wall \
	-Wextra \
	-Wpedantic \
	-mcpu=cortex-a15 \
	-mfloat-abi=softfp

SOURCES := main.c $(ROOT_DIR)/common/clinic_net.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES) Makefile
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@

clean:
	$(RM) $(TARGET)
