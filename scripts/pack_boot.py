#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""pack_boot.py -- 把编译出的裸 Image 打成设备可直接刷的 boot.img（header v4）。

为什么需要
---------
Android 13 (header v4) 的 boot 分区不是"Image 直接写进去"，而是一段带
`ANDROID!` 头的复合镜像：header page + kernel + ramdisk(+ vendor_ramdisk + boot_signature)。
内核编出来只有 Image（约 29 MB），必须按原厂 boot 的 header 参数重新拼一遍才能刷。

模板从哪来
---------
`scripts/boot_assets/boot_header_v4.bin` = 设备原厂 boot_a 的前 4096 字节，
`scripts/boot_assets/ramdisk.cpio.lz4` = 原厂 ramdisk（1,471,146 B，lz4_legacy；
从 boot_a.bin 的 offset 0x248a000 提取，解出 13 个 cpio 条目）。
注意不要用 Magisk patched boot 里那份 1,167,209 B 的 ramdisk（解出 21 个条目，
含 .backup/.magisk、overlay.d、.backup/init.xz）——它需要 Magisk 的整套运行环境。
路径是相对本脚本所在目录，CI 与本地都能跑。

用法
----
    python3 scripts/pack_boot.py --kernel arch/arm64/boot/Image --out boot.img
    python3 scripts/pack_boot.py --kernel Image --out boot.img --pad-to 67108864

    # 校验一个已打好的 boot.img（把 kernel 段切出来算 md5）
    python3 scripts/pack_boot.py --verify boot.img

设计取舍
--------
默认写 AVB（`--no-avb` 可关）。设备的 bootloader 会去
`/proc/device-tree/firmware/android/vbmeta/parts` 里列出的分区找 `AvbFooter`，
找不到就让 `avb_slot_verify()` 返回 6，init 随即
`Failed to mount required partitions early` 并 panic——这就是早期一版
“能开机但一进系统就重启”的直接原因。

