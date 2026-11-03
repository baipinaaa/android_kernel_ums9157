#!/usr/bin/env bash
# scripts/ddk-sync.sh -- 把 DDK 外部模块的源文件刷进 ddk/<mod>/ 供树外构建用
#
# 为什么需要这一步：ddk/<mod>/Makefile 是 recon/ddkemit.py 生成的树外配方
# （obj-m += <stem>.o 与 <stem>-objs := ...），但源文件留在内核树里的原位置
# （drivers/input/touchscreen/... 等）。Kbuild 的 M=<dir> 只编译 M 目录下的 .c，
# 所以构建前必须把源与本目录头文件物化过来。仓库不复制源（避免两份代码漂移），
# 构建期复制、用完即弃。
#
# 用法：
#   scripts/ddk-sync.sh                # 同步全部 ddk/*/sources.txt 列到的模块
#   scripts/ddk-sync.sh <mod> [...]    # 只同步指定模块
# 环境变量：
#   KROOT=<内核树根>  默认为本脚本所在树的根（scripts/ 的上一级）
set -euo pipefail
KROOT="${KROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
cd "$KROOT"

mods=("$@")
if [ ${#mods[@]} -eq 0 ]; then
    mods=()
    for d in ddk/*/; do
        [ -f "${d}sources.txt" ] && mods+=("$(basename "$d")")
    done
fi

n_file=0
for m in "${mods[@]}"; do
    src_list="ddk/$m/sources.txt"
    if [ ! -f "$src_list" ]; then
        echo "skip $m（无 sources.txt）" >&2
        continue
    fi
    # 源文件所在目录（去重）：整个目录的 .c/.h 都复制，否则 `#include "本地头"` 会断
    dirs=$(sed 's|/[^/]*$||' "$src_list" | sort -u)
    for d in $dirs; do
        if [ ! -d "$d" ]; then
            echo "skip $m：源码目录 $d 不存在（Step 2 的搬运可能没覆盖到）" >&2
            continue
        fi
        for f in "$d"/*.c "$d"/*.h "$d"/*.S; do
            [ -e "$f" ] || continue
            cp -f "$f" "ddk/$m/" && n_file=$((n_file + 1))
        done
    done
    echo "synced ddk/$m"
done
echo "ddk-sync: 复制 $n_file 个文件"
