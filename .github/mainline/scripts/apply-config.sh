#!/usr/bin/env bash
# 把 config fragment 逐项写进 .config，并即时确认 scripts/config 真的改了值。
# 用法: apply-config.sh <linux-src> <out/.config> <frag1> [frag2 ...]
#
# fragment 格式：每行 CONFIG_FOO=y|n|m（字符串值请用 sed 单独处理，
# 因为 scripts/config --set-str 遇到以 "-" 开头的值会把它当成选项）。
set -euo pipefail

KSRC="${1:?linux-src}"
CFG="${2:?config path}"
shift 2
CONF="$KSRC/scripts/config"
test -x "$CONF" || { echo "::error::scripts/config 不存在"; exit 1; }
test -f "$CFG" || { echo "::error::.config 不存在"; exit 1; }

for FRAG in "$@"; do
  echo "== 应用 $FRAG =="
  test -f "$FRAG" || { echo "::error::fragment 不存在: $FRAG"; exit 1; }
  # CRLF 会让 val 变成 "y\r"，case 匹配不上 —— 统一去掉
  while IFS='=' read -r OPT WANT; do
    [ -n "$OPT" ] || continue
    NAME="${OPT#CONFIG_}"
    case "$WANT" in
      y) "$CONF" --file "$CFG" --enable "$NAME" ;;
      m) "$CONF" --file "$CFG" --module "$NAME" ;;
      n) "$CONF" --file "$CFG" --disable "$NAME" ;;
      *) echo "::error::不支持的值 '$WANT' ($OPT)"; exit 1 ;;
    esac
    # 即时校验：scripts/config 必须真的把这一项写进去了
    if [ "$WANT" = "n" ]; then
      grep -q "^# ${OPT} is not set" "$CFG" || { echo "::error::$OPT 未能关闭"; exit 1; }
    elif [ "$WANT" = "m" ]; then
      grep -q "^${OPT}=m" "$CFG" || { echo "::error::$OPT 未能设为 m"; exit 1; }
    else
      grep -q "^${OPT}=y" "$CFG" || { echo "::error::$OPT 未能开启"; exit 1; }
    fi
    echo "  $OPT=$WANT"
  done < <(tr -d '\r' < "$FRAG" | grep -E '^CONFIG_[A-Z0-9_]+=(y|m|n)$' || true)
done
echo "fragment 应用完成（olddefconfig 之后由 verify.py 做最终断言）"
