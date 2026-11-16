#!/usr/bin/env python3
"""产物独立校验：不复用 pack-boot.py 的任何代码，重新解析 boot image / IKCONFIG / DTB。

"编成功 ≠ 配置生效"：这里断言的是镜像里实际存在的事实，而不是构建脚本说了什么。
每次运行先做一次 self-test，证明断言逻辑本身能把 =n 判成 FAIL（防止校验形同虚设）。
"""
import argparse
import gzip
import hashlib
import struct
import subprocess
import sys
import zlib


class Checker:
    def __init__(self):
        self.ok = 0
        self.fails = []

    def expect(self, cond, msg):
        if cond:
            self.ok += 1
            print("  OK   " + msg)
        else:
            self.fails.append(msg)
            print("  FAIL " + msg)

    def finish(self):
        print("\n== 校验汇总: %d 项通过, %d 项失败 ==" % (self.ok, len(self.fails)))
        if self.fails:
            for f in self.fails:
                print("::error::" + f)
            sys.exit(1)


def cfg_value(lines, key):
    """返回 'y' / 'n' / 字符串值 / None(该选项在本内核不存在)。"""
    for line in lines:
        if line == "# %s is not set" % key:
            return "n"
        if line.startswith(key + "="):
            return line[len(key) + 1:]
    return None


def assert_config(lines, key, want):
    got = cfg_value(lines, key)
    if want == "y":
        assert got == "y", "%s 期望 =y，实际 %r" % (key, got)
    elif want == "n":
        assert got in ("n", None), "%s 期望 =n/不存在，实际 %r" % (key, got)
    else:
        assert got == want, "%s 期望 %r，实际 %r" % (key, want, got)


