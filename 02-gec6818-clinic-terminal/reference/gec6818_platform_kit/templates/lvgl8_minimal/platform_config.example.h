#ifndef GEC6818_PLATFORM_CONFIG_H
#define GEC6818_PLATFORM_CONFIG_H

/*
 * Safe example only. Populate display/input values only from scope=board
 * confirmed-runtime fields in gec6818_profile.json. Host-backend tests and
 * toolchain-only evidence cannot confirm this hardware configuration.
 * Zero, empty, UNKNOWN, and -1 mean unresolved.
 */
#define GEC6818_PROFILE_SCHEMA_VERSION "1.1.0"
#define GEC6818_LVGL_API_MAJOR 8
#define GEC6818_LVGL_RECOMMENDED_VERSION "8.3.0"

#define GEC6818_PLATFORM_CONFIG_RUNTIME_CONFIRMED 0
#define GEC6818_UNVERIFIED_DEFAULTS 0

#define GEC6818_DISPLAY_WIDTH 0
#define GEC6818_DISPLAY_HEIGHT 0
#define GEC6818_DISPLAY_BITS_PER_PIXEL 0
#define GEC6818_DISPLAY_LINE_LENGTH 0
#define GEC6818_DISPLAY_PIXEL_FORMAT "UNKNOWN"
#define GEC6818_DISPLAY_ROTATION_DEGREES -1
#define GEC6818_FBDEV_PATH ""

#define GEC6818_INPUT_EVENT_PATH ""
#define GEC6818_INPUT_X_MIN 0
#define GEC6818_INPUT_X_MAX 0
#define GEC6818_INPUT_Y_MIN 0
#define GEC6818_INPUT_Y_MAX 0
#define GEC6818_INPUT_SWAP_XY -1
#define GEC6818_INPUT_INVERT_X -1
#define GEC6818_INPUT_INVERT_Y -1
#define GEC6818_INPUT_EVENT_PROTOCOL "UNKNOWN"

#define GEC6818_CJK_FONT_PATH ""

#if !GEC6818_PLATFORM_CONFIG_RUNTIME_CONFIRMED && !GEC6818_UNVERIFIED_DEFAULTS
#error "Populate GEC6818 hardware settings from scope=board confirmed-runtime profile values"
#endif

#if defined(LV_VERSION_MAJOR) && (LV_VERSION_MAJOR != GEC6818_LVGL_API_MAJOR)
#error "This platform template is for LVGL 8 only; do not mix LVGL 9 APIs"
#endif

#endif /* GEC6818_PLATFORM_CONFIG_H */
