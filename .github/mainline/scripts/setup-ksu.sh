#!/usr/bin/env bash
# ksu 档专用：把 KernelSU 系代码挂进非 GKI 的主线内核树。
#
# 默认用 SukiSU Ultra 的旧内核兼容提交 4a0c01e（2026-04-01「移植并修复旧内核
# 兼容性支持 #852」），因为它按 LINUX_VERSION_CODE 做了版本分支，6.6 能编。
# 官方 KernelSU main 只面向 GKI，不能直接用在这里。
#
# 可用 KSU_REPO / KSU_REF / KSU_SETUP_URL 覆盖，便于以后换版本。
set -euo pipefail

SRC="${1:?linux-src}"
cd "$SRC"

KSU_REPO="${KSU_REPO:-https://github.com/SukiSU-Ultra/SukiSU-Ultra.git}"
KSU_REF="${KSU_REF:-4a0c01e}"
KSU_SETUP_URL="${KSU_SETUP_URL:-https://raw.githubusercontent.com/SukiSU-Ultra/SukiSU-Ultra/main/kernel/setup.sh}"

echo "== KernelSU 集成: ${KSU_REPO} @ ${KSU_REF} =="
curl -fLsS --retry 3 "$KSU_SETUP_URL" -o /tmp/ksu-setup.sh
head -1 /tmp/ksu-setup.sh | grep -q '^#!' || { echo "::error::setup.sh 下载到了非脚本内容"; exit 1; }
sh /tmp/ksu-setup.sh "$KSU_REF" || echo "setup.sh 返回非零，检查落盘结果"

# setup.sh 失败时走手工集成，保证不会静默产出一个没有 root 的 "ksu" 内核
if [ ! -f drivers/kernelsu/Kconfig ]; then
  echo "== setup.sh 未生效，改走手工集成 =="
  rm -rf KernelSU
  git clone --depth=1 "$KSU_REPO" KernelSU
  git -C KernelSU fetch --depth=1 origin "$KSU_REF" || true
  git -C KernelSU checkout -q "$KSU_REF" || true
  rm -rf KernelSU/.git
  test -f KernelSU/kernel/Kconfig
  rm -f drivers/kernelsu
  ln -s ../../KernelSU/kernel drivers/kernelsu
  grep -q 'kernelsu/' drivers/Makefile || \
    printf '\nobj-$(CONFIG_KSU) += kernelsu/\n' >> drivers/Makefile
  grep -q 'drivers/kernelsu/Kconfig' drivers/Kconfig || \
    sed -i '1i source "drivers/kernelsu/Kconfig"' drivers/Kconfig
fi

test -f drivers/kernelsu/Kconfig || { echo "::error::KernelSU Kconfig 缺失"; exit 1; }
grep -q 'CONFIG_KSU\|config KSU' drivers/kernelsu/Kconfig || { echo "::error::Kconfig 里没有 KSU"; exit 1; }
grep -n kernelsu drivers/Makefile
grep -n kernelsu drivers/Kconfig

# 嵌套 .git 会被 git 当成 submodule，写回分支时源码会丢
rm -rf KernelSU/.git
echo "KernelSU 文件数: $(find KernelSU -type f | wc -l)"

# --- 主线 6.6 兼容：flask.h / av_permissions.h ----------------------------
# 4.9 时代这两个头静态放在 security/selinux/include/；主线 6.6 改成由
# scripts/selinux/genheaders 生成到 $(obj)/security/selinux/。
# KernelSU 的 kernel_compat.h 直接 include security/selinux/include/security.h，
# 里面 #include "flask.h" 只在同目录找，而 KSU 编译时也不带 $(obj) 的 -I，
# 于是报 "fatal error: flask.h: No such file or directory"。
# 用主线自带的生成器提前把两份头放回 include/，与老布局对齐。
if [ ! -f security/selinux/include/flask.h ]; then
  echo "== 生成 SELinux flask.h / av_permissions.h（主线 6.6 兼容） =="
  test -f scripts/selinux/genheaders/genheaders.c
  # 三个 -I 与主线 scripts/selinux/genheaders/Makefile 的 HOST_EXTRACFLAGS 一致，
  # 少了 include/uapi 会让 classmap.h 里的 <linux/capability.h> 找错版本
  cc -O2 -Iinclude/uapi -Iinclude -Isecurity/selinux/include \
     -o /tmp/genheaders scripts/selinux/genheaders/genheaders.c
  /tmp/genheaders security/selinux/include/flask.h \
                  security/selinux/include/av_permissions.h
  test -s security/selinux/include/flask.h
  wc -c security/selinux/include/flask.h security/selinux/include/av_permissions.h
fi

echo "== 集成结果 =="
ls -l drivers/kernelsu
