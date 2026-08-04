#!/bin/sh

# Create a standalone LVGL 8 platform scaffold.  This script does not download,
# build, install, run, or connect to any target device.

set -u

usage()
{
    cat <<'EOF'
Usage: bootstrap_gec6818_lvgl8_project.sh [--allow-unverified-defaults] <new-directory>

The target directory must not already exist.  By default, generation stops
before creating anything unless every platform-critical profile value is
confirmed-runtime.  --allow-unverified-defaults creates an inspection-only
scaffold with prominent UNVERIFIED and UNKNOWN markers; it never chooses an
unverified compiler candidate silently.
EOF
}

ALLOW_UNVERIFIED=0
TARGET_DIR=

while [ "$#" -gt 0 ]; do
    case "$1" in
        --allow-unverified-defaults)
            ALLOW_UNVERIFIED=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --*)
            echo "ERROR: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
        *)
            if [ -n "$TARGET_DIR" ]; then
                echo "ERROR: exactly one target directory is required." >&2
                usage >&2
                exit 2
            fi
            TARGET_DIR=$1
            shift
            ;;
    esac
done

if [ -z "$TARGET_DIR" ]; then
    echo "ERROR: a target directory is required." >&2
    usage >&2
    exit 2
fi

case "$TARGET_DIR" in
    /|.|..)
        echo "ERROR: refusing unsafe target directory: $TARGET_DIR" >&2
        exit 2
        ;;
esac

SCRIPT_DIR=$(CDPATH= cd -P "$(dirname "$0")" 2>/dev/null && pwd)
if [ -z "$SCRIPT_DIR" ]; then
    echo "ERROR: cannot resolve the script directory." >&2
    exit 2
fi

KIT_DIR=$(CDPATH= cd -P "$SCRIPT_DIR/.." 2>/dev/null && pwd)
if [ -z "$KIT_DIR" ]; then
    echo "ERROR: cannot resolve the platform-kit directory." >&2
    exit 2
fi

PROFILE=$KIT_DIR/gec6818_profile.json
VALIDATOR=$SCRIPT_DIR/validate_profile.sh
TEMPLATE_DIR=$KIT_DIR/templates/lvgl8_minimal

for required_file in \
    "$PROFILE" \
    "$VALIDATOR" \
    "$TEMPLATE_DIR/README.md" \
    "$TEMPLATE_DIR/platform_config.example.h" \
    "$TEMPLATE_DIR/toolchain.example.mk"
do
    if [ ! -f "$required_file" ]; then
        echo "ERROR: required platform-kit file is missing: $required_file" >&2
        exit 2
    fi
done

# Validation occurs before mkdir so the default failure path leaves no target.
if [ "$ALLOW_UNVERIFIED" -eq 1 ]; then
    if ! sh "$VALIDATOR" --allow-unverified-defaults --profile "$PROFILE"; then
        echo "ERROR: profile structure is invalid; no project was created." >&2
        exit 3
    fi
else
    if ! sh "$VALIDATOR" --profile "$PROFILE"; then
        echo "ERROR: critical runtime values are not ready; no project was created." >&2
        echo "For an inspection-only scaffold, rerun with --allow-unverified-defaults." >&2
        exit 3
    fi
fi

if [ -e "$TARGET_DIR" ]; then
    echo "ERROR: target already exists; refusing to overwrite it: $TARGET_DIR" >&2
    exit 2
fi

if command -v python3 >/dev/null 2>&1 && python3 -c 'import sys; raise SystemExit(0 if sys.version_info.major == 3 else 1)' >/dev/null 2>&1; then
    PYTHON=python3
elif command -v python >/dev/null 2>&1 && python -c 'import sys; raise SystemExit(0 if sys.version_info.major == 3 else 1)' >/dev/null 2>&1; then
    PYTHON=python
else
    echo "ERROR: Python 3 is required to generate files from the JSON profile." >&2
    exit 2
fi

TARGET_PARENT=$(dirname "$TARGET_DIR")
TARGET_BASENAME=$(basename "$TARGET_DIR")