AVB0 blob 用 `scripts/boot_assets/boot_vbmeta.bin`（取自原厂 boot_a.bin
的 offset 39,788,544，2304 B）。设备是 unlocked（verifiedbootstate=orange、
veritymode=disabled），hash 与签名都不校验（Magisk 补丁包连 image_size
都没改也能启动），所以不需要私钥重签，只要结构存在、能被解析。
换到锁定设备时，必须另外刷一个把 boot/vendor_boot 从 chain 里摘掉、
或 `flags=1`（HASHTREE_DISABLED）的 vbmeta。
"""
import argparse
import hashlib
import os
import struct
import sys

PAGE = 4096
HERE = os.path.dirname(os.path.abspath(__file__))
TPL = os.path.join(HERE, "boot_assets", "boot_header_v4.bin")
RAMDISK = os.path.join(HERE, "boot_assets", "ramdisk.cpio.lz4")
VBMETA = os.path.join(HERE, "boot_assets", "boot_vbmeta.bin")
PART_SIZE = 67108864  # boot_a 分区实际大小（64 MiB）

# v3/v4 header 里我们关心的字段偏移
OFF_KERNEL_SIZE = 0x08
OFF_RAMDISK_SIZE = 0x0c
OFF_HEADER_VERSION = 0x28


def align(n, p=PAGE):
    return (n + p - 1) // p * p


def add_avb(buf, vbmeta=VBMETA, partition_size=PART_SIZE):
    """在镜像末尾追加 AVB0 + AvbFooter。

    布局： [boot image，按 4 KiB 对齐][vbmeta blob][0 填充][AvbFooter 64 B]

    为什么这样做：设备 bootloader 会去 /proc/device-tree/firmware/android/vbmeta/parts
    列出的分区上找 AvbFooter，找不到就 avb_slot_verify 返回 6
    (AVB_SLOT_VERIFY_RESULT_ERROR_VERIFICATION)，init 随即
    "Failed to mount required partitions early" 并 panic。
    设备是 unlocked（verifiedbootstate=orange、veritymode=disabled），
    hash 与签名都不校验，所以这里直接复用原厂那份 2,304 B vbmeta blob
    （scripts/boot_assets/boot_vbmeta.bin，取自 boot_a.bin 的 offset 39,788,544），
    只需把 footer 的 original_image_size / vbmeta_offset 指向我们自己的 image_end。
    原厂 Magisk 补丁包用的也是这个办法（它连 image_size 都没改）。
    """
    end = align(len(buf))
    out = bytearray(buf) + b"\x00" * (end - len(buf))
    if vbmeta:
        vb = open(vbmeta, "rb").read()
        if vb[:4] != b"AVB0":
            sys.exit("%s 不是 AVB0 结构" % vbmeta)
    else:
        vb = b""
    out += vb
    if partition_size:
        pad = partition_size - 64 - len(out)
        if pad < 0:
            sys.exit("镜像 %d B + vbmeta %d B 超出分区 %d B"
                     % (end, len(vb), partition_size))
        out += b"\x00" * pad
    # AvbFooter = magic(4) + version(8) + original_image_size(8) + vbmeta_offset(8)
    #              + vbmeta_size(8) + reserved(28) = 64
    out += (b"AVBf" + struct.pack(">II", 1, 0) + struct.pack(">QQQ", end, end, len(vb))
            + b"\x00" * 28)
    return end, len(vb), bytes(out)


def pack(kernel, out, ramdisk=RAMDISK, tpl=TPL, pad_to=0, avb=True,
         vbmeta=VBMETA, partition_size=PART_SIZE):
    for p in (kernel, ramdisk, tpl):
        if not os.path.exists(p):
            sys.exit("missing: %s" % p)

    hdr = bytearray(open(tpl, "rb").read())
    if len(hdr) != PAGE or hdr[:8] != b"ANDROID!":
        sys.exit("%s 不是 header 模板（需要 4096 B 且以 ANDROID! 开头）" % tpl)
    ver = struct.unpack_from("<I", hdr, OFF_HEADER_VERSION)[0]
    if ver != 4:
        sys.exit("模板 header_version=%d，本脚本只支持 4" % ver)

    k = open(kernel, "rb").read()
    r = open(ramdisk, "rb").read()
    struct.pack_into("<I", hdr, OFF_KERNEL_SIZE, len(k))
    struct.pack_into("<I", hdr, OFF_RAMDISK_SIZE, len(r))

    buf = bytearray(hdr)
    buf += k
    buf += b"\x00" * (align(len(k)) - len(k))
    buf += r
    buf += b"\x00" * (align(len(r)) - len(r))
    if avb:
        img_end, vlen, buf = add_avb(bytes(buf), vbmeta, partition_size)
    else:
        img_end, vlen = len(buf), 0
        if pad_to and len(buf) < pad_to:
            buf = bytes(buf) + b"\x00" * (pad_to - len(buf))

    with open(out, "wb") as f:
        f.write(buf)

    print("kernel   : %s (%d B)" % (kernel, len(k)))
    print("ramdisk  : %s (%d B)" % (ramdisk, len(r)))
    print("header   : v%d, os_version=%s, page=%d"
          % (ver, hex(struct.unpack_from("<I", hdr, 0x10)[0]), PAGE))
    if avb and vlen:
        print("avb      : vbmeta %d B @ %d, footer @ %d" % (vlen, img_end, len(buf) - 64))
    print("out      : %s (%d B, md5 %s)" % (out, len(buf), md5(buf)))
    return out


def md5(b):
    return hashlib.md5(b).hexdigest()


def verify(boot):
    d = open(boot, "rb").read()
    if d[:8] != b"ANDROID!":
        sys.exit("不是 boot.img（magic 不对）")
    ksize = struct.unpack_from("<I", d, OFF_KERNEL_SIZE)[0]
    rsize = struct.unpack_from("<I", d, OFF_RAMDISK_SIZE)[0]
    ver = struct.unpack_from("<I", d, OFF_HEADER_VERSION)[0]
    k_off = PAGE
    r_off = k_off + align(ksize)
    print("header_version %d  kernel_size %d  ramdisk_size %d" % (ver, ksize, rsize))
    print("kernel  @%d  md5 %s" % (k_off, md5(d[k_off:k_off + ksize])))
    print("ramdisk @%d  first4 %s" % (r_off, d[r_off:r_off + 4].hex()))
    left = len(d) - (r_off + align(rsize))
    print("tail %d B (0 表示干净紧凑镜像)" % left)
    if left > 0:
        print("tail 非零字节 %d" % sum(1 for b in d[r_off + align(rsize):] if b))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", help="arch/arm64/boot/Image")
    ap.add_argument("--out", default="boot.img")
    ap.add_argument("--ramdisk", default=RAMDISK)
    ap.add_argument("--template", default=TPL)
    ap.add_argument("--pad-to", type=int, default=0,
                    help="补齐到该字节数（非 AVB 模式用）")
    ap.add_argument("--no-avb", action="store_true",
                    help="不写 AVB0/AvbFooter（默认写，设备 bootloader 需要它）")
    ap.add_argument("--vbmeta", default=VBMETA, help="AVB0 blob 模板（2304 B）")
    ap.add_argument("--partition-size", type=int, default=PART_SIZE,
                    help="boot 分区大小，AVB 模式下镜像会 pad 到这个长度（默认 64 MiB）")
    ap.add_argument("--verify", help="只校验一个已打好的 boot.img")
    a = ap.parse_args()
    if a.verify:
        verify(a.verify)
        return
    if not a.kernel:
        ap.error("需要 --kernel 或 --verify")
    pack(a.kernel, a.out, a.ramdisk, a.template, a.pad_to,
         avb=not a.no_avb, vbmeta=a.vbmeta, partition_size=a.partition_size)


if __name__ == "__main__":
    main()
