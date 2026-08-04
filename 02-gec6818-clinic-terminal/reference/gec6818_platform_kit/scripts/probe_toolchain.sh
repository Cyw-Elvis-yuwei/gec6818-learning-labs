#!/bin/sh

# Inspect only the three documented GEC6818 compiler candidates.  This script
# never links or runs target code.  Its only compilation is a tiny -c test in a
# temporary /tmp directory that is removed on exit.

usage() {
    cat <<'EOF'
Usage: sh probe_toolchain.sh [--output REPORT_FILE]

Inspect the three documented GEC6818 compiler candidates.  For each available
compiler, report its resolved path, version, target, sysroot, include search
path, libgcc, multilib/float-ABI evidence, and minimal compile-only results.
No linking, LVGL/FreeType build, installation, or target execution is done.
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

run_probe_command() {
    label=$1
    shift
    printf -- '-- %s\n' "$label"
    "$@" 2>&1
    probe_status=$?
    if [ "$probe_status" -ne 0 ]; then
        printf '[unavailable-or-failed] %s (exit %s)\n' "$label" "$probe_status"
    fi
    return 0
}

resolve_compiler() {
    compiler_candidate=$1
    case "$compiler_candidate" in
        */*)
            if [ -x "$compiler_candidate" ] && [ ! -d "$compiler_candidate" ]; then
                printf '%s\n' "$compiler_candidate"
                return 0
            fi
            ;;
        *)
            resolved_from_path=$(command -v "$compiler_candidate" 2>/dev/null || true)
            if [ -n "$resolved_from_path" ] && [ -x "$resolved_from_path" ]; then
                printf '%s\n' "$resolved_from_path"
                return 0
            fi
            ;;
    esac
    return 1
}

canonical_path() {
    path_to_resolve=$1
    if have_command readlink; then
        resolved_path=$(readlink -f "$path_to_resolve" 2>/dev/null || true)
        if [ -n "$resolved_path" ]; then
            printf '%s\n' "$resolved_path"
            return 0
        fi
    fi
    printf '%s\n' "$path_to_resolve"
}

TEMP_DIR=
PROBE_SOURCE=

cleanup_temp() {
    case "$TEMP_DIR" in
        /tmp/gec6818-toolchain-probe.*)
            if [ -d "$TEMP_DIR" ]; then
                rm -rf "$TEMP_DIR"
            fi
            ;;
    esac
}

trap 'cleanup_temp' 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

create_temp_source() {
    umask 077
    if have_command mktemp; then
        TEMP_DIR=$(mktemp -d /tmp/gec6818-toolchain-probe.XXXXXX 2>/dev/null || true)
    fi
    if [ -z "$TEMP_DIR" ]; then
        TEMP_DIR=/tmp/gec6818-toolchain-probe.$$
        if ! mkdir "$TEMP_DIR" 2>/dev/null; then
            TEMP_DIR=
            return 1
        fi
    fi

    PROBE_SOURCE=$TEMP_DIR/minimal_probe.c
    if ! cat >"$PROBE_SOURCE" <<'EOF'
int gec6818_toolchain_probe_add(int left, int right)
{
    return left + right;
}
EOF
    then
        cleanup_temp
        TEMP_DIR=
        PROBE_SOURCE=
        return 1
    fi
    return 0
}

compile_only_test() {
    compiler=$1
    candidate_number=$2
    abi_label=$3
    abi_flag=$4
    object_file=$TEMP_DIR/candidate_${candidate_number}_${abi_label}.o

    if [ -z "$PROBE_SOURCE" ] || [ ! -f "$PROBE_SOURCE" ]; then
        printf '[unavailable] compile-only %s: no safe /tmp workspace\n' "$abi_label"
        return 0
    fi

    printf -- '-- compile-only %s' "$abi_label"
    if [ -n "$abi_flag" ]; then
        printf ' (%s)' "$abi_flag"
    fi
    printf '\n'

    if [ -n "$abi_flag" ]; then
        "$compiler" "$abi_flag" -c "$PROBE_SOURCE" -o "$object_file" 2>&1
    else
        "$compiler" -c "$PROBE_SOURCE" -o "$object_file" 2>&1
    fi
    compile_status=$?

    if [ "$compile_status" -eq 0 ] && [ -f "$object_file" ]; then
        printf '[pass] compiler accepted the minimal C translation unit with -c; no link was attempted\n'
        if have_command file; then
            file "$object_file" 2>&1 || true
        fi
    else
        printf '[fail] compile-only %s (exit %s); this does not by itself prove link/runtime incompatibility\n' \
            "$abi_label" "$compile_status"
    fi
    rm -f "$object_file"
    return 0
}

inspect_compiler() {
    candidate=$1
    candidate_number=$2

    section "Compiler candidate $candidate_number"
    printf 'configured_candidate: %s\n' "$candidate"

    compiler_path=$(resolve_compiler "$candidate" 2>/dev/null || true)
    if [ -z "$compiler_path" ]; then
        printf '%s\n' 'availability: unavailable'
        printf '%s\n' 'result: candidate was not found as an executable; remaining checks skipped'
        return 0
    fi

    actual_path=$(canonical_path "$compiler_path")
    printf '%s\n' 'availability: available'
    printf 'resolved_path: %s\n' "$compiler_path"
    printf 'actual_path: %s\n' "$actual_path"

    run_probe_command "compiler --version" "$compiler_path" --version
    run_probe_command "compiler -dumpmachine" "$compiler_path" -dumpmachine

    printf -- '-- compiler -print-sysroot\n'
    sysroot_value=$("$compiler_path" -print-sysroot 2>&1)
    sysroot_status=$?
    if [ "$sysroot_status" -eq 0 ]; then
        if [ -n "$sysroot_value" ]; then
            printf '%s\n' "$sysroot_value"
            if [ -d "$sysroot_value" ]; then
                printf '%s\n' 'sysroot_directory_exists: yes'
            else
                printf '%s\n' 'sysroot_directory_exists: no'
            fi
        else
            printf '%s\n' '[empty] compiler reported no explicit sysroot'
            printf '%s\n' 'sysroot_directory_exists: not-applicable'
        fi
    else
        printf '%s\n' "$sysroot_value"
        printf '[unavailable-or-failed] compiler -print-sysroot (exit %s)\n' "$sysroot_status"
    fi

    run_probe_command "compiler -print-search-dirs" "$compiler_path" -print-search-dirs

    printf -- '-- compiler include search path (-E -x c -v; preprocessing only)\n'
    printf '%s\n' '#include <stddef.h>' | "$compiler_path" -E -x c -v - >/dev/null
    include_status=$?
    if [ "$include_status" -ne 0 ]; then
        printf '[unavailable-or-failed] include search probe (exit %s)\n' "$include_status"
    fi

    printf -- '-- compiler -print-libgcc-file-name\n'
    libgcc_path=$("$compiler_path" -print-libgcc-file-name 2>&1)
    libgcc_status=$?
    printf '%s\n' "$libgcc_path"
    if [ "$libgcc_status" -eq 0 ] && [ -f "$libgcc_path" ]; then
        if have_command file; then
            run_probe_command "file reported libgcc architecture" file "$libgcc_path"
        else
            printf '%s\n' '[unavailable] file command; libgcc architecture not decoded'
        fi
        if have_command readelf; then
            run_probe_command "readelf -h libgcc" readelf -h "$libgcc_path"
        else
            printf '%s\n' '[unavailable] readelf command; libgcc ELF header not decoded'
        fi
    elif [ "$libgcc_status" -ne 0 ]; then
        printf '[unavailable-or-failed] compiler -print-libgcc-file-name (exit %s)\n' "$libgcc_status"
    else
        printf '%s\n' '[unavailable] reported libgcc path is not a regular file'
    fi

    run_probe_command "compiler -print-multi-lib (float-ABI/multilib evidence)" \
        "$compiler_path" -print-multi-lib

    printf -- '-- target option state filtered for float ABI/FPU (-Q --help=target)\n'
    "$compiler_path" -Q --help=target 2>&1 | grep -E 'mfloat-abi|mfpu|float|fpu' 2>&1
    option_status=$?
    if [ "$option_status" -ne 0 ]; then
        printf '[unavailable-or-failed] no float-ABI/FPU option lines reported (exit %s)\n' "$option_status"
    fi

    printf -- '-- predefined float-ABI-related macros (preprocessing only)\n'
    printf '%s\n' '' | "$compiler_path" -dM -E -x c - 2>/dev/null | \
        grep -E '__SOFTFP__|__ARM_PCS_VFP|__VFP_FP__|__ARM_FP' 2>&1
    macro_status=$?
    if [ "$macro_status" -ne 0 ]; then
        printf '%s\n' '[none-reported] no selected float-ABI macros matched'
    fi

    printf '%s\n' 'compile_test_scope: front-end/assembler acceptance only; no link or runtime claim'
    compile_only_test "$compiler_path" "$candidate_number" default ""
    compile_only_test "$compiler_path" "$candidate_number" soft "-mfloat-abi=soft"
    compile_only_test "$compiler_path" "$candidate_number" softfp "-mfloat-abi=softfp"
    compile_only_test "$compiler_path" "$candidate_number" hard "-mfloat-abi=hard"
}

printf '%s\n' 'GEC6818 toolchain probe'
printf '%s\n' 'Safety: compiler metadata, preprocessing, and -c only; no linking or target execution.'
printf '%s\n' 'Candidates: exactly the three documented paths/names; no automatic compiler substitution.'
if have_command date; then
    printf 'generated_at_utc: '
    date -u '+%Y-%m-%dT%H:%M:%SZ' 2>&1 || true
fi

if create_temp_source; then
    printf 'temporary_compile_workspace: %s (removed automatically)\n' "$TEMP_DIR"
else
    printf '%s\n' '[unavailable] could not create /tmp workspace; metadata probes continue without compile tests'
fi

inspect_compiler "/usr/local/arm/5.4.0/usr/bin/arm-none-linux-gnueabi-gcc" 1
inspect_compiler "/usr/bin/arm-linux-gnueabi-gcc" 2
inspect_compiler "arm-linux-gcc" 3

section "Probe completion"
printf '%s\n' 'completed: all three documented candidates were considered'
printf '%s\n' 'interpretation: compile-only success does not prove sysroot, ABI, library, link, or board compatibility'
printf '%s\n' 'not performed: linking, static linking, LVGL/FreeType build, installation, target execution'

exit 0