if [ -z "$TARGET_BASENAME" ] || [ "$TARGET_BASENAME" = "." ] || [ "$TARGET_BASENAME" = ".." ]; then
    echo "ERROR: target directory has an unsafe final component: $TARGET_DIR" >&2
    exit 2
fi

if ! mkdir -p "$TARGET_PARENT"; then
    echo "ERROR: cannot create target parent directory: $TARGET_PARENT" >&2
    exit 2
fi

TARGET_PARENT_ABS=$(CDPATH= cd -P "$TARGET_PARENT" 2>/dev/null && pwd)
if [ -z "$TARGET_PARENT_ABS" ]; then
    echo "ERROR: cannot resolve target parent directory: $TARGET_PARENT" >&2
    exit 2
fi

TARGET_FINAL=$TARGET_PARENT_ABS/$TARGET_BASENAME
if [ -e "$TARGET_FINAL" ]; then
    echo "ERROR: target already exists; refusing to overwrite it: $TARGET_FINAL" >&2
    exit 2
fi

case "$TARGET_FINAL/" in
    "$KIT_DIR/"*)
        echo "ERROR: target must be independent and cannot be created inside the platform kit." >&2
        exit 2
        ;;
esac

STAGE=$TARGET_PARENT_ABS/.${TARGET_BASENAME}.gec6818-bootstrap.$$
if [ -e "$STAGE" ]; then
    echo "ERROR: temporary staging path unexpectedly exists: $STAGE" >&2
    exit 2
fi

cleanup_stage()
{
    if [ -n "$STAGE" ] && [ -d "$STAGE" ]; then
        # Remove only the exact files this script may create.  No recursive
        # deletion is used, and an unrelated non-empty directory is preserved.
        rm -f \
            "$STAGE/README.md" \
            "$STAGE/gec6818_profile.json" \
            "$STAGE/platform_config.h" \
            "$STAGE/toolchain.mk" \
            "$STAGE/templates/README.md" \
            "$STAGE/templates/platform_config.example.h" \
            "$STAGE/templates/toolchain.example.mk" 2>/dev/null || true
        rmdir "$STAGE/templates" 2>/dev/null || true
        rmdir "$STAGE" 2>/dev/null || true
    fi
}

trap cleanup_stage 0
trap 'exit 130' HUP INT TERM

if ! mkdir "$STAGE" || ! mkdir "$STAGE/templates"; then
    echo "ERROR: cannot create staging directory: $STAGE" >&2
    exit 2
fi

if ! cp "$PROFILE" "$STAGE/gec6818_profile.json" || \
   ! cp "$TEMPLATE_DIR/README.md" "$STAGE/templates/README.md" || \
   ! cp "$TEMPLATE_DIR/platform_config.example.h" "$STAGE/templates/platform_config.example.h" || \
   ! cp "$TEMPLATE_DIR/toolchain.example.mk" "$STAGE/templates/toolchain.example.mk"; then
    echo "ERROR: cannot copy the profile or templates into staging." >&2
    exit 2
fi

if ! "$PYTHON" - "$STAGE/gec6818_profile.json" "$STAGE" "$ALLOW_UNVERIFIED" <<'PY'
import json
import os
import re
import sys


profile_path = sys.argv[1]
output_dir = sys.argv[2]
allow_unverified = sys.argv[3] == "1"

with open(profile_path, "r", encoding="utf-8") as handle:
    profile = json.load(handle)


def get_path(path):
    node = profile
    for component in path.split("."):
        if not isinstance(node, dict) or component not in node:
            return None
        node = node[component]
    return node if isinstance(node, dict) and "status" in node else None


def normalize_name(value):
    return re.sub(r"[^a-z0-9]+", "_", str(value).lower()).strip("_")


all_parameters = []


def collect_parameters(node, path=""):
    if isinstance(node, dict):
        if "status" in node and "value" in node:
            all_parameters.append((path, node))
            return
        for key, value in node.items():
            child = "{}.{}".format(path, key) if path else key
            collect_parameters(value, child)
    elif isinstance(node, list):
        for index, value in enumerate(node):
            collect_parameters(value, "{}[{}]".format(path, index))


collect_parameters(profile)


