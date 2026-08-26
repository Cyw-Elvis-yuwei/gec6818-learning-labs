#!/bin/sh

# Validate the platform profile without probing hardware or changing it.

set -u

usage()
{
    cat <<'EOF'
Usage: validate_profile.sh [--allow-unverified-defaults] [--profile FILE]

By default, structural errors and any platform-critical field that is not
confirmed-runtime cause a non-zero exit.  --allow-unverified-defaults relaxes
only the second rule; structural validation remains mandatory and every
unverified critical field is printed as a warning.
EOF
}

ALLOW_UNVERIFIED=0
PROFILE_PATH=

while [ "$#" -gt 0 ]; do
    case "$1" in
        --allow-unverified-defaults)
            ALLOW_UNVERIFIED=1
            shift
            ;;
        --profile)
            if [ "$#" -lt 2 ]; then
                echo "ERROR: --profile requires a file path." >&2
                usage >&2
                exit 2
            fi
            PROFILE_PATH=$2
            shift 2
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
            if [ -n "$PROFILE_PATH" ]; then
                echo "ERROR: only one profile file may be supplied." >&2
                usage >&2
                exit 2
            fi
            PROFILE_PATH=$1
            shift
            ;;
    esac
done

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

if [ -z "$PROFILE_PATH" ]; then
    PROFILE_PATH=$KIT_DIR/s5p6818_profile.json
fi

if [ ! -f "$PROFILE_PATH" ]; then
    echo "ERROR: profile does not exist: $PROFILE_PATH" >&2
    exit 2
fi

if command -v python3 >/dev/null 2>&1 && python3 -c 'import sys; raise SystemExit(0 if sys.version_info.major == 3 else 1)' >/dev/null 2>&1; then
    PYTHON=python3
elif command -v python >/dev/null 2>&1 && python -c 'import sys; raise SystemExit(0 if sys.version_info.major == 3 else 1)' >/dev/null 2>&1; then
    PYTHON=python
else
    echo "ERROR: Python 3 is required to parse and validate the JSON profile." >&2
    exit 2
fi

"$PYTHON" - "$PROFILE_PATH" "$ALLOW_UNVERIFIED" <<'PY'
import json
import os
import re
import sys


profile_path = sys.argv[1]
allow_unverified = sys.argv[2] == "1"

ALLOWED_STATUSES = {
    "confirmed-runtime",
    "confirmed-source",
    "inferred",
    "unknown",
}

PARAMETER_FIELDS = {
    "name",
    "value",
    "status",
    "source",
    "risk",
    "verification_command",
}

PARAMETER_MARKER_FIELDS = PARAMETER_FIELDS | {"scope"}

ALLOWED_SCOPES = {
    "board",
    "toolchain",
    "host-backend",
}

REQUIRED_TOP_LEVEL = (
    "schema_version",
    "generated_at",
    "scope_definitions",
    "board",
    "operating_system",
    "toolchains",
    "display",
    "input",
    "lvgl",
    "fonts",
    "networking",
    "deployment",
    "known_risks",
    "unresolved_items",
    "evidence_sources",
)

# These fields may affect code generation, binary compatibility, memory
# addressing, or touch-coordinate correctness.  Source defaults are evidence
# about the archived project, not evidence about the live board.
CRITICAL_RUNTIME_REQUIREMENTS = (
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

# Some cross-compilers legitimately print an empty line for -print-sysroot.
# confirmed-runtime plus an empty value records that observed result; it does
# not imply a guessed path or prove that a complete target libc is available.
EMPTY_CONFIRMED_RUNTIME_PATHS = {
    "toolchains.selected_sysroot",
}


def display_value(value):
    if value is None:
        return "null"
    rendered = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    if len(rendered) > 120:
        rendered = rendered[:117] + "..."
    return rendered


try:
    with open(profile_path, "r", encoding="utf-8") as handle:
        profile = json.load(handle)
except (OSError, UnicodeError, json.JSONDecodeError) as exc:
    print("ERROR: cannot parse profile as UTF-8 JSON: {}".format(exc), file=sys.stderr)
    sys.exit(3)

errors = []
parameters = {}

if not isinstance(profile, dict):
    errors.append("the JSON root must be an object")
else:
    for key in REQUIRED_TOP_LEVEL:
        if key not in profile:
            errors.append("missing top-level field: {}".format(key))

if isinstance(profile, dict):
    schema_version = profile.get("schema_version")
    if not isinstance(schema_version, str) or not schema_version.strip():
        errors.append("schema_version must be a non-empty string")

    generated_at = profile.get("generated_at")
    if not isinstance(generated_at, str) or not generated_at.strip():
        errors.append("generated_at must be a non-empty ISO-8601 string")
    elif not re.match(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})$", generated_at):
        errors.append("generated_at is not an ISO-8601 timestamp with timezone: {}".format(generated_at))

    for list_field in ("known_risks", "unresolved_items", "evidence_sources"):
        if list_field in profile and not isinstance(profile[list_field], list):
            errors.append("{} must be an array".format(list_field))

    toolchains = profile.get("toolchains")
    if isinstance(toolchains, dict) and "candidates" in toolchains:
        if not isinstance(toolchains["candidates"], list):
            errors.append("toolchains.candidates must be an array")

    scope_definitions = profile.get("scope_definitions")
    if not isinstance(scope_definitions, dict):
        errors.append("scope_definitions must be an object")
    else:
        missing_scopes = sorted(ALLOWED_SCOPES.difference(scope_definitions.keys()))
        if missing_scopes:
            errors.append("scope_definitions is missing: {}".format(", ".join(missing_scopes)))

    critical_paths = [path for path, _scope in CRITICAL_RUNTIME_REQUIREMENTS]
    if len(CRITICAL_RUNTIME_REQUIREMENTS) != 22:
        errors.append("critical runtime gate must contain exactly 22 requirements")
    if len(set(critical_paths)) != 22:
        errors.append("critical runtime gate contains duplicate paths")


