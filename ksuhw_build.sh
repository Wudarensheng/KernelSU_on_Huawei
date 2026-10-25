set -e

KSRC="${PWD}/kernel-source"
PRIMA_TAG="LA.UM.6.6.c32-10800-89xx.0"

# ---------- 配置 ----------
# 基于原厂完整配置, 做三处必要调整:
#   - 关闭 CONFIG_HUAWEI_CFI      : 依赖 AOSP 预置 gcc4.9 的 cfi.so 插件, 外部工具链没有
#   - 打开 WLAN 特性 (11W/11R/LFR/OKC/OFFLOAD/LINUX_REG/11AC_HIGH_TP):
#     prima 该版本的代码默认按 Android 构建路径开启这些特性, 否则编不过;
#     原厂其实也是用 Android 构建路径 (这些开关不进 .config), 效果一致
#   - 打开 CONFIG_NL80211_TESTMODE: prima 的 cfg80211 testmode_cmd 需要
mkdir -p $KSRC/out
sed 's/^CONFIG_HUAWEI_CFI=y/# CONFIG_HUAWEI_CFI is not set/
     s/^CONFIG_HUAWEI_CFI_TAG=.*/# CONFIG_HUAWEI_CFI_TAG is not set/
     s/^# CONFIG_WLAN_FEATURE_11W is not set/CONFIG_WLAN_FEATURE_11W=y/
     s/^# CONFIG_QCOM_VOWIFI_11R is not set/CONFIG_QCOM_VOWIFI_11R=y/
     s/^# CONFIG_ENABLE_LINUX_REG is not set/CONFIG_ENABLE_LINUX_REG=y/
     s/^# CONFIG_WLAN_OFFLOAD_PACKETS is not set/CONFIG_WLAN_OFFLOAD_PACKETS=y/
     s/^# CONFIG_PRIMA_WLAN_LFR is not set/CONFIG_PRIMA_WLAN_LFR=y/
     s/^# CONFIG_PRIMA_WLAN_OKC is not set/CONFIG_PRIMA_WLAN_OKC=y/
     s/^# CONFIG_PRIMA_WLAN_11AC_HIGH_TP is not set/CONFIG_PRIMA_WLAN_11AC_HIGH_TP=y/
     s/^# CONFIG_NL80211_TESTMODE is not set/CONFIG_NL80211_TESTMODE=y/' \
     ${PWD}/stock/ldn-al20_stock_config > $KSRC/out/.config

cat <<EOF >> "$KSRC/out/.config"
# BakaSU
CONFIG_KSU=y
CONFIG_KSU_MANUAL_HOOK=y
EOF

if [ "$PM_STATE" = "true" ]; then
  sed -i 's/^# CONFIG_SECURITY_SELINUX_DEVELOP is not set$/CONFIG_SECURITY_SELINUX_DEVELOP=y/' $KSRC/out/.config
  sed -i 's/^\(CONFIG_LOCALVERSION=".*\)\(_xixiaobei[^"]*@Github"\)$/\1_PM\2/' $KSRC/out/.config
fi
make -C "$KSRC" O=out olddefconfig

# ---------- 编译 ----------
# QCOM_TDLS / MDNS / LFR_MBB 不在 Kconfig 里, 只能通过命令行传入
# CONFIG_NO_ERROR_ON_MISMATCH=y 忽略 10 处良性段不匹配 (modpost 报错)
make -C "$KSRC" O=out -j"$(nproc)" \
  CONFIG_QCOM_TDLS=y CONFIG_MDNS_OFFLOAD_SUPPORT=y CONFIG_PRIMA_WLAN_LFR_MBB=y \
  CONFIG_NO_ERROR_ON_MISMATCH=y \
  Image.gz

# ---------- 导出产物 ----------
echo "== 编译完成 =="
echo "产物: ${KSRC}/out/Image.gz  (可配合 stock/ldn-al20.dtb 重打包刷入 kernel 分区)"
