#!/usr/bin/env python3
"""离线解析厂商 Android boot image v1（不执行镜像里的任何代码）。

用法: python3 extract_vendor_boot.py <vendor-kernel.img> [output-dir]

复用本脚本可以复核 docs/REVERSE_ENGINEERING.md 里的全部字节级事实：
header 字段、cmdline、gzip 内核、IKCONFIG、linux_banner、FDT magic、KSU 字符串计数。
"""
import gzip
import os
import struct
import sys
import zlib


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "vendor-extract"
    os.makedirs(out, exist_ok=True)
    data = open(src, "rb").read()
    if data[:8] != b"ANDROID!":
        print("!! 不是 ANDROID! 开头的 boot image")
        sys.exit(1)

    (kernel_size, kernel_addr, ramdisk_size, ramdisk_addr,
     second_size, second_addr, tags_addr, page_size,
     header_version, os_version) = struct.unpack_from("<10I", data, 8)
    cmdline = data[64:576].split(b"\x00")[0].decode(errors="replace")
    print("magic=%s page=%d header_version=%d os_version=0x%x" %
          (data[:8].decode(), page_size, header_version, os_version))
    print("kernel_size=%d kernel_addr=0x%x ramdisk_size=%d second_size=%d tags_addr=0x%x" %
          (kernel_size, kernel_addr, ramdisk_size, second_size, tags_addr))
    print("cmdline=%s" % cmdline)
    print("header_size@1612=%d  id@1616=%s" %
          (struct.unpack_from("<I", data, 1612)[0], data[1616:1648].hex()))

    kernel = data[page_size:page_size + kernel_size]
    if kernel[:2] != b"\x1f\x8b":
        print("!! kernel 不是 gzip，跳过解压")
        return
    raw = zlib.decompressobj(16 + zlib.MAX_WBITS).decompress(kernel)
    print("decompressed Image=%d bytes" % len(raw))
    open(os.path.join(out, "Image"), "wb").write(raw)

    a, b = raw.find(b"IKCFG_ST"), raw.find(b"IKCFG_ED")
    if a >= 0 and b > a:
        cfg = gzip.decompress(raw[a + 8:b]).decode(errors="replace")
        open(os.path.join(out, "ikconfig"), "w", newline="\n").write(cfg)
        print("IKCONFIG=%d bytes" % len(cfg))
        for key in ("CONFIG_LOCALVERSION", "CONFIG_KSU", "CONFIG_KPROBES",
                    "CONFIG_SECURITY_SELINUX_DEVELOP", "CONFIG_ARCH_HISI",
                    "CONFIG_F2FS_FS", "CONFIG_EROFS_FS"):
            hit = next((l for l in cfg.splitlines()
                        if l.startswith(key + "=") or l.startswith("# " + key + " ")), None)
            print("   ", hit)

    i = raw.find(b"Linux version ")
    if i >= 0:
        banner = raw[i:raw.find(b"\x00", i)].decode(errors="replace")
        print("linux_banner=%s" % banner)
        open(os.path.join(out, "banner.txt"), "w").write(banner + "\n")

    magic = b"\xd0\x0d\xfe\xed"
    offs, pos = [], 0
    while True:
        pos = raw.find(magic, pos)
        if pos < 0:
            break
        offs.append(pos)
        pos += 4
    print("FDT magic offsets=%s count=%d" % (offs[:10], len(offs)))
    for idx, off in enumerate(offs[:4]):
        if off + 8 > len(raw):
            continue
        total = struct.unpack_from(">I", raw, off + 4)[0]
        if 40 <= total <= len(raw) - off:
            open(os.path.join(out, "dtb_%d.dtb" % idx), "wb").write(raw[off:off + total])
            print("   wrote dtb_%d.dtb (%d bytes)" % (idx, total))

    for pat in (b"KernelSU", b"kernelsu", b"ksu_", b"kirin710"):
        print("   %-12s x%d" % (pat.decode(), raw.count(pat)))


if __name__ == "__main__":
    main()