def find_parameter(paths, names=(), statuses=None):
    for path in paths:
        parameter = get_path(path)
        if parameter is not None and (statuses is None or parameter.get("status") in statuses):
            return path, parameter

    wanted_names = {normalize_name(name) for name in names}
    if wanted_names:
        for path, parameter in all_parameters:
            if normalize_name(parameter.get("name", "")) in wanted_names:
                if statuses is None or parameter.get("status") in statuses:
                    return path, parameter
    return None, None


CRITICAL_REQUIREMENTS = (
    ("toolchains.selected_compiler", "toolchain"),
    ("toolchains.selected_dumpmachine", "toolchain"),
    ("toolchains.selected_sysroot", "toolchain"),
    ("toolchains.selected_default_architecture", "toolchain"),
    ("toolchains.selected_abi", "toolchain"),
    ("toolchains.selected_float_abi", "toolchain"),
    ("display.runtime_device", "board"),
    ("display.runtime_width", "board"),
    ("display.runtime_height", "board"),
    ("display.runtime_bits_per_pixel", "board"),
    ("display.runtime_line_length", "board"),
    ("display.runtime_pixel_format", "board"),
    ("display.runtime_rotation", "board"),
    ("input.runtime_device", "board"),
    ("input.runtime_x_min", "board"),
    ("input.runtime_x_max", "board"),
    ("input.runtime_y_min", "board"),
    ("input.runtime_y_max", "board"),
    ("input.runtime_swap_xy", "board"),
    ("input.runtime_invert_x", "board"),
    ("input.runtime_invert_y", "board"),
    ("input.runtime_event_protocol", "board"),
)


def is_runtime_parameter(path, expected_scope):
    parameter = get_path(path)
    if (not parameter or parameter.get("status") != "confirmed-runtime" or
            parameter.get("scope") != expected_scope):
        return False
    if path == "toolchains.selected_sysroot" and parameter.get("value") == "":
        return True
    return parameter.get("value") is not None and parameter.get("value") != ""


runtime_confirmed = all(
    is_runtime_parameter(path, expected_scope)
    for path, expected_scope in CRITICAL_REQUIREMENTS
)


def choose(runtime_path, fallback_paths, fallback_names, unknown_value):
    runtime = get_path(runtime_path)
    if (runtime and runtime.get("status") == "confirmed-runtime" and
            runtime.get("scope") == "board" and runtime.get("value") not in (None, "")):
        return runtime.get("value"), runtime_path, True

    if allow_unverified:
        fallback_path, fallback = find_parameter(
            fallback_paths,
            fallback_names,
            statuses={"confirmed-source"},
        )
        if fallback is not None and fallback.get("value") not in (None, ""):
            return fallback.get("value"), "UNVERIFIED source default: {}".format(fallback_path), False

    return unknown_value, "UNKNOWN: {} is not confirmed-runtime".format(runtime_path), False


fb_device = choose(
    "display.runtime_device",
    ("display.old_default_device", "display.source_default_device", "display.source_device", "display.defaults.device"),
    ("source framebuffer device", "old framebuffer device", "framebuffer source default"),
    "UNKNOWN",
)
width = choose(
    "display.runtime_width",
    ("display.old_logical_width", "display.source_logical_width", "display.logical_width", "display.defaults.width"),
    ("source logical width", "old logical width", "lvgl horizontal resolution"),
    0,
)
height = choose(
    "display.runtime_height",
    ("display.old_logical_height", "display.source_logical_height", "display.logical_height", "display.defaults.height"),
    ("source logical height", "old logical height", "lvgl vertical resolution"),
    0,
)
bits_per_pixel = choose(
    "display.runtime_bits_per_pixel",
    ("display.old_lvgl_color_depth", "display.source_lvgl_color_depth", "display.lvgl_color_depth", "lvgl.color_depth"),
    ("source lvgl color depth", "lvgl color depth"),
    0,
)
line_length = choose(
    "display.runtime_line_length",
    ("display.source_line_length",),
    ("source line length",),
    0,
)
pixel_format = choose(
    "display.runtime_pixel_format",
    ("display.source_pixel_format",),
    ("source pixel format", "source channel layout"),
    "UNKNOWN",
)
rotation = choose(
    "display.runtime_rotation",
    ("display.source_rotation",),
    ("source rotation",),
    -1,
)

