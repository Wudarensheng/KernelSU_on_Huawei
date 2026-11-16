#!/usr/bin/env bash
# 把本仓库的移植层叠加到一份干净的 kernel.org 源码树上。
# 用法: apply-mainline.sh <linux-src> <.github/mainline> <repo-root>
set -euo pipefail

SRC="${1:?linux-src}"
MAIN="${2:?mainline dir}"
REPO="${3:?repo root}"

DTS_DIR="$SRC/arch/arm64/boot/dts/hisilicon"
test -f "$DTS_DIR/Makefile" || { echo "::error::不是主线 arm64 源码树: $SRC"; exit 1; }

echo "== 1/3 设备树 =="
cp "$MAIN/dts/hi6250-jkm.dts" "$DTS_DIR/hi6250-jkm.dts"
MK="$DTS_DIR/Makefile"
if ! grep -q 'hi6250-jkm\.dtb' "$MK"; then
  printf '\n# JKM / Kirin 710 mainline porting skeleton\ndtb-$(CONFIG_ARCH_HISI) += hi6250-jkm.dtb\n' >> "$MK"
fi
grep -n 'hi6250-jkm' "$MK"

echo "== 2/3 CI 配置与文档（随源码一起写回分支） =="
mkdir -p "$SRC/.github" "$SRC/docs"
cp -a "$REPO/.github"/. "$SRC/.github/"
cp "$REPO/README.md" "$SRC/README.md"
cp -a "$REPO/docs"/. "$SRC/docs/"
# 4.9 GSI 时代的工作流只对 JKM_EMUI9.1_GSI* 生效，留在本分支是死代码，清掉
rm -f "$SRC/.github/workflows/build-kernel.yml" \
      "$SRC/.github/workflows/rebuild-source.yml"
rm -rf "$SRC/.github/kernel-config"
ls -l "$SRC/.github/mainline/scripts" | head

# Linux 顶层 .gitignore 有 ".*" 规则，会把 .github/ 整个忽略掉；
# 不追加例外的话 CI 写回分支时 workflow 与移植层会全部消失（踩过一次）。
GI="$SRC/.gitignore"
if [ -f "$GI" ] && ! grep -qF '!.github/' "$GI"; then
  printf '\n# keep the JKM porting CI files visible to git\n!.github/\n' >> "$GI"
fi
grep -nF '!.github/' "$GI"

echo "== 3/3 自检 =="
test -f "$SRC/.github/workflows/mainline.yml"
test -f "$SRC/.github/mainline/config/base.config"
test -f "$SRC/docs/REVERSE_ENGINEERING.md"
grep -q 'hisilicon,kirin710' "$DTS_DIR/hi6250-jkm.dts"
echo "移植层叠加完成"
