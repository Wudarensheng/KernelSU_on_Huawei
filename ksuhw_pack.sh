#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C          # 保证 ${#str} 按字节计数, cmdline 是 ASCII

HEADER="${HEADER:-${PWD}/stock/header.bin}"
TAIL="${TAIL:-${PWD}/stock/tail.bin}"
DTB_BLOB="${DTB_BLOB:-${PWD}/stock/dtb_blob.bin}"
GZ="${GZ:-${PWD}/kernel-source/out/Image.gz}"
OUT="${OUT:-${PWD}/kernel.img}"
EXTRA_CMDLINE="${1:-}"

HEADER_SIZE=2048
CMD_OFF=64               # 标准 boot header 的 cmdline 偏移
CMD_LEN=512              # cmdline 长度

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# ---------- 0. 校验输入 ----------
for f in "$HEADER" "$TAIL" "$DTB_BLOB" "$GZ"; do
    [ -r "$f" ] || { echo "找不到: $f" >&2; exit 1; }
done
[ "$(stat -c%s "$HEADER")" -eq "$HEADER_SIZE" ] || { echo "header.bin 不是 $HEADER_SIZE 字节" >&2; exit 1; }

# ---------- 1. 复制 header 到临时文件 ----------
cp "$HEADER" "$TMP/header"

# ---------- 2. 可选: 追加 cmdline (offset 64, 长度 512) ----------
if [ -n "$EXTRA_CMDLINE" ]; then
    orig=$(dd if="$HEADER" bs=1 skip=$CMD_OFF count=$CMD_LEN 2>/dev/null | tr -d '\0')
    new="$orig $EXTRA_CMDLINE"
    n=${#new}
    if [ "$n" -ge "$CMD_LEN" ]; then
        echo "cmdline 溢出: $n >= $CMD_LEN" >&2
        exit 1
    fi
    printf '%s' "$new" > "$TMP/cmd"
    head -c $((CMD_LEN - n)) /dev/zero >> "$TMP/cmd"
    dd if="$TMP/cmd" of="$TMP/header" bs=1 seek=$CMD_OFF conv=notrunc 2>/dev/null
    echo "附加 cmdline: $EXTRA_CMDLINE"
fi

# ---------- 3. 计算新 kernel_size 并写回 header offset 8 ----------
gz_size=$(stat -c%s "$GZ")
blob_size=$(stat -c%s "$DTB_BLOB")
kernel_size=$((gz_size + blob_size))
hex=$(printf '%08x' "$kernel_size")
le="${hex:6:2}${hex:4:2}${hex:2:2}${hex:0:2}"         # 转小端字节序
echo -n "$le" | xxd -r -p > "$TMP/ks"
dd if="$TMP/ks" of="$TMP/header" bs=1 seek=8 conv=notrunc 2>/dev/null
echo "Image.gz 大小: $gz_size"
echo "DTB blob 大小: $blob_size"
echo "新 kernel_size: $kernel_size"

# ---------- 4. 组装: header + Image.gz + DTB blob + tail ----------
: > "$OUT"
cat "$TMP/header"  >> "$OUT"
cat "$GZ"          >> "$OUT"
cat "$DTB_BLOB"    >> "$OUT"
cat "$TAIL"        >> "$OUT"

echo "输出: $OUT, 大小: $(stat -c%s "$OUT")"

mv kernel.img "$ARTIFACT_NAME".img