input_device = choose(
    "input.runtime_device",
    ("input.old_default_device", "input.source_default_device", "input.source_device", "input.defaults.device"),
    ("source input device", "old input device", "evdev source default"),
    "UNKNOWN",
)
x_min = choose(
    "input.runtime_x_min",
    ("input.old_x_min", "input.source_x_min", "input.defaults.x_min"),
    ("source x min", "old touch x minimum"),
    0,
)
x_max = choose(
    "input.runtime_x_max",
    ("input.old_x_max", "input.source_x_max", "input.defaults.x_max"),
    ("source x max", "old touch x maximum"),
    0,
)
y_min = choose(
    "input.runtime_y_min",
    ("input.old_y_min", "input.source_y_min", "input.defaults.y_min"),
    ("source y min", "old touch y minimum"),
    0,
)
y_max = choose(
    "input.runtime_y_max",
    ("input.old_y_max", "input.source_y_max", "input.defaults.y_max"),
    ("source y max", "old touch y maximum"),
    0,
)
swap_xy = choose(
    "input.runtime_swap_xy",
    ("input.old_swap_xy", "input.source_swap_xy", "input.defaults.swap_xy"),
    ("source swap xy", "old touch swap xy"),
    -1,
)
invert_x = choose(
    "input.runtime_invert_x",
    ("input.old_invert_x", "input.source_invert_x", "input.defaults.invert_x"),
    ("source invert x", "old touch invert x"),
    -1,
)
invert_y = choose(
    "input.runtime_invert_y",
    ("input.old_invert_y", "input.source_invert_y", "input.defaults.invert_y"),
    ("source invert y", "old touch invert y"),
    -1,
)
event_protocol = choose(
    "input.runtime_event_protocol",
    (),
    (),
    "UNKNOWN",
)


def integer_value(selected, unknown=0):
    value = selected[0]
    if isinstance(value, bool):
        return int(value)
    try:
        return int(value)
    except (TypeError, ValueError):
        return unknown


def boolean_or_unknown(selected):
    value = selected[0]
    if isinstance(value, bool):
        return 1 if value else 0
    if value in (0, 1):
        return value
    return -1


def c_string(value):
    if isinstance(value, (list, tuple)):
        value = ", ".join(str(item) for item in value)
    return json.dumps(str(value), ensure_ascii=True)


def c_comment(value):
    return str(value).replace("*/", "* /").replace("\n", " ")


