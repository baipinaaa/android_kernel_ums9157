#!/usr/bin/env python3
"""Build a flashable boot.img from a freshly built Image plus a boot ramdisk.

The Android boot partition on this device carries two things besides the
kernel and the ramdisk:

  * an AndroidVerifiedBoot (AVB) vbmeta blob right after the ramdisk, and
  * a 64-byte AVB footer in the last 64 bytes of the partition.

The device is unlocked (``verifiedbootstate=orange``, ``veritymode=disabled``),
so neither the vbmeta hash nor its signature is checked -- but ``init``'s
``avb_slot_verify()`` still insists on the *structure* being present, and
returns ``AVB_SLOT_VERIFY_RESULT_ERROR_VERIFICATION`` (6) when it is not,
which aborts the first stage and panics with "Attempted to kill init!".

So we copy the vbmeta blob out of a known-good (stock) boot image and append a
matching footer.  Nothing is re-hashed and nothing is re-signed.

Usage:

  python3 tools/mkboot.py \
      --image arch/arm64/boot/Image \
      --ramdisk vendor/ramdisk_magisk.lz4 \
      --avb-from vendor/boot_a.bin \
      --out boot_magisk.img

``--avb-from`` may be any image that carries the AVB footer this device
accepts: the stock ``boot_a.bin``, a Magisk-patched boot image, or a boot.img
produced by this script earlier.
"""

import argparse
import struct
import sys

PAGE = 4096
DEFAULT_PARTITION_SIZE = 67108864  # boot_a is 64 MiB on this device


def align(off, page=PAGE):
    return (off + page - 1) // page * page


def read_avb(src):
    """Return (vbmeta_blob, original_image_size) taken from *src*."""
    data = open(src, "rb").read()
    if len(data) < 64:
        raise SystemExit("%s: too small to hold an AVB footer" % src)

    footer = data[-64:]
    if footer[:4] != b"AVBf":
        raise SystemExit(
            "%s: no AVB footer at the end (last 4 bytes are %r). "
            "Pass a stock or previously packaged boot image."
            % (src, bytes(footer[:4]))
        )
    major, minor = struct.unpack_from(">II", footer, 4)
    orig_size, vbmeta_off, vbmeta_size = struct.unpack_from(">QQQ", footer, 12)
    if major != 1 or minor != 0:
        raise SystemExit("%s: unsupported AVB footer version %d.%d" % (src, major, minor))
    if vbmeta_off + vbmeta_size > len(data):
        raise SystemExit("%s: footer points outside the file" % src)

    blob = data[vbmeta_off:vbmeta_off + vbmeta_size]
    if blob[:4] != b"AVB0":
        raise SystemExit(
            "%s: vbmeta at 0x%x does not start with AVB0 (got %r)"
            % (src, vbmeta_off, bytes(blob[:4]))
        )
    return blob, orig_size


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--image", required=True, help="arch/arm64/boot/Image")
    ap.add_argument("--ramdisk", required=True,
                    help="lz4-compressed boot ramdisk (stock or Magisk-patched)")
    ap.add_argument("--avb-from", required=True, dest="avb_from",
                    help="boot image to copy the AVB vbmeta blob from")
    ap.add_argument("--out", required=True, help="output boot.img")
    ap.add_argument("--size", type=lambda s: int(s, 0), default=DEFAULT_PARTITION_SIZE,
                    help="partition size in bytes (default: 0x4000000 = 64 MiB)")
    ap.add_argument("--os-version", type=lambda s: int(s, 0), default=0x1a000178,
                    help="os_version field (default 0x1a000178, same as stock)")
    ap.add_argument("--cmdline", default="",
                    help="extra kernel cmdline (stock boot has none)")
    args = ap.parse_args()

    image = open(args.image, "rb").read()
    ramdisk = open(args.ramdisk, "rb").read()
    vbmeta, _ = read_avb(args.avb_from)

    if len(args.cmdline.encode()) > 1535:
        raise SystemExit("cmdline too long (max 1535 bytes + NUL)")

    hdr = bytearray(PAGE)
    hdr[0:8] = b"ANDROID!"
    struct.pack_into("<I", hdr, 0x08, len(image))
    struct.pack_into("<I", hdr, 0x0c, len(ramdisk))
    struct.pack_into("<I", hdr, 0x10, args.os_version)
    struct.pack_into("<I", hdr, 0x14, 1584)   # header_size
    struct.pack_into("<I", hdr, 0x1c, 0)      # page_size deprecated in v4
    struct.pack_into("<I", hdr, 0x28, 4)      # header_version
    hdr[0x2c:0x2c + len(args.cmdline)] = args.cmdline.encode()

    body = bytearray(hdr)
    body += image
    body += b"\0" * (align(len(body)) - len(body))
    body += ramdisk
    body += b"\0" * (align(len(body)) - len(body))
    image_end = len(body)

    body += vbmeta
    if len(body) + 64 > args.size:
        raise SystemExit(
            "image does not fit: %d bytes needed, partition is %d"
            % (len(body) + 64, args.size)
        )
    body += b"\0" * (args.size - 64 - len(body))

    footer = bytearray(64)
    footer[0:4] = b"AVBf"
    struct.pack_into(">I", footer, 4, 1)              # major
    struct.pack_into(">I", footer, 8, 0)              # minor
    struct.pack_into(">Q", footer, 12, image_end)     # original_image_size
    struct.pack_into(">Q", footer, 20, image_end)     # vbmeta_offset
    struct.pack_into(">Q", footer, 28, len(vbmeta))   # vbmeta_size
    body += footer

    assert len(body) == args.size
    open(args.out, "wb").write(bytes(body))

    print("kernel_size   %d" % len(image))
    print("ramdisk_size  %d" % len(ramdisk))
    print("image_end     0x%x (%d)" % (image_end, image_end))
    print("vbmeta        %d bytes @0x%x" % (len(vbmeta), image_end))
    print("partition     %d" % args.size)
    print("wrote         %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
