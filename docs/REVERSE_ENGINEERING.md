# JKM 逆向证据与硬件事实清单

本文记录本次移植用到的**观察（observation）**、**推断（inference）**与**未知（unknown）**，
并保留 REA 的证据 ID，方便后续复核。结论分三类，不把推断当事实写。

**方法边界**：厂商 4.9.148 内核源码是**开源完整仓库**，这类目标按 REA 的使用原则应当用
普通源码工具读取，不跑 REA；REA 用在**已发布的闭源二进制**上（发布镜像、触控固件）。
两者交叉验证，不混为一谈。

## 1. REA 会话与可用能力

`mcp__rea__binary_session`：rea-agents 6.8.0，win32/x64，未打开 target。

| 能力 | 状态 | 影响 |
| --- | --- | --- |
| `inspect_byte_entropy`、`inspect_devicetree`、`inspect_elf_notes`、`inspect_kernel_module`、`inspect_native_exports` | 可用（target-free） | 本次实际使用熵分析 |
| Ghidra | `not_configured`（缺 `GHIDRA_INSTALL_DIR`） | 不能反汇编 / 出伪代码 |
| Hopper | `unsupported_host`（win32） | 不可用 |
| IDA MCP | `not_configured` | 不可用 |
| Kuna | `not_configured` | 不可用 |
| binwalk / unblob（`inspect_firmware_regions`、`extract_firmware`） | `unsupported_host`，需 Linux + 外部工具 | 固件分区提取未做 |
| ADB / JADX / apktool | 不可用 | 无实机与 APK 分析 |

## 2. REA 证据

### E1 — 闭源触控固件 `huawei.bin`

来源：`JKM_EMUI9.1_GSI` 分支（厂商 4.9.148 完整源码树）
`drivers/huawei_platform/touthscreen/panel/firmware/huawei.bin`（131,104 字节）。

- `inspect_byte_entropy`，`block_size=4096`，`detail=summary`
- Evidence：`ev_6f150f9002c4ebf788a5e55f7fb11c6aed6bf4d32b025378e298d33f751f7f6b`
  （父记录 `ev_33e1629fdfc6310bb310125e7bdca17262b719c0e2f721158812ae8ddee8b75c`）
- 观察：sha256 `ab68770fcaaa94390513958fd3855efb92f6dbe74179435c605391ec20f99eb2`，
  全文件熵 **5.4201**，33 个块，2 段低熵（≤1.0）：offset `90112` 长 `32768`、
  offset `126976` 长 `4128`。
- 推断：前 ~90 KiB 是固件有效载荷，尾部存在大段常量填充区。**熵不能区分压缩与加密**
  （证据自带的 limitation），所以不能据此断言固件被加密。
- 未知：载荷格式、校验和、寄存器编程序列。需要 Linux runner 上的 binwalk/unblob +
  反汇编引擎才能继续。

### E2 — 厂商发布镜像 `JKM_EMUI9.1_KernelSU.img`

来源：`xixiaobei-bei/KernelSU_on_Huawei` release `v22.0` 的 JKM 产物（13,121,536 字节）。

- `inspect_byte_entropy`，`block_size=65536`，`detail=summary`
- Evidence：`ev_ef25f2df002ca4e029924cd22df8025a90e77e0a2b5b856a220efcdb139a42b1`
  （父记录 `ev_f72b388c38e21b61aac94036ac3ecc9fae976ab0f40efbec6353fd628849a8fc`）
- 观察：sha256 `bee79c4e29e8943758251a14ed9fe233e33e2b33ac3f2ad325d147c7d8e4683a`，
  全文件熵 **7.9705**，201 个块，唯一一段高熵（≥7.2）覆盖整个文件。
- 交叉验证：与离线解析出的「gzip 压缩内核占满整个 payload」一致 —— 说明这个文件没有
  明文配置区、也没有未压缩的 DTB。