header = """/* Generated from gec6818_profile.json. */
/* DO NOT treat source defaults as live-board facts. */
#ifndef GEC6818_PLATFORM_CONFIG_H
#define GEC6818_PLATFORM_CONFIG_H

#define GEC6818_PROFILE_RUNTIME_CONFIRMED {runtime_confirmed}
#define GEC6818_PROFILE_SCHEMA_VERSION {schema_version}
#define GEC6818_UNVERIFIED_DEFAULTS_ALLOWED {allow_unverified}
#define GEC6818_UNVERIFIED_VALUES_PRESENT {unverified_present}
#define GEC6818_LVGL_API_MAJOR 8
#define GEC6818_LVGL_RECOMMENDED_VERSION "8.3.0"

#define GEC6818_FBDEV_PATH {fb_device} /* {fb_device_source} */
#define GEC6818_DISPLAY_WIDTH {width} /* {width_source} */
#define GEC6818_DISPLAY_HEIGHT {height} /* {height_source} */
#define GEC6818_FB_BITS_PER_PIXEL {bits_per_pixel} /* {bpp_source} */
#define GEC6818_FB_LINE_LENGTH {line_length} /* {line_length_source} */
#define GEC6818_FB_PIXEL_FORMAT {pixel_format} /* {pixel_format_source} */
#define GEC6818_DISPLAY_ROTATION {rotation} /* -1 means UNKNOWN; {rotation_source} */

#define GEC6818_EVDEV_PATH {input_device} /* {input_device_source} */
#define GEC6818_TOUCH_X_MIN {x_min} /* {x_min_source} */
#define GEC6818_TOUCH_X_MAX {x_max} /* {x_max_source} */
#define GEC6818_TOUCH_Y_MIN {y_min} /* {y_min_source} */
#define GEC6818_TOUCH_Y_MAX {y_max} /* {y_max_source} */
#define GEC6818_TOUCH_SWAP_XY {swap_xy} /* -1 means UNKNOWN; {swap_xy_source} */
#define GEC6818_TOUCH_INVERT_X {invert_x} /* -1 means UNKNOWN; {invert_x_source} */
#define GEC6818_TOUCH_INVERT_Y {invert_y} /* -1 means UNKNOWN; {invert_y_source} */
#define GEC6818_INPUT_EVENT_PROTOCOL {event_protocol} /* {event_protocol_source} */

#if GEC6818_UNVERIFIED_VALUES_PRESENT
/* This header is scaffold data only.  Confirm runtime values before use. */
#endif

#if GEC6818_UNVERIFIED_VALUES_PRESENT && !defined(GEC6818_ACKNOWLEDGE_UNVERIFIED_SCAFFOLD)
#error "UNVERIFIED GEC6818 scaffold: confirm runtime profile values before compilation"
#endif

#if defined(LV_VERSION_MAJOR) && (LV_VERSION_MAJOR != GEC6818_LVGL_API_MAJOR)
#error "This scaffold is for LVGL 8 only; do not mix LVGL 9 APIs"
#endif

#endif /* GEC6818_PLATFORM_CONFIG_H */
""".format(
    runtime_confirmed=1 if runtime_confirmed else 0,
    schema_version=c_string(profile.get("schema_version", "UNKNOWN")),
    allow_unverified=1 if allow_unverified else 0,
    unverified_present=0 if runtime_confirmed else 1,
    fb_device=c_string(fb_device[0]),
    fb_device_source=c_comment(fb_device[1]),
    width=integer_value(width),
    width_source=c_comment(width[1]),
    height=integer_value(height),
    height_source=c_comment(height[1]),
    bits_per_pixel=integer_value(bits_per_pixel),
    bpp_source=c_comment(bits_per_pixel[1]),
    line_length=integer_value(line_length),
    line_length_source=c_comment(line_length[1]),
    pixel_format=c_string(pixel_format[0]),
    pixel_format_source=c_comment(pixel_format[1]),
    rotation=integer_value(rotation, unknown=-1),
    rotation_source=c_comment(rotation[1]),
    input_device=c_string(input_device[0]),
    input_device_source=c_comment(input_device[1]),
    x_min=integer_value(x_min),
    x_min_source=c_comment(x_min[1]),
    x_max=integer_value(x_max),
    x_max_source=c_comment(x_max[1]),
    y_min=integer_value(y_min),
    y_min_source=c_comment(y_min[1]),
    y_max=integer_value(y_max),
    y_max_source=c_comment(y_max[1]),
    swap_xy=boolean_or_unknown(swap_xy),
    swap_xy_source=c_comment(swap_xy[1]),
    invert_x=boolean_or_unknown(invert_x),
    invert_x_source=c_comment(invert_x[1]),
    invert_y=boolean_or_unknown(invert_y),
    invert_y_source=c_comment(invert_y[1]),
    event_protocol=c_string(event_protocol[0]),
    event_protocol_source=c_comment(event_protocol[1]),
)


def runtime_toolchain_value(path):
    parameter = get_path(path)
    if (parameter and parameter.get("status") == "confirmed-runtime" and
            parameter.get("scope") == "toolchain" and parameter.get("value") not in (None, "")):
        return str(parameter.get("value"))
    return ""


compiler = runtime_toolchain_value("toolchains.selected_compiler")
dumpmachine = runtime_toolchain_value("toolchains.selected_dumpmachine")
sysroot = runtime_toolchain_value("toolchains.selected_sysroot")
default_architecture = runtime_toolchain_value("toolchains.selected_default_architecture")
selected_abi = runtime_toolchain_value("toolchains.selected_abi")
float_abi = runtime_toolchain_value("toolchains.selected_float_abi")

