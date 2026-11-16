# KernelSU 可选档位：CI 编译与产物校验已通过，真机运行未验证

**截至 2026-11-15：`ksu=true` 档在 Linux 6.6.162 主线上已编译通过并通过全部 39 项产物校验**
（2026-11-15 21:51 实测）。**真机运行状态未验证** —— 编译通过 ≠ 能开机，
刷机前先读 [PORTING_ROADMAP.md](PORTING_ROADMAP.md)。

集成目标：`SukiSU-Ultra/SukiSU-Ultra` 的旧内核兼容提交 `4a0c01e`（2026-04-01
「移植并修复旧内核兼容性支持 #852」），由 `.github/mainline/scripts/setup-ksu.sh`
用其 `kernel/setup.sh` 挂载到 `drivers/kernelsu`。选它是因为它按
`LINUX_VERSION_CODE` 做了 5.10 / 6.4 / 6.7 的版本分支，比官方 KernelSU main
（只支持 GKI）更适合非 GKI 内核。

## 实测记录

| 时间 | 结果 | 关键信息 |
| --- | --- | --- |
| 2026-11-15 第一次 | failure | `security/selinux/include/security.h:21: fatal error: flask.h: No such file or directory` |
| 2026-11-15 第二次 | failure | `security/selinux/ss/sidtab.h:72: 'CONFIG_SECURITY_SELINUX_SIDTAB_HASH_BITS' undeclared` |
| 第三次 | 未能触发 | `POST /actions/workflows/mainline.yml/dispatches` 返回 404（workflow 不在默认分支） |
| **2026-11-15 21:51** | **success** | `ksu=true` + `push_source=false`；**39 项校验全绿**，`CONFIG_KSU=y`、KSU 字符串在 Image 中命中 305 次 |

（历史构建记录已随分支历史清理删除，产物 sha256 存档于 [README](../README.md)。）

## 已修并通过验证的两层

**1. `flask.h` / `av_permissions.h` 不是源码树文件了。**
4.9 时代它们静态放在 `security/selinux/include/`；主线 6.6 改由
`scripts/selinux/genheaders` 生成到 `$(obj)/security/selinux/`。KSU 的
`kernel_compat.h` 直接 include `security/selinux/include/security.h`，其中
`#include "flask.h"` 只在同目录找，而 KSU 编译不带 `$(obj)` 的 `-I`。
修法：`setup-ksu.sh` 用主线自带生成器提前把两份头放回 include/，
`-I` 参数照抄 `scripts/selinux/genheaders/Makefile` 的 `HOST_EXTRACFLAGS`
（`include/uapi`、`include`、`security/selinux/include`）。
**已验证**：构建日志确认生成成功（`flask.h` 8621 B、`av_permissions.h` 105476 B），
`drivers/kernelsu/selinux/{selinux,sepolicy,rules}.o` 全部编译通过并链入 built-in.a。

**2. 主线 arm64 defconfig 没开 SELinux。**
`CONFIG_SECURITY_SELINUX` 依赖 `SECURITY_NETWORK && AUDIT && NET && INET`，
默认 `n`；而 `CONFIG_SECURITY_SELINUX_SIDTAB_HASH_BITS` /
`CONFIG_SECURITY_SELINUX_SID2STR_CACHE_SIZE` 这两个 int 只在
`SECURITY_SELINUX=y` 时才由 Kconfig 写进 `autoconf.h`。KSU 无条件 include
`ss/policydb.h` → `sidtab.h`，于是报宏未定义。
修法：`ksu.config` 补 `AUDIT` / `SECURITY_NETWORK` / `NET` / `INET` /
`SECURITY_SELINUX` / `SECURITY_SELINUX_DEVELOP`（最后一项也是 permissive 的前提）；
并在生成 `.config` 后对 ksu 档做关键项断言，依赖没开全就在 1 分钟内失败。
**已验证**：构建中两个 int 选项分别生成（`SIDTAB_HASH_BITS=9`、
`SID2STR_CACHE_SIZE=256`），workflow 内的断言逐项 `OK`。

## 2026-11-15 校验结果（verify.py，独立于打包流程的第二套解析）

- boot header v1：`ANDROID!`@0、page 2048、`kernel_addr=0x00080000`、
  `tags_addr=0x07A00000`、`os_version=0x12000000`、ramdisk/second=0 —— 与厂商 release 一致
- `linux_banner = 6.6.162-JKM_mainline6.6_ksu_Wudarensheng@Github`
- IKCONFIG 10684 行，与构建 `.config` 交叉核对一致
- KSU：`CONFIG_KSU=y` + Image 内 KSU 字符串 305 次命中（>50），代码确实链入
- DTB：`hisilicon,kirin710` / `arm,gic-400` / `arm,pl011` / `arm,psci` / memory 节点齐全
- 汇总：**39 项通过，0 项失败**

## dispatch 触发限制（仍然成立）

`workflow_dispatch` API 只能调用**默认分支**上列出的 workflow。`mainline.yml`
只存在于移植分支上时，API dispatch 返回 404；当时能触发是因为先把 workflow
同步了一份到默认分支。可选触发方式：

1. **Actions 页面手动触发**：打开 `mainline-lts` → Run workflow，选移植分支，
   UI 不要求 workflow 在默认分支。
2. **把 `mainline.yml` 同步一份到默认分支**，之后 API dispatch 稳定可用。
   注意默认分支上另有多个 workflow，新增文件前先检查它们的 `on.push.paths`
   有没有监听 `.github/**`。
3. **push 自动触发**：往移植分支推 `.github/workflows/mainline.yml`、
   `.github/mainline/**`、`docs/**` 或 `README.md` 会自动跑一次（默认 clean 档 +
   `push_source=true`）。

## 剩余风险（真机未验证）

- **运行期行为**：`selinux/rules.c` 的 `>= 5.10` 路径（`selinux_state.policy` +
  `policy_mutex`）编译通过，但运行期 KSU hook / supercall / manager 识别
  （`4a0c01e` 用签名白名单，6 家 manager）在真机上没人测过。
- **SELinux 状态**：产物 cmdline 带 `androidboot.selinux=permissive`，
  `CONFIG_SECURITY_SELINUX_DEVELOP=y` 已开；先用 permissive 排障，稳定后再切 enforcing。
- **启动前提**：本档与纯净档共享同一份 DTS 骨架，开机本身受
  [PORTING_ROADMAP.md](PORTING_ROADMAP.md) 阶段 1+ 约束 —— 没有串口日志之前
  不要把任何失败归因于 KSU。

**设计约定**：`ksu` 默认 `false`，且 ksu 档务必带 `push_source=false`，
避免把带 KSU 的源码树写回主线分支。这个档位失败不应阻塞纯净主线基线。
