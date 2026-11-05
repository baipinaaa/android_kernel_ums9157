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
不做 AVB 重签名（设备没有签名私钥，AVB 由原厂 key 签）。原厂 boot 的
vbmeta blob（offset 39,481,344 起 2,304 B）签的是**原厂 kernel 的哈希**，
换内核后必然对不上，所以本脚本不写 vbmeta、也不写末尾的 AvbFooter(64 B)：
设备 `ro.boot.verifiedbootstate=orange` + `ro.boot.veritymode=disabled`
（bootloader 已解锁、dm-verity 关闭）时会跳过校验。若换到锁定设备，必须在
fastboot 里先 `--disable-verity --disable-verification flash vbmeta`。
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

# v3/v4 header 里我们关心的字段偏移
OFF_KERNEL_SIZE = 0x08
OFF_RAMDISK_SIZE = 0x0c
OFF_HEADER_VERSION = 0x28


def align(n, p=PAGE):
    return (n + p - 1) // p * p


def pack(kernel, out, ramdisk=RAMDISK, tpl=TPL, pad_to=0):
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
    if pad_to and len(buf) < pad_to:
        buf += b"\x00" * (pad_to - len(buf))

    with open(out, "wb") as f:
        f.write(buf)

    print("kernel   : %s (%d B)" % (kernel, len(k)))
    print("ramdisk  : %s (%d B)" % (ramdisk, len(r)))
    print("header   : v%d, os_version=%s, page=%d"
          % (ver, hex(struct.unpack_from("<I", hdr, 0x10)[0]), PAGE))
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
                    help="补齐到该字节数（设备 boot 分区 = 67108864）；默认不补")
    ap.add_argument("--verify", help="只校验一个已打好的 boot.img")
    a = ap.parse_args()
    if a.verify:
        verify(a.verify)
        return
    if not a.kernel:
        ap.error("需要 --kernel 或 --verify")
    pack(a.kernel, a.out, a.ramdisk, a.template, a.pad_to)


if __name__ == "__main__":
    main()
