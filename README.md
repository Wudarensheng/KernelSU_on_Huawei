# JKM 主线 Linux LTS 内核移植（CI 构建）

目标机型：**JKM / 华为畅享 9 Plus（Kirin 710）**。本分支把原来华为 4.9.148 厂商内核
整体替换成 kernel.org 主线 LTS 内核，并叠加一层可复现的移植配置；**编译、打包、校验、
源码写回全部由 GitHub Actions 完成**，本地不需要工具链，也不需要上传 1.4 GB 源码。

> ⚠️ **当前状态：能编译、能产出可校验的镜像，但还不能让真机开机。**
> 主线 6.6 没有任何 Kirin 710 的时钟 / 电源 / 存储 / 显示驱动。请先读
> [docs/PORTING_ROADMAP.md](docs/PORTING_ROADMAP.md)，不要直接把产物刷进手机。

## 版本沿革

| 项 | 值 |
| --- | --- |
| 分支名 | `jkm_Mainline_6.6lts` |
| 实际内核 | **Linux 6.6.162 LTS**（kernel.org 长期维护分支），源码包 sha256 `4aa89f36…dc06f06` |
| 替换前 SHA | `50ba67d097f5f2f0a392a51bbfdad7fb4953cbbd`（4.9 旧树，仍可从 `JKM_HarmonyOS2.0_SukiSU-Ultra` 访问） |

> 升级内核版本时，改 `.github/workflows/mainline.yml` 里的 `KVER` 和 `KERNEL_SHA256`
> 两个变量，并以 kernel.org releases.json 确认目标是 longterm。

## 许可证

本分支是 **Linux 内核源码树，按 GPL-2.0-only 授权**，以树内的 `COPYING` 与 `LICENSES/`
为准。仓库默认分支挂的 GPL-3.0 `LICENSE` 属于原 KernelSU 项目文档，**不适用于本分支**，
两者（GPL-2.0-only 与 GPL-3.0）不兼容，因此本分支不放 GPL-3.0 许可证文件。
新增的移植层文件（`.github/`、DTS、脚本）同样按 GPL-2.0 提供。

## 目录结构

```
.github/workflows/mainline.yml          唯一的 CI workflow
.github/mainline/config/base.config     主线基线 config fragment
.github/mainline/config/ksu.config      KernelSU 档叠加 fragment
.github/mainline/dts/hi6250-jkm.dts     Kirin 710 设备树骨架
.github/mainline/scripts/               叠加 / 集成 / 打包 / 校验脚本
docs/REVERSE_ENGINEERING.md              逆向证据与硬件事实清单（含 REA 读回的 DTB reg）
docs/CLOCK_FACTS.md                      Kirin 710 时钟树事实清单（阶段 2 输入）
docs/PORTING_ROADMAP.md                  真机可启动分阶段路线图
docs/KSU_STATUS.md                       KernelSU 可选档位实测状态与后续路径
(其余为 kernel.org 主线源码原样)
```

## CI 怎么用

在 Actions 里选 **mainline-lts** → Run workflow：

| 输入 | 默认 | 说明 |
| --- | --- | --- |
| `ksu` | `false` | `false` = 纯净主线（**已验证通过**）；`true` = 集成 KernelSU / SukiSU-Ultra（**CI 已验证通过**，2026-11-15 实测 39 项校验全绿；真机运行未验证，见 [KSU_STATUS.md](docs/KSU_STATUS.md)） |
| `push_source` | `true` | 把完整主线源码树写回本分支；只想要产物时关掉（ksu 档务必关掉） |

推送 `.github/workflows/mainline.yml`、`.github/mainline/**`、`docs/**` 或 `README.md`
也会自动触发一次构建。一次运行依次做：

1. 下载 `linux-6.6.162.tar.xz` 并校验 sha256，核对 `Makefile` 版本号；
2. 叠加 DTS / config fragment / 文档；
3. `aarch64-linux-gnu-gcc` 编 `Image.gz` + `hi6250-jkm.dtb`；
4. 打包 Android boot image v1（与厂商 release 的 header 参数对齐）；
5. 用**独立于打包流程**的解析器校验 banner / IKCONFIG / KSU 代码 / DTB；
6. 校验全过后，以当前分支 HEAD 为父提交 force push 完整源码树。

校验有一项不过就让 CI 失败，不会把半成品写回分支。

## 产物

| 文件 | 说明 |
| --- | --- |
| `JKM_mainline_6.6.162_<档位>.img` | Android boot image v1，`ANDROID!` 在偏移 0，ramdisk_size=0（与厂商一致） |
| `…_dtb-appended.img` | kernel 尾部附 DTB，给支持 appended-dtb 的 bootloader |
| `Image.gz` / `hi6250-jkm.dtb` | 单独产物，便于刷 dtb 分区或做串口调试 |
| `.config` / `System.map` / `verify-report.txt` | 审计用 |

版本串形如 `6.6.162-JKM_mainline6.6_clean_Wudarensheng@Github`，在设置 → 关于手机
里能分辨刷的是哪一档。`CONFIG_LOCALVERSION_AUTO=n`，不会追加 `-NNNN-g<sha>`。

### 已验证构建的哈希（审计用）

| 产物（2026-11-15 ksu 档构建，39 项校验全绿） | sha256 |
| --- | --- |
| `JKM_mainline_6.6.162_ksu.img` | `e7722ff1695c7d0e1641431a8ffb7230c27bd01827d21bc625f2e3c3bab0deae` |
| `JKM_mainline_6.6.162_ksu_dtb-appended.img` | `3cbb87eacbcec0987b94fac0dd135b2454f6472edc29fc7fed6c9bc6e3f43d23` |

clean 档（2026-11-16）与后续构建的哈希以对应 run 的 `verify-report.txt`
与 artifact 内文件实测为准；刷机前建议对下载到的文件重算一次 sha256 比对。

## 回滚

分支被 CI 覆盖后，用重建前的 SHA 强推回去：

```bash
git push --force origin 50ba67d097f5f2f0a392a51bbfdad7fb4953cbbd:refs/heads/jkm_Mainline_6.6lts
```

CI 日志里会打印每次运行时的实际 `重建前分支 SHA`，以那一次为准。

## 刷机前必读

- 解锁 Bootloader、完整备份、准备恢复方案；刷错内核会变砖。
- 华为 GPT 里应用分区带 `_a` 后缀（`kernel_a`、`recovery_ramdisk_a` …），
  `fastboot flash kernel` 可能报 `partition not found`，要试 `kernel_a`。
- 当前产物**不具备开机条件**，仅用于编译与串口 bring-up，不要当日用内核刷入。