### E3 — REA 对 boot image 的格式判定

- `open_binary` → `target_unavailable / unsupported_target`，
  `reason: "unsupported binary format"`，`file_format_rejected`。
- 结论（观察）：Android boot image 不在该 REA 版本支持的容器/可执行格式里。
  因此容器内部结构改用离线解析（见下），**不是**靠 REA 读出来的；REA 在这里提供的是
  「格式不适配」这一明确的负向证据，以及上面的熵证据。

### E4 — 用 REA 解析 CI 产出的主线 DTB（交叉验证移植层）

**性质说明**：这一节的输入是**我们自己 CI 编出来的** `hi6250-jkm.dtb`，不是厂商闭源二进制。
用途是让 REA 从最终二进制里把 `reg` / `interrupts` 读回来，验证 DTS 骨架与厂商事实逐字节一致，
属于"移植结果的结构化复核"，不要当成对厂商固件的逆向。

之前手头没有 FDT blob，`inspect_devicetree` 无从下手；第二轮 CI 产出 DTB 后补做。

- 输入：`hi6250-jkm.dtb`，4154 字节，sha256 `5f47bc57c315239f8fb89ed505d3e9991f31ae6ada3a079d381a578fbda8d23c`，FDT v17
- `inspect_devicetree` 父 Evidence `ev_3a9ed1415ced7972fee1d0dabc04f44d448221a7e9ebaa175f6c146146c7a8a1`
  （summary 视图 `ev_b8f1f13b1e077aa623ad1ef3b9018e8659838c0e7bafc545633590c37e785031`）
  - 37 节点 / 123 属性；2 个 phandle 引用、**0 个未解析引用**；0 个 memreserve
  - `compatible = ["huawei,jkm", "hisilicon,kirin710"]`
  - `bootargs = "console=ttyAMA0,115200n8 earlycon loglevel=8"`
  - `stdout-path = "serial0:115200n8"`，alias `serial0 → /soc/serial@fdf02000`
- 节点视图 `ev_60b6d20ad95c32f5dfaa7407369ea2bbee741bc8fb916e9729322b12d91a4f42`：
  完整 37 条路径（8 个 CPU、7 个 reserved-memory、7 个 PL011、4 个 TODO(RE) 私有控制器）。

REA 直接解出的 `reg` 元组与厂商 4.9 DTS 对照：

| 节点（REA 视图 Evidence） | REA 从 DTB 读回 | 厂商 DTS | 结论 |
| --- | --- | --- | --- |
| `/memory@400000`（`ev_d5ccef2d…`） | `0x400000 + 0xbfe00000` | `reg = <0x0 0x00400000 0x0 0xBFE00000>` | 一致 |
| `/soc/interrupt-controller@e82b1000`（`ev_2448b595…`） | `0xe82b1000+0x1000`、`0xe82b2000+0x1000`、`arm,gic-400`、phandle 1 | GICD/GICC 同 | 一致 |
| `/soc/serial@fdf02000`（`ev_d4b16414…`） | `0xfdf02000+0x1000`；`interrupts` hex `00000000 0000004a 00000004` = `<GIC_SPI 74 IRQ_TYPE_LEVEL_HIGH>`；clocks phandle 2 ×2；`status = "okay"` | `<0 74 4>` | 一致 |

这证明 DTS 骨架里的 `GIC_SPI` / `IRQ_TYPE_LEVEL_HIGH` / `GIC_CPU_MASK_SIMPLE(8)` 经 dtc
展开后，数值与厂商 4.9 DTS 完全相同 —— 不是"看着像"，是 REA 从二进制里读回来的。

**limitation（证据自带）**：REA 只解析单 cell 的 `interrupt-parent` / `remote-endpoint` /
`msi-parent` 引用；`clocks`、`interrupts` 这类 binding 相关数组不做语义解释，
地址转换与运行时硬件状态仍是未知。