def walk(node, path):
    if isinstance(node, dict):
        # Any fragment of the parameter contract makes this a parameter
        # candidate.  That catches accidentally omitted status/source/risk
        # fields instead of silently treating a partial object as a container.
        is_parameter = any(field in node for field in PARAMETER_MARKER_FIELDS)
        if is_parameter:
            missing = sorted(PARAMETER_FIELDS.difference(node.keys()))
            if missing:
                errors.append("{} is a partial parameter object; missing: {}".format(
                    path, ", ".join(missing)))
                return

            status = node.get("status")
            if status not in ALLOWED_STATUSES:
                errors.append("{}.status has unsupported value: {}".format(
                    path, display_value(status)))

            scope = node.get("scope")
            if scope is not None and scope not in ALLOWED_SCOPES:
                errors.append("{}.scope has unsupported value: {}".format(
                    path, display_value(scope)))
            if status == "confirmed-runtime" and scope is None:
                errors.append("{}.scope is required when status is confirmed-runtime".format(path))

            name = node.get("name")
            if not isinstance(name, str) or not name.strip():
                errors.append("{}.name must be a non-empty string".format(path))

            for field in ("source", "risk", "verification_command"):
                value = node.get(field)
                if not isinstance(value, str) or not value.strip():
                    errors.append("{}.{} must be a non-empty string".format(path, field))

            value = node.get("value")
            empty_runtime_observation = (
                path in EMPTY_CONFIRMED_RUNTIME_PATHS and
                status == "confirmed-runtime" and
                value == ""
            )
            if status == "unknown" and value is not None:
                errors.append("{}.value must be null when status is unknown".format(path))
            if status != "unknown" and (value is None or value == "") and not empty_runtime_observation:
                errors.append("{}.value cannot be empty when status is {}".format(path, status))

            parameters[path] = node
            return

        for key, value in node.items():
            child_path = "{}.{}".format(path, key) if path else key
            walk(value, child_path)
    elif isinstance(node, list):
        for index, value in enumerate(node):
            walk(value, "{}[{}]".format(path, index))


if isinstance(profile, dict):
    # Metadata fields are intentionally excluded.  Every recognized parameter
    # below the platform sections is checked recursively.
    for section in (
        "board",
        "operating_system",
        "toolchains",
        "display",
        "input",
        "lvgl",
        "fonts",
        "networking",
        "deployment",
        "known_risks",
        "unresolved_items",
        "evidence_sources",
    ):
        if section in profile:
            walk(profile[section], section)

if not parameters:
    errors.append("no parameter objects were found")


def require_runtime_value(path, expected_scope):
    parameter = parameters.get(path)
    if parameter is None:
        return "missing parameter"
    if parameter.get("status") != "confirmed-runtime":
        return "status={} value={}".format(
            parameter.get("status", "missing"), display_value(parameter.get("value")))
    if parameter.get("scope") != expected_scope:
        return "scope={} expected={}".format(
            parameter.get("scope", "missing"), expected_scope)
    value = parameter.get("value")
    if value == "" and path in EMPTY_CONFIRMED_RUNTIME_PATHS:
        return None
    if value is None or value == "":
        return "confirmed-runtime value is empty"
    return None


critical_failures = []
for critical_path, expected_scope in CRITICAL_RUNTIME_REQUIREMENTS:
    reason = require_runtime_value(critical_path, expected_scope)
    if reason:
        critical_failures.append((critical_path, reason))


