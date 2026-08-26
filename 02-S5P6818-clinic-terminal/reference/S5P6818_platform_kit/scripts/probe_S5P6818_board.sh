#!/bin/sh

# Read-only S5P6818 runtime inventory.
# This script never reads input event streams and never writes framebuffer or
# input device nodes.  The only optional write is the caller-selected report
# file supplied with --output.

usage() {
    cat <<'EOF'
Usage: sh probe_s5p6818_board.sh [--output REPORT_FILE]

Collect a read-only operating-system, framebuffer, and input-device inventory.
Unavailable commands and files are reported and do not stop the remaining
checks.  The script does not run evtest and does not read /dev/input/event*.
EOF
}

OUTPUT_FILE=

while [ "$#" -gt 0 ]; do
    case "$1" in
        --output)
            if [ "$#" -lt 2 ] || [ -z "$2" ]; then
                echo "error: --output requires a non-empty file path" >&2
                usage >&2
                exit 2
            fi
            OUTPUT_FILE=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [ -n "$OUTPUT_FILE" ]; then
    case "$OUTPUT_FILE" in
        /dev/fb*|/dev/input/event*)
            echo "error: refusing to use a framebuffer or input device as the output file" >&2
            exit 2
            ;;
    esac

    if [ -c "$OUTPUT_FILE" ] || [ -b "$OUTPUT_FILE" ] || [ -d "$OUTPUT_FILE" ]; then
        echo "error: output must be a regular file, not a device or directory: $OUTPUT_FILE" >&2
        exit 2
    fi

    if ! : >"$OUTPUT_FILE"; then
        echo "error: cannot create output file: $OUTPUT_FILE" >&2
        exit 1
    fi
    exec >"$OUTPUT_FILE" 2>&1
fi

have_command() {
    command -v "$1" >/dev/null 2>&1
}

section() {
    printf '\n== %s ==\n' "$1"
}

unavailable() {
    printf '[unavailable] %s\n' "$1"
}

run_command() {
    label=$1
    shift
    printf -- '-- %s\n' "$label"
    "$@"
    command_status=$?
    if [ "$command_status" -ne 0 ]; then
        printf '[unavailable-or-failed] %s (exit %s)\n' "$label" "$command_status"
    fi
    return 0
}

read_text_file() {
    label=$1
    path=$2
    printf -- '-- %s: %s\n' "$label" "$path"
    if [ -r "$path" ]; then
        if ! cat "$path" 2>&1; then
            printf '[unavailable-or-failed] unable to read %s\n' "$path"
        fi
    else
        unavailable "$path is absent or unreadable"
    fi
}

list_framebuffer_nodes() {
    found=0
    for node in /dev/fb*; do
        if [ -e "$node" ] || [ -L "$node" ]; then
            ls -l "$node" 2>&1 || true
            found=1
        fi
    done
    if [ "$found" -eq 0 ]; then
        unavailable "no /dev/fb* nodes"
    fi
}

list_input_event_nodes() {
    found=0
    for node in /dev/input/event*; do
        if [ -e "$node" ] || [ -L "$node" ]; then
            # Metadata only: never open or read the event stream.
            ls -l "$node" 2>&1 || true
            found=1
        fi
    done
    if [ "$found" -eq 0 ]; then
        unavailable "no /dev/input/event* nodes"
    fi
}

printf '%s\n' 'S5P6818 read-only runtime probe'
printf '%s\n' 'Safety: no device writes, no input event reads, no installs, no legacy programs.'
if have_command date; then
    printf 'generated_at_utc: '
    date -u '+%Y-%m-%dT%H:%M:%SZ' 2>&1 || unavailable "UTC timestamp"
else
    unavailable "date command"
fi

section "Kernel and CPU"
if have_command uname; then
    run_command "uname -a" uname -a
    run_command "uname -m" uname -m
else
    unavailable "uname command"
fi
read_text_file "CPU information" /proc/cpuinfo
if have_command getconf; then
    run_command "getconf LONG_BIT" getconf LONG_BIT
else
    unavailable "getconf command (LONG_BIT not measured)"
fi

section "Operating system and C runtime"
read_text_file "OS release" /etc/os-release
read_text_file "system issue banner" /etc/issue
if have_command ldd; then
    run_command "ldd --version" ldd --version
else
    unavailable "ldd command"
fi
if have_command busybox; then
    run_command "BusyBox version and applet inventory" busybox
else
    unavailable "busybox command"
fi

section "Identity, mounts, and storage"
if have_command id; then
    run_command "id" id
else
    unavailable "id command"
fi
if have_command mount; then
    run_command "mount" mount
else
    unavailable "mount command"
fi
if have_command df; then
    run_command "df -h" df -h
else
    unavailable "df command"
fi

section "Framebuffer device nodes"
if have_command ls; then
    list_framebuffer_nodes
else
    unavailable "ls command"
fi

section "Framebuffer fb0 sysfs attributes"
read_text_file "driver name" /sys/class/graphics/fb0/name
read_text_file "bits per pixel" /sys/class/graphics/fb0/bits_per_pixel
read_text_file "virtual size" /sys/class/graphics/fb0/virtual_size
read_text_file "stride / line length" /sys/class/graphics/fb0/stride
read_text_file "reported modes" /sys/class/graphics/fb0/modes

section "Optional fbset information"
if [ -e /dev/fb0 ] && have_command fbset; then
    # -i requests fixed/variable screen information; no mode is set.
    run_command "fbset -fb /dev/fb0 -i (information only)" fbset -fb /dev/fb0 -i
elif [ ! -e /dev/fb0 ]; then
    unavailable "/dev/fb0 is absent; fbset was not run"
else
    unavailable "fbset command"
fi

section "Input event device nodes"
if have_command ls; then
    list_input_event_nodes
else
    unavailable "ls command"
fi

section "Kernel input-device inventory"
read_text_file "input device descriptions (metadata only)" /proc/bus/input/devices

section "Probe completion"
printf '%s\n' 'completed: all available read-only checks were attempted'
printf '%s\n' 'not performed: evtest, event-stream reads, framebuffer writes, installs, legacy scripts/programs'

exit 0