## 3. 离线解析（独立实现，不复用任何 REA 结果）

脚本：`.github/mainline/tools/extract_vendor_boot.py`（`python3 extract_vendor_boot.py <img>`）。

**Android boot header v1（观察）**

| 字段 | 值 |
| --- | --- |
| magic / page_size / header_version | `ANDROID!` @0 / 2048 / 1 |
| kernel_size / kernel_addr | 13,119,401 / `0x00080000` |
| ramdisk_size / second_size | 0 / 0 |
| tags_addr / os_version | `0x07A00000` / `0x12000000` |
| cmdline | `loglevel=4 initcall_debug=n page_tracker=on unmovable_isolate1=2:192M,3:224M,4:256M printktimer=0xfff0a000,0x534,0x538 androidboot.selinux= buildvariant=user` |

**华为私有区（观察 + 推断）**：`extra_cmdline` 区前 24 字节是不透明数据，`id` 区
(1616..1648) 末尾有 `70 06 00 00`，`header_size`@1612 = 0，1648..2048 全零 —— 说明这个
镜像不是标准 AOSP `mkbootimg.py` 的直接输出，而是厂商加了私有头字段。
**推断**：这些字节不是启动必需（现有 GSI workflow 用标准 AOSP 脚本生成的镜像可刷），
所以本工程打包时按标准 v1 布局填零，只对齐 `base/offset/page/header_version/os_version`。

**内核内容（对 offset 2048 的 gzip 流解压，39,675,912 字节）**

| 项 | 值 |
| --- | --- |
| `linux_banner` | `Linux version 4.9.148-JKM_EMUI9.1_KernelSU_xixiaobei@Github (root@3fd932d02df5) (gcc version 4.9.x 20150123 (prerelease) (GCC) ) #1 SMP PREEMPT Mon Oct 26 11:48:06 UTC 2026` |
| IKCONFIG | 153,576 字节，`CONFIG_KSU=y`、`# CONFIG_KPROBES is not set`、`# CONFIG_SECURITY_SELINUX_DEVELOP is not set`、`CONFIG_ARCH_HISI=y`、`CONFIG_F2FS_FS=y`、`CONFIG_EROFS_FS=y`、`CONFIG_LOCALVERSION="-JKM_EMUI9.1_KernelSU_xixiaobei@Github"` |
| 字符串计数 | `KernelSU` ×159、`ksu_` ×69、`kernelsu` ×3 |
| FDT magic (`d00dfeed`) | **0 处** |

**推断**：厂商 DTB 不在 kernel 分区里，由 bootloader 从独立分区 / 独立镜像加载。
本工程因此同时产出裸 `boot.img` 与 `…_dtb-appended.img` 两种形态。

## 4. 硬件事实（来自厂商开源源码，逐项可追溯）

