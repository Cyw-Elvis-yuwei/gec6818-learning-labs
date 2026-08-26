#!/bin/sh

# VMware 共享目录不保证完整兼容 SQLite 的文件锁语义。这个运行器只把测试运行时
# 产生的 build/test/*.db 放入 Ubuntu 本地临时目录；被测二进制仍来自项目构建目录。
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <absolute-test-binary>" >&2
    exit 2
fi

test_binary=$1
if [ ! -x "$test_binary" ]; then
    echo "test binary is not executable: $test_binary" >&2
    exit 2
fi

# 使用项目专用覆盖变量，避免通用 TMPDIR 恰好也指向 /mnt/hgfs。
tmp_base=${CLINIC_TEST_TMPDIR:-/tmp}
runtime_dir=$(mktemp -d "$tmp_base/clinic-test.XXXXXX")

cleanup()
{
    case "$runtime_dir" in
        "$tmp_base"/clinic-test.*)
            rm -rf -- "$runtime_dir"
            ;;
    esac
}

trap cleanup EXIT HUP INT TERM
mkdir -p "$runtime_dir/build/test"
cd "$runtime_dir"
"$test_binary"