def runtime_value(path):
    parameter = parameters.get(path)
    expected_scope = None
    if path.startswith("toolchains."):
        expected_scope = "toolchain"
    elif path.startswith("display.") or path.startswith("input."):
        expected_scope = "board"
    if (parameter and parameter.get("status") == "confirmed-runtime" and
            (expected_scope is None or parameter.get("scope") == expected_scope)):
        return parameter.get("value")
    return None


def positive_integer(path):
    value = runtime_value(path)
    if value is not None and (isinstance(value, bool) or not isinstance(value, int) or value <= 0):
        errors.append("{} must be a positive integer when confirmed-runtime".format(path))


for numeric_path in (
    "display.runtime_width",
    "display.runtime_height",
    "display.runtime_bits_per_pixel",
    "display.runtime_line_length",
):
    positive_integer(numeric_path)

for boolean_path in (
    "input.runtime_swap_xy",
    "input.runtime_invert_x",
    "input.runtime_invert_y",
):
    value = runtime_value(boolean_path)
    if value is not None and not isinstance(value, bool):
        errors.append("{} must be true or false when confirmed-runtime".format(boolean_path))

for device_path in (
    "display.runtime_device",
    "input.runtime_device",
    "toolchains.selected_compiler",
    "toolchains.selected_dumpmachine",
    "toolchains.selected_default_architecture",
    "toolchains.selected_abi",
):
    value = runtime_value(device_path)
    if value is not None and (not isinstance(value, str) or not value.strip()):
        errors.append("{} must be a non-empty string when confirmed-runtime".format(device_path))

rotation = runtime_value("display.runtime_rotation")
if rotation is not None:
    if isinstance(rotation, str) and rotation.isdigit():
        rotation = int(rotation)
    if rotation not in (0, 90, 180, 270):
        errors.append("display.runtime_rotation must be one of 0, 90, 180, or 270")

float_abi = runtime_value("toolchains.selected_float_abi")
if float_abi is not None and float_abi not in ("soft", "softfp", "hard"):
    errors.append("toolchains.selected_float_abi must be soft, softfp, or hard")

for low_path, high_path in (
    ("input.runtime_x_min", "input.runtime_x_max"),
    ("input.runtime_y_min", "input.runtime_y_max"),
):
    low = runtime_value(low_path)
    high = runtime_value(high_path)
    if low is not None and high is not None:
        if (isinstance(low, bool) or isinstance(high, bool) or
                not isinstance(low, (int, float)) or not isinstance(high, (int, float)) or
                low >= high):
            errors.append("{} must be numeric and less than {}".format(low_path, high_path))

if errors:
    print("PROFILE INVALID: {} structural/value error(s).".format(len(errors)), file=sys.stderr)
    for error in errors:
        print("  - {}".format(error), file=sys.stderr)
    sys.exit(3)

status_counts = {status: 0 for status in sorted(ALLOWED_STATUSES)}
for parameter in parameters.values():
    status_counts[parameter["status"]] += 1

print("Profile structure is valid: {}".format(os.path.abspath(profile_path)))
print("Parameter status counts (all scopes): " + ", ".join(
    "{}={}".format(status, status_counts[status]) for status in sorted(status_counts)))

runtime_scope_counts = {scope: 0 for scope in sorted(ALLOWED_SCOPES)}
for parameter in parameters.values():
    if parameter["status"] == "confirmed-runtime":
        runtime_scope_counts[parameter["scope"]] += 1
print("Confirmed-runtime counts by scope: " + ", ".join(
    "{}={}".format(scope, runtime_scope_counts[scope])
    for scope in sorted(runtime_scope_counts)))
print("Platform confirmed-runtime count: {} (board + toolchain); host-backend is non-platform evidence.".format(
    runtime_scope_counts["board"] + runtime_scope_counts["toolchain"]))

if critical_failures:
    heading = "WARNING" if allow_unverified else "CRITICAL"
    stream = sys.stderr
    print("{}: {} platform-critical field(s) are not confirmed-runtime:".format(
        heading, len(critical_failures)), file=stream)
    for path, reason in critical_failures:
        print("  - {}: {}".format(path, reason), file=stream)

    if allow_unverified:
        print("WARNING: override accepted for scaffold generation only.", file=stream)
        print("WARNING: source defaults and UNKNOWN sentinels must not be treated as board facts.", file=stream)
        print("Profile validation completed with explicit unverified-default warnings.")
        sys.exit(0)

    print("Refusing to validate for project generation.", file=stream)
    print("Run the toolchain/board probes, update the profile with confirmed-runtime values,", file=stream)
    print("then rerun this command.  For scaffold inspection only, pass", file=stream)
    print("--allow-unverified-defaults explicitly.", file=stream)
    sys.exit(4)

print("All platform-critical fields are confirmed-runtime.")
print("Profile validation passed.")
PY