candidate_paths = []
candidates = profile.get("toolchains", {}).get("candidates", [])
if isinstance(candidates, list):
    for candidate in candidates:
        if not isinstance(candidate, dict):
            continue
        parameter = candidate.get("compiler_path")
        if not isinstance(parameter, dict):
            continue
        value = parameter.get("value")
        if isinstance(value, str) and value and value not in candidate_paths:
            candidate_paths.append(value)

candidate_lines = "\n".join("#   - {}".format(candidate) for candidate in candidate_paths)
if not candidate_lines:
    candidate_lines = "#   - none recorded"

toolchain = """# Generated from gec6818_profile.json.
# Candidate paths are evidence only; they are never selected automatically.
GEC6818_PROFILE_RUNTIME_CONFIRMED := {runtime_confirmed}
GEC6818_UNVERIFIED_VALUES_PRESENT := {unverified_present}

# Source-recorded compiler candidates (NOT selected):
{candidate_lines}

CROSS_CC ?= {compiler}
TARGET_DUMPMACHINE ?= {dumpmachine}
TARGET_SYSROOT ?= {sysroot}
TARGET_DEFAULT_ARCHITECTURE ?= {default_architecture}
TARGET_ABI ?= {selected_abi}
TARGET_FLOAT_ABI ?= {float_abi}

# An empty confirmed TARGET_SYSROOT means -print-sysroot actually printed an
# empty line.  It adds no --sysroot flag and does not prove target-libc completeness.

ifeq ($(strip $(CROSS_CC)),)
$(error CROSS_CC is unset; select it only after probe_toolchain.sh confirms it)
endif

CC := $(CROSS_CC)
CPPFLAGS ?=
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=

ifneq ($(strip $(TARGET_SYSROOT)),)
CPPFLAGS += --sysroot=$(TARGET_SYSROOT)
endif

ifneq ($(strip $(TARGET_FLOAT_ABI)),)
CFLAGS += -mfloat-abi=$(TARGET_FLOAT_ABI)
endif

# No -static default is provided.  FreeType and all target libraries must be
# rebuilt or selected only after compiler, ABI, and sysroot are confirmed.
# TARGET_DEFAULT_ARCHITECTURE and TARGET_ABI are recorded observations, not
# automatically translated into -mcpu/-march/-mabi flags.
""".format(
    runtime_confirmed=1 if runtime_confirmed else 0,
    unverified_present=0 if runtime_confirmed else 1,
    candidate_lines=candidate_lines,
    compiler=compiler,
    dumpmachine=dumpmachine,
    sysroot=sysroot,
    default_architecture=default_architecture,
    selected_abi=selected_abi,
    float_abi=float_abi,
)

lvgl_path, lvgl_parameter = find_parameter(
    ("lvgl.recommended_version", "lvgl.source_version", "lvgl.version"),
    ("recommended lvgl version", "source lvgl version", "lvgl version"),
    statuses={"confirmed-runtime", "confirmed-source", "inferred"},
)
lvgl_version = lvgl_parameter.get("value") if lvgl_parameter else "UNCONFIRMED"

if runtime_confirmed:
    current_status = "ready-for-platform-integration"
    meaning = "关键工具链、显示和输入参数已由运行时证据确认；该骨架可进入独立的 LVGL 8 平台集成。"
    discuss = "不需要；除非要改变已确认的平台参数或 LVGL 主版本。"
    next_step = "单独引入 LVGL {} 源码并按 platform_config.h 实现最小显示/输入验证。".format(lvgl_version)
    missing = "经许可的中文字体文件、最终 FreeType 构建产物，以及独立项目自己的应用代码。"
else:
    current_status = "scaffold-only-unverified"
    meaning = "这是使用显式 override 生成的检查用骨架；其中包含旧源码默认值或 UNKNOWN 哨兵，不代表真实开发板。"
    discuss = "需要；必须先运行只读探针并把关键字段更新为 confirmed-runtime。"
    next_step = "运行工具链和开发板只读探针，更新 profile，再执行默认验证。"
    missing = "工具链、framebuffer、像素格式、旋转、触摸设备和坐标映射的 confirmed-runtime 证据。"

