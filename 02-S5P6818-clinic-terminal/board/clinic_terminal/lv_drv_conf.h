/* LVGL driver configuration for the S5P6818 clinic terminal. */
#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H

#include "lvgl.h"

#define USE_FBDEV 1
#if USE_FBDEV
#define FBDEV_PATH "/dev/fb0"
#endif

#define USE_EVDEV 1
#if USE_EVDEV
#define EVDEV_NAME "/dev/input/event0"
#define EVDEV_SWAP_AXES 0
#define EVDEV_CALIBRATE 1
#if EVDEV_CALIBRATE
#define EVDEV_HOR_MIN 0
#define EVDEV_HOR_MAX 1024
#define EVDEV_VER_MIN 0
#define EVDEV_VER_MAX 600
#endif
#endif

#define USE_SDL 0
#define USE_DRM 0
#define USE_WAYLAND 0
#define USE_X11 0
#define USE_NUTTX 0

#define USE_MOUSE 0
#define USE_MOUSEWHEEL 0
#define USE_KEYBOARD 0

#define USE_MONITOR 0
#define USE_LINUX_DRM 0
#define USE_GTK 0

#define USE_GENERIC_MIPI 0
#define USE_ILI9341 0
#define USE_ST7735S 0
#define USE_SHARP_MIP 0
#define USE_UC1610 0
#define USE_SSD1306 0
#define USE_R61581 0
#define USE_ST7565 0
#define USE_GC9A01 0

#define USE_AD_TOUCH 0
#define USE_XPT2046 0
#define USE_FT5406EE8 0
#define USE_LIBINPUT 0
#define USE_BSD_EVDEV 0
#define USE_XKB 0

#endif
