# 从"能编译"到"真机可启动"

目标是把 JKM / Kirin 710 跑在长期维护的主线 LTS 内核上。**CI 只能保证编译与产物可校验，
不能保证开机** —— 开机要靠实机串口调试循环，这部分必须分阶段推进，且每一步都要有
独立的验收标准。当前进度停在**阶段 0**。

## 阶段 0 — 可复现编译基线 ✅ 已完成

- kernel.org 6.6.162 源码 sha256 钉死，CI 下载→叠加→编译→打包→校验→写回分支
- `hi6250-jkm.dtb` 描述 CPU / 内存 / GIC / timer / PL011，能被 `dtc` 反编译
- 产物校验覆盖 banner、IKCONFIG、KSU 代码、DTB 节点
- **不含**任何让真机进入内核的驱动；刷进去预计黑屏

## 阶段 1 — 串口 bring-up

目标：串口上看到 `Booting Linux on physical CPU 0x0` 和 `Linux version 6.6.162-…`。

| 项 | 内容 |
| --- | --- |
| 输入物料 | `hi6250-jkm.dtb`、`Image.gz`、厂商 dtb / xloader / fastboot 镜像、设备串口 |
| 待解决 | ① bootloader 从哪个分区取 dtb（裸 `dtb.img`？`kernel` 尾部 appended？）；② UART0 真实时钟（现在 DTS 里是 `fixed-clock` 19.2 MHz 占位）；③ 华为 boot header 私有区是否必须复刻 |
| REA 逆向 | 从固件包切出 xloader/ATF（`inspect_firmware_regions` + `extract_firmware`，需 Linux runner），`inspect_devicetree` 解析厂商 dtb 拿到完整 `reg`/时钟/phandle，`search_strings` 找 dtb 加载地址与串口初始化 |
| 验收 | 稳定输出 earlycon 日志到解压完成；`--appended` 与裸 dtb 两种交付方式各测一次 |
| 阻塞 | 需要解锁 BL 的实机 + UART 引出或 `pstore` / ramoops |

## 阶段 2 — 时钟 / 电源 / pinctrl

| 项 | 内容 |
| --- | --- |
| 关键寄存器 | crgctrl `0xfff35000`、pmctrl `0xfff31000`、pctrl `0xe8a09000`、sysctrl `0xfff0a000`、iomcu `0xFFD7e000` |
| 闭源点 | Hi6555v200 PMIC 走 SPMI，厂商驱动在 `drivers/huawei_platform/`；主线没有对应 binding |
| 做法 | 以主线 `drivers/clk/hisilicon/clk-hi3660.c` / `clk-hi6220.c` 为模板写 `clk-kirin710`；**厂商 119 KB 时钟树已经结构化成 [CLOCK_FACTS.md](CLOCK_FACTS.md)**（444 个节点、控制器基址、UART0 完整时钟链与门控位）；SPMI 控制器先按 `drivers/spmi` 现有 binding 试 |
| REA 逆向 | 对 xloader/ATF 里的上电序列做反汇编（`open_binary` → `analyze_function`），核对 PMIC 初始化顺序；闭源 `.ko` 用 `inspect_kernel_module` 看依赖符号 |
| 验收 | `/sys/kernel/debug/clk/clk_summary` 有完整时钟树，CPU 能变频，UART 不再依赖 fixed-clock |

## 阶段 3 — 存储与 rootfs

- MMC / UFS 控制器 + PHY 初始化序列，依赖阶段 2 的 clock / regulator
- 主线 `drivers/mmc/host/` 有可参照的 DW MMC 驱动；厂商 `kirin710_mmc.dtsi` / `kirin710_ufs.dtsi` 提供寄存器
- 验收：能挂载一个 initramfs/ext4 rootfs 并进到 shell

## 阶段 4 — 显示 / GPU / 触摸

- GPU 是 Mali-G51，主线 panfrost 支持，但需要 clock、regulator、IOMMU 先就绪
- DSI / 面板驱动与背光；触控固件是闭源（`huawei.bin` 等 40+ 个 `.bin/.img`），
  主线阶段建议先保留 vendor 侧加载路径
- 验收：DRM/KMS 点亮屏幕、framebuffer console 有输出

## 阶段 5 — Android 用户态兼容

- 主线 6.6 有 `CONFIG_ANDROID_BINDER_IPC`，但**没有 ashmem**（改用 memfd）、
  没有 Android 专属的 `android-common` 补丁集、`CONFIG_ANDROID_LOW_MEMORY_KILLER` 等
- `/data` 是 f2fs + inline_data/xattr + FBE，`/system` `/vendor` 是 EROFS；
  内核侧要 `F2FS_FS_ENCRYPTION` + `CRYPTO_XTS/CTS` + `EROFS_FS` + LZ4/LZ4M 压缩
- vendor HAL 与主线内核的接口差异（ION → DMA-BUF heap、HISI 私有 ioctl）是最大障碍
- 验收：能起 Android init / GSI，`getprop` 与 `dmesg` 无致命 avc/mount 错误

## 阶段 6 — modem / 音频 / 相机 / 传感器

- 这些高度依赖 TrustZone、闭源固件与 inputhub；主线移植通常**不重实现**，
  而是保留 vendor 侧固件加载 + 逆向出寄存器接口写最小 shim
- 这是 REA 深度逆向的主战场：binwalk/unblob 切固件 → Ghidra 反汇编 →
  `trace_native_values` / `analyze_function` 还原初始化序列
- 验收：基带注册、音频通路、相机预览

## 真机调试前置条件

1. 解锁 Bootloader，完整备份所有分区（尤其 `kernel_a`、`recovery_ramdisk_a`、`dtb` 相关分区）。
2. 确认 GPT 分区名带 `_a` 后缀（华为方案：有 `_a` 无 `_b`），刷机脚本要「裸名失败就试 `_a`」。
3. 准备一键日志收集脚本：`/proc/version`、关键 getprop、`getenforce`、mounts、`dmesg`、
   logcat（FATAL/avc/mount/vold）、HAL 进程列表。
4. 每次只改一个变量；`CONFIG_LOCALVERSION` 让设置里能直接确认刷的是哪一档。

## CI 能验证什么、不能验证什么

| 能 | 不能 |
| --- | --- |
| 源码版本与 sha256、defconfig 关键项、banner / LOCALVERSION | 时钟是否真的驱动起来 |
| Image.gz 能否链接、KSU 代码是否真的编进去 | bootloader 是否接受这个 header |
| DTB 语法与关键节点 | 硬件寄存器值是否正确 |
| 产物与厂商 release 的 header 参数一致性 | 开机、显示、触摸、基带 |

**结论**：阶段 0 的"构建交给 CI"已经落地；阶段 1 起需要实机与固件物料，
没有串口日志之前不要声称任何一步完成。