warning = "" if runtime_confirmed else """
> **UNVERIFIED SCAFFOLD:** 不得编译、部署或当作开发板事实使用。`platform_config.h`
> 中的 source default 仅描述旧源码，`UNKNOWN`/`-1`/`0` 是待确认哨兵。
"""

readme = """# GEC6818 LVGL 8 最小项目骨架

## 中文判断提示

- 当前状态：{current_status}
- 这是什么意思：{meaning}
- 是否还需要继续讨论：{discuss}
- 建议下一步：{next_step}
- 还缺什么：{missing}

{warning}
## 生成边界

本目录只由平台 kit 的 profile 和模板生成。bootstrap 没有下载 LVGL、没有编译、
没有安装软件、没有运行程序、没有访问 `/dev/fb0` 或输入事件流，也没有连接开发板。
它没有复制旧业务源码、对象文件、库、字体或图片。

推荐的 LVGL 主线版本是 `{lvgl_version}`。不要混用 LVGL 9 API。

## 文件

- `gec6818_profile.json`：生成时的平台证据快照；
- `platform_config.h`：运行参数或显式未确认哨兵；
- `toolchain.mk`：不自动选择候选编译器，也不默认静态链接；
- `templates/`：原始最小模板副本，便于审查差异。

## 下一步命令（只读探针和验证）

```sh
KIT_DIR=/path/to/gec6818_platform_kit
sh "$KIT_DIR/scripts/probe_toolchain.sh"

# 在开发板上只读取系统信息；该脚本不读取触摸事件流。
sh "$KIT_DIR/scripts/probe_gec6818_board.sh" --output gec6818_runtime_report.txt

# 人工依据报告更新 kit 中的 profile 后，再执行默认验证。
sh "$KIT_DIR/scripts/validate_profile.sh"
```

在默认验证通过前，不要添加构建、部署或运行命令。中文字体必须另行选择许可证清晰的
文件；FreeType 必须使用最终确认的工具链和 sysroot 重新构建。

## 后续 Codex 标准提示词

> 读取 `gec6818_platform_kit/gec6818_profile.json` 和 `README.md`，仅使用与字段匹配的
> `scope=board` 或 `scope=toolchain` 的 `confirmed-runtime` 平台关键值创建 GEC6818 项目。
> `scope=host-backend` 只属于参考项目证据。不得重新扫描旧 six 工程，不得使用
> `unknown` 参数，不得复制旧业务源码、旧对象、旧库、旧字体或旧图片。
""".format(
    current_status=current_status,
    meaning=meaning,
    discuss=discuss,
    next_step=next_step,
    missing=missing,
    warning=warning,
    lvgl_version=lvgl_version,
)

readme += """

## sysroot 空输出说明

如果 `selected_sysroot` 是 `confirmed-runtime` 但值为空，只表示编译器的
`-print-sysroot` 确实输出空行。`toolchain.mk` 不会自动添加 `--sysroot`，而且该空输出
本身不证明目标 libc、头文件、启动对象或链接库完整；不得改写成猜测路径。
"""

for filename, content in (
    ("platform_config.h", header),
    ("toolchain.mk", toolchain),
    ("README.md", readme),
):
    path = os.path.join(output_dir, filename)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(content)
PY
then
    echo "ERROR: failed to generate project files from the profile." >&2
    exit 3
fi

if [ -e "$TARGET_FINAL" ]; then
    echo "ERROR: target appeared during generation; refusing to overwrite it: $TARGET_FINAL" >&2
    exit 2
fi

if ! mv "$STAGE" "$TARGET_FINAL"; then
    echo "ERROR: cannot move the completed scaffold to: $TARGET_FINAL" >&2
    exit 2
fi

STAGE=
trap - 0 HUP INT TERM

echo "Created standalone GEC6818 LVGL 8 scaffold: $TARGET_FINAL"
if [ "$ALLOW_UNVERIFIED" -eq 1 ]; then
    echo "WARNING: generated with --allow-unverified-defaults." >&2
    echo "WARNING: inspect UNVERIFIED/UNKNOWN markers; do not build or deploy yet." >&2
else
    echo "All platform-critical values were confirmed-runtime at generation time."
fi