def self_test():
    """校验逻辑必须能失败；否则所有 OK 都没有意义。"""
    fake = ["# CONFIG_KSU is not set", "CONFIG_IKCONFIG=y"]
    try:
        assert_config(fake, "CONFIG_KSU", "y")
    except AssertionError:
        pass
    else:
        raise RuntimeError("self-test 失败: 把 '# CONFIG_KSU is not set' 判成了 y")
    try:
        assert_config(fake, "CONFIG_IKCONFIG", "n")
    except AssertionError:
        pass
    else:
        raise RuntimeError("self-test 失败: 把 'CONFIG_IKCONFIG=y' 判成了 n")
    print("  OK   self-test: 断言逻辑能把 =n 与 =y 判成 FAIL")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--img", required=True)
    ap.add_argument("--image-gz", required=True)
    ap.add_argument("--config", required=True)
    ap.add_argument("--dtb", required=True)
    ap.add_argument("--variant", required=True, choices=["clean", "ksu"])
    ap.add_argument("--kver", required=True)
    ap.add_argument("--vertag", required=True)
    a = ap.parse_args()

    c = Checker()
    print("== 0. 校验逻辑自检 ==")
    self_test()

    print("\n== 1. boot image header ==")
    d = open(a.img, "rb").read()
    c.expect(d[:8] == b"ANDROID!", "ANDROID! 位于偏移 0")
    ks, ka, rs, ra, ss, sa, ta, page, hv, osv = struct.unpack_from("<10I", d, 8)
    c.expect(page == 2048, "page_size = 2048")
    c.expect(hv == 1, "header_version = 1")
    c.expect(osv == 0x12000000, "os_version = 0x12000000")
    c.expect(ka == 0x00080000, "kernel_addr = 0x00080000")
    c.expect(ta == 0x07A00000, "tags_addr = 0x07A00000")
    c.expect(rs == 0 and ss == 0, "ramdisk/second size = 0（与厂商 release 一致）")
    payload = len(d) - page
    c.expect(0 < ks <= payload and payload - ks < 2048,
             "kernel_size=%d 与实际负载 %d 匹配" % (ks, payload))
    cmdline = d[64:576].split(b"\x00")[0].decode(errors="replace")
    c.expect("console=ttyAMA0" in cmdline, "cmdline 带主线串口 console")

    print("\n== 2. 解压内核并读回 linux_banner ==")
    raw = zlib.decompressobj(16 + zlib.MAX_WBITS).decompress(d[page:page + ks])
    c.expect(len(raw) > 5_000_000, "Image 解压后大小 %d 字节" % len(raw))
    i = raw.find(b"Linux version ")
    c.expect(i >= 0, "Image 里存在 linux_banner")
    banner = raw[i:raw.find(b"\x00", i)].decode(errors="replace") if i >= 0 else ""
    print("     banner: " + banner[:160])
    c.expect(("Linux version " + a.kver) in banner, "linux_banner 版本为 %s" % a.kver)
    c.expect(a.vertag in banner, "linux_banner 含版本标注 %s" % a.vertag)

    print("\n== 3. IKCONFIG（镜像里的 /proc/config.gz）==")
    st, ed = raw.find(b"IKCFG_ST"), raw.find(b"IKCFG_ED")
    c.expect(st >= 0 and ed > st, "IKCFG_ST / IKCFG_ED 标记存在")
    if st < 0 or ed <= st:
        c.finish()
    cfg = gzip.decompress(raw[st + 8:ed]).decode(errors="replace")
    lines = cfg.splitlines()
    c.expect(len(lines) > 2000, "IKCONFIG 共 %d 行" % len(lines))
    for key, want in (
        ("CONFIG_IKCONFIG", "y"),
        ("CONFIG_IKCONFIG_PROC", "y"),
        ("CONFIG_ARCH_HISI", "y"),
        ("CONFIG_SERIAL_AMBA_PL011", "y"),
        ("CONFIG_LOCALVERSION_AUTO", "n"),
        ("CONFIG_ARM64", "y"),
    ):
        try:
            assert_config(lines, key, want)
            c.expect(True, "%s=%s" % (key, want))
        except AssertionError as e:
            c.expect(False, str(e))
    lv = cfg_value(lines, "CONFIG_LOCALVERSION")
    c.expect(lv == '"%s"' % a.vertag,
             "CONFIG_LOCALVERSION=%s" % lv)

    print("\n== 4. KernelSU 档位（配置开关 + 代码是否真的链进 Image）==")
    ksu_hits = raw.count(b"KernelSU") + raw.count(b"ksu_")
    if a.variant == "ksu":
        try:
            assert_config(lines, "CONFIG_KSU", "y")
            c.expect(True, "CONFIG_KSU=y")
        except AssertionError as e:
            c.expect(False, str(e))
        c.expect(ksu_hits > 50, "Image 里 KSU 字符串命中 %d 次（>50）" % ksu_hits)
        c.expect(b"KernelSU: Initialized" in raw or b"ksu_" in raw,
                 "KSU 代码确实链入 Image")
    else:
        try:
            assert_config(lines, "CONFIG_KSU", "n")
            c.expect(True, "CONFIG_KSU=n / 不存在")
        except AssertionError as e:
            c.expect(False, str(e))
        c.expect(ksu_hits == 0, "纯净档 Image 里 KSU 字符串命中 0 次")

    print("\n== 5. 构建 .config 与 IKCONFIG 交叉核对 ==")
    out_lines = open(a.config).read().splitlines()
    for key in ("CONFIG_IKCONFIG", "CONFIG_ARCH_HISI", "CONFIG_LOCALVERSION",
                "CONFIG_LOCALVERSION_AUTO", "CONFIG_KSU"):
        c.expect(cfg_value(out_lines, key) == cfg_value(lines, key),
                 "%s 两份配置一致 (%r)" % (key, cfg_value(lines, key)))

    print("\n== 6. 产物一致性 ==")
    gz = open(a.image_gz, "rb").read()
    c.expect(hashlib.sha256(gz).hexdigest() ==
             hashlib.sha256(d[page:page + ks]).hexdigest(),
             "boot.img 里的 kernel 与 Image.gz 完全一致")

    print("\n== 7. 设备树（用外部 dtc 反编译，不用生成流程的代码）==")
    dtb = open(a.dtb, "rb").read()
    c.expect(dtb[:4] == b"\xd0\x0d\xfe\xed", "DTB 有 FDT magic")
    try:
        r = subprocess.run(["dtc", "-I", "dtb", "-O", "dts", a.dtb],
                           capture_output=True, text=True)
        src, rc, err = r.stdout, r.returncode, r.stderr.strip()[:120]
    except (FileNotFoundError, OSError) as exc:
        src, rc, err = "", -1, str(exc)
    c.expect(rc == 0, "dtc 能反编译 DTB" + (": " + err if rc else ""))
    for needle in ("hisilicon,kirin710", "arm,gic-400", "arm,pl011",
                   "arm,armv8-timer", "arm,psci", "device_type = \"memory\""):
        c.expect(needle in src, "DTB 含 %s" % needle)

    c.finish()


if __name__ == "__main__":
    main()
