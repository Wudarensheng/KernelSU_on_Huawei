#!/usr/bin/env python3
"""把主线 Image.gz 打包成 JKM 可直接 fastboot flash 的 Android boot image v1。

header 布局不是猜的，来自对厂商发布镜像 JKM_EMUI9.1_KernelSU.img 的离线逆向，
字段与偏移记录在 docs/REVERSE_ENGINEERING.md。厂商 release 的 ramdisk_size=0
（ramdisk 在独立分区），本脚本保持一致。

同时输出一份 "kernel + appended dtb" 的镜像，供支持 appended-dtb 的 bootloader 使用。
"""
import argparse
import hashlib
import os
import struct

PAGE_SIZE = 2048
KERNEL_ADDR = 0x00080000
RAMDISK_ADDR = 0x07C00000
SECOND_ADDR = 0x00F00000
TAGS_ADDR = 0x07A00000
HEADER_VERSION = 1
OS_VERSION = 0x12000000  # 与厂商 release 一致
HEADER_SIZE = 1648

# 10 x u32 紧跟 magic，见 docs/REVERSE_ENGINEERING.md
FIELDS = (
    "kernel_size", "kernel_addr", "ramdisk_size", "ramdisk_addr",
    "second_size", "second_addr", "tags_addr", "page_size",
    "header_version", "os_version",
)


def build_header(kernel: bytes, cmdline: str) -> bytes:
    h = bytearray(PAGE_SIZE)
    struct.pack_into(
        "<8s10I", h, 0, b"ANDROID!",
        len(kernel), KERNEL_ADDR,
        0, RAMDISK_ADDR,          # ramdisk_size / ramdisk_addr
        0, SECOND_ADDR,           # second_size / second_addr
        TAGS_ADDR, PAGE_SIZE, HEADER_VERSION, OS_VERSION,
    )
    c = cmdline.encode()[:511]
    h[64:64 + len(c)] = c        # cmdline[512]
    # extra_cmdline[1024] 保持全 0：厂商镜像该区域是私有数据，不是启动必需字段
    struct.pack_into("<I", h, 1600, 0)          # recovery_dtbo_size
    struct.pack_into("<Q", h, 1604, 0)          # recovery_dtbo_offset
    struct.pack_into("<I", h, 1612, HEADER_SIZE)
    h[1616:1616 + 20] = hashlib.sha1(kernel).digest()  # id[32] 前 20 字节
    return bytes(h)


def pack(path: str, kernel: bytes, cmdline: str) -> None:
    padded = kernel + b"\x00" * ((-len(kernel)) % PAGE_SIZE)
    with open(path, "wb") as f:
        f.write(build_header(kernel, cmdline))
        f.write(padded)
    print("wrote %s (kernel=%d bytes, total=%d)" %
          (path, len(kernel), PAGE_SIZE + len(padded)))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--dtb", required=True)
    ap.add_argument("--cmdline", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--appended", required=True)
    a = ap.parse_args()

    kernel = open(a.kernel, "rb").read()
    dtb = open(a.dtb, "rb").read()
    assert kernel[:2] == b"\x1f\x8b", "kernel 不是 gzip 压缩的 Image.gz"
    assert dtb[:4] == b"\xd0\x0d\xfe\xed", "dtb 缺少 FDT magic"

    pack(a.output, kernel, a.cmdline)
    # 追加 dtb：主线内核需要设备树，某些 bootloader 会把 kernel 尾部的 FDT 直接传给内核
    pack(a.appended, kernel + dtb, a.cmdline)


if __name__ == "__main__":
    main()
