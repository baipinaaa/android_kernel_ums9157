#!/usr/bin/env bash
# scripts/ddk-build.sh -- 树外(DDK)构建一个或多个外部模块
#
# 判据（见 recon/koflags.py 与 08 文档 8.20）：190 个厂商 .ko 里 47 个是 EXTERNAL
# （.modinfo 无 intree=Y ⇔ 设备 /proc/modules 带 (O)），它们在内核 .config 里没有符号，
# 只能 `make -C <kernel> M=<dir> modules` 编。ddk/<mod>/Makefile 由 recon/ddkemit.py 生成。
#
# 用法：
#   scripts/ddk-build.sh <mod> [<mod> ...]
#   scripts/ddk-build.sh --all
# 环境变量：
#   KROOT=<内核树根>        默认本脚本上一级
#   OUT=<输出目录>          默认 $KROOT/out-ddk
#   JOBS=<并行度>           默认 nproc
#   EXTRA_SYMBOLS=<路径>    Module.symvers（CRC 标尺见 recon/crcdump.py）
set -euo pipefail
KROOT="${KROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
OUT="${OUT:-$KROOT/out-ddk}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

if [ "${1:-}" = "--all" ]; then
    set --
    for d in "$KROOT"/ddk/*/; do set -- "$@" "$(basename "$d")"; done
fi
[ $# -gt 0 ] || { echo "用法: $0 <mod> ... | --all" >&2; exit 2; }

mkdir -p "$OUT"
rc=0
for m in "$@"; do
    echo "=== ddk-build: $m"
    "$KROOT/scripts/ddk-sync.sh" "$m" >/dev/null || { rc=1; continue; }
    args=(-C "$KROOT" "M=$KROOT/ddk/$m" "MO=$OUT" "modules")
    [ -n "${EXTRA_SYMBOLS:-}" ] && args+=("KBUILD_EXTRA_SYMBOLS=$EXTRA_SYMBOLS")
    if ! make "${args[@]}" -j"$JOBS"; then
        echo "FAILED $m" >&2
        rc=1
    fi
done
echo "ddk-build: 输出在 $OUT（rc=$rc）"
exit $rc