| 事实 | 值 | 来源 |
| --- | --- | --- |
| SoC compatible | `hisilicon,kirin710` | `config/kirin710/kirin710_cs_config.dts` |
| PSCI | `method = "smc"`，`cpu_on=0xc4000003`、`cpu_off=0x84000002` | 同上 |
| 内存 | `reg = <0x0 0x00400000 0x0 0xBFE00000>`（起始 4 MiB，约 3 GiB） | `kirin710_memory.dtsi` |
| 关键保留区 | modem `0x10000000+0xB00000`、iommu pgtable `0x10C00000+0x200000`、modem-mntn `0x12E00000+0x100000`、hhee `0x12F00000+0x600000`、fka/mntndump/ivp `0x13500000..` | 同上 |
| GIC | `arm,gic-400`，GICD `0xe82b1000`、GICC `0xe82b2000`，PPI 9 maintenance | `kirin710_gic.dtsi` |
| arch timer | 1,920,000 Hz；PPI 13/14/11/10，flags `0xff08` | `kirin710_timer.dtsi` |
| CPU | 4× Cortex-A53 (`reg 0..3`) + 4× Cortex-A73 (`reg 0x100..0x103`) | `kirin710_cpu.dtsi` |
| UART | PL011：`0xfdf02000`/SPI 74、`0xfdf00000`/75、`0xfdf03000`/76、`0xfdf01000`/77、`0xfdf05000`/78、`0xfff32000`/79、`0xffd74000`/114 | `kirin710_uart.dtsi` |
| sysctrl | `0xfff0a000` | `kirin710_cs_config.dts`；**与 cmdline `printktimer=0xfff0a000` 交叉验证一致** |
| crgctrl / pmctrl / pctrl / iomcu | `0xfff35000` / `0xfff31000` / `0xe8a09000` / `0xFFD7e000` | 同上 |
| defconfig | `CONFIG_CMDLINE="console=ttyAMA0"`、`CONFIG_SERIAL_AMBA_PL011=y`、`CONFIG_ARCH_HISI=y`、`CONFIG_LOCALVERSION_AUTO` 未开 | `arch/arm64/configs/merge_kirin710_defconfig` |
| 文件系统 | `/data` = f2fs（带 inline_data/xattr/fileencryption），`/system` `/vendor` = EROFS | IKCONFIG + 厂商 fstab |

主线 6.6 的 `arch/arm64/boot/dts/hisilicon/` 只有 hi3660 / hi3670 / hi6220 /
hi3798cv200 / hip0x，**没有 Kirin 710**，所以上表全部需要重新落到主线 binding 上。

## 5. 还没解决的（unknown）

- 时钟树：`kirin710_clock.dtsi`（119 KB）里 factor/gate 的寄存器位定义，主线需要
  `clk-hi6250` 驱动；目前 DTS 里是 `fixed-clock` 占位。
- PMIC：Hi6555v200 走 SPMI，主线没有对应驱动；`fastboot_pmic.dtsi` / `hisi_6555v200_*` 待逆向。
- 存储：MMC / UFS 控制器 binding 与 PHY 初始化序列；主线无驱动。
- 显示 / GPU：Mali-G51（panfrost 主线有驱动），但依赖 clock / regulator / IOMMU 先就绪。
- 闭源固件：ATF/BL31、modem、触控固件、GPU 微码的格式与加载约定。
- DTB 交付路径：bootloader 到底从哪个分区取 dtb，需从固件 GPT / xloader 确认。

## 6. 后续 REA 逆向怎么继续

在 **Linux runner** 上给 REA 配好外部工具后可以做（Windows 本机做不了）：

```bash
# 环境变量（REA 进程启动时生效）
GHIDRA_INSTALL_DIR=/opt/ghidra-12.1
REA_BINWALK_COMMAND=/usr/bin/binwalk        # binwalk 3.1.x
REA_UNBLOB_COMMAND=/usr/bin/unblob          # unblob 26.6.x
```

然后按目标路由：

| 目标 | REA 工具 | 想拿到什么 |
| --- | --- | --- |
| 华为固件包 / UPDATE.APP | `inspect_firmware_regions` → `extract_firmware` | 切出 xloader / ATF / modem / dtb 镜像 |
| 切出的 FDT blob | `inspect_devicetree`（本机即可用） | 完整节点、`reg` 元组、phandle 引用 |
| ATF / xloader ELF 或 raw | `open_binary` → `binary_overview` / `search_strings` / `analyze_function` | DDR 初始化、时钟/PMIC 上电序列、dtb 加载地址 |
| 触控固件 `huawei.bin` | `inspect_byte_entropy` → `open_binary` | 载荷格式、校验、寄存器序列 |
| 内核模块 `.ko` | `inspect_kernel_module` | `.modinfo` / CRC，判断哪些 vendor 模块必须保留 |

每次结论都要写清是观察还是推断，保留 Evidence ID 与覆盖边界；静态分析不能声称"观察到执行"。
