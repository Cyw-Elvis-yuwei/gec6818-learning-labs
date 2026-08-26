#ifndef LV_DRV_CONF_H
#define LV_DRV_CONF_H

/* The smoke test uses the Linux framebuffer and evdev drivers. */
#define USE_FBDEV 1
#define FBDEV_PATH "/dev/fb0"
#define USE_BSD_FBDEV 0

#define USE_SDL 0
#define USE_SDL_GPU 0
#define USE_MONITOR 0
#define USE_WINDOWS 0
#define USE_WIN32DRV 0
#define USE_GTK 0
#define USE_WAYLAND 0
#define USE_SSD1963 0
#define USE_R61581 0
#define USE_ST7565 0
#define USE_GC9A01 0
#define USE_UC1610 0
#define USE_SHARP_MIP 0
#define USE_ILI9341 0
#define USE_DRM 0

#define USE_XPT2046 0
#define USE_FT5406EE8 0
#define USE_AD_TOUCH 0
#define USE_MOUSE 0
#define USE_MOUSEWHEEL 0
#define USE_LIBINPUT 0
#define USE_BSD_LIBINPUT 0
#define USE_EVDEV 1
#define USE_BSD_EVDEV 0
#define EVDEV_NAME "/dev/input/event0"
#define EVDEV_SWAP_AXES 0
#define EVDEV_CALIBRATE 1
#define EVDEV_HOR_MIN 0
#define EVDEV_HOR_MAX 1024
#define EVDEV_VER_MIN 0
#define EVDEV_VER_MAX 600
#define USE_XKB 0
#define USE_KEYBOARD 0

#endif /* LV_DRV_CONF_H */
