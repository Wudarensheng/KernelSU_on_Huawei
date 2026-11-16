# Kirin 710 时钟树事实清单（主线阶段 2 输入）

来源：厂商 4.9.148 开源源码
`arch/arm64/boot/dts/auto-generate/config/kirin710/kirin710_clock.dtsi`（119,343 字节，444 个时钟节点）。
这是**完整开源源码**，按 REA 的使用原则用普通源码工具读取即可，不跑 REA；REA 留给闭源二进制。

目的：把 DTS 骨架里那个 `fixed-clock` 占位换成有依据的时钟描述，并给后续
`clk-kirin710` 驱动移植提供逐项可追溯的寄存器/位定义。

## 控制器基址

| 节点 | compatible | reg | 用途 |
| --- | --- | --- | --- |
| `hisi_clk_crgctrl` | `hisilicon,clk-crgctrl` | `0xfff35000 + 0x1000` | 主 CRG（clock/reset/gate），UART 时钟都挂在这里 |
| `hisi_clk_pmctrl` | `hisilicon,clk-pmctrl` | `0xfff31000 + 0x1000` | 电源/PPLL 控制 |
| `hisi_clk_pmuctrl` | `hisilicon,hi6421pmic` | `0xfff34000 + 0x1000` | PMU 侧门控（status disabled） |
| `hisi_clk_sctrl` | `hisilicon,clk-sctrl` | `0xfff0a000 + 0x1000` | sysctrl，与 cmdline `printktimer=0xfff0a000` 一致 |
| media1 / media2 / iomcu CRG | `hisilicon,media1-crg` 等 | `0xe87ff000` / `0xe8900000` / `0xFFD7E000` | 多媒体与 IOMCU 时钟域 |

DTS 骨架里的 `TODO(RE)` 私有控制器节点基址就是上表，已经有依据，不是猜的。

## 固定输入时钟

| 节点 | 频率 | 备注 |
| --- | --- | --- |
| `clkin_sys` | **19,200,000 Hz** | 系统晶振，也是 PL011 参考时钟的根 |
| `clkin_ref` | 32,764 Hz | 32.768 kHz 附近，RTC/低功耗域 |
| `clk_fpga_20m` / `clk_fpga_160m` | 20 MHz / 160 MHz | FPGA 调试用 |

**这解释了 DTS 骨架里的 `uartclk` 为什么填 19.2 MHz**：它与厂商 `clkin_sys` 一致。
但要注意——主线内核里 UART 真正拿到的是 `clk_mux_uart0` 的输出，可能是 19.2 MHz，
也可能是分频后的 `clk_div_uart0`，取决于复位后 mux 的默认值。所以 19.2 MHz 只是
**bootloader 遗留配置下的合理占位**，不是已验证的运行时频率。

## UART0 完整时钟链（从根到 PL011）

```
clk_div_320m
  └─ clk_andgt_uart0   hisilicon,clk-gate        clkgate  = <0xF4 9>      always_on / hiword
      └─ clk_div_uart0 hisilicon,hi3xxx-clk-div  clkdiv   = <0xB0 0xf0>, table <16 1>
          └─ clk_mux_uart0 hisilicon,hi3xxx-clk-mux clkmux-reg = <0xAC 0x4>
               sources: <&clkin_sys  &clk_div_uart0>, hiword
               ├─ clk_gate_uart0  hisilicon,hi3xxx-clk-gate  <0x20 0x400>
               │    └─ clk_factor_uart0  fixed-factor 1:1  →  PL011 "uartclk"
               └─ (pclk 支链) clk_mux_uartl → pclk_gate_uart0 fixed-factor 1:1 → "apb_pclk"
```

寄存器都是 `crgctrl (0xfff35000)` 内的偏移，门控值是 bit mask（不是 bit 号）：
UART0 门控 `0x20` 偏移的 `0x400`（bit 10），UART1 是 `0x20` 偏移的 `0x800`（bit 11）。

其余 6 个 PL011 的时钟节点同样在 `kirin710_clock.dtsi` 里，检索
`clk_gate_uart[1-6]` / `pclk_gate_uart[1-6]` 即可。

## 节点类型分布（可用于估算驱动工作量）

| compatible | 数量 | 主线对应框架 |
| --- | --- | --- |
| `hisilicon,hi3xxx-clk-gate` | 188 | `clk_gate` + 自定义 hiword/mask 语义 |
| `fixed-factor-clock` | 61 | 主线原生 `clk-fixed-factor` |
| `hisilicon,hi3xxx-clk-mux` | 53 | `clk_mux`（hiword 写 1 需自定义） |
| `hisilicon,hi3xxx-clk-div` | 46 | `clk_divider`（带 div table） |
| `hisilicon,clk-gate` | 46 | `clk_gate` |
| `fixed-clock` | 35 | 主线原生 |
| 其它（ppll / pmu-gate / xfreq / interactive / media-crg） | 19 | 需单独移植 |

合计 444 个节点。**其中大部分是 gate/div/mux/fixed-factor 四类**，可以基于主线
`drivers/clk/hisilicon/clk-hi3660.c`（同为 Hisilicon、同为 hi3xxx 系列语义）做模板，
不需要从零反汇编。真正需要 REA 深度逆向的是 ATF/xloader 里的上电与 PLL 初始化序列
（见 `REVERSE_ENGINEERING.md` 第 6 节）。

## 阶段 2 的落地顺序

1. 写 `clk-kirin710` 驱动，注册 crgctrl 的 gate/div/mux；`clkin_sys` / `clkin_ref` 用主线 `fixed-clock`。
2. DTS 骨架里把 `uartclk: clock-uart` 改指向 `&clk_mux_uart0` 的 phandle，
   并把 `crgctrl` 节点从 `status = "disabled"` 改成 `okay`。
3. 删掉四个 `TODO(RE)` 里已实现的部分，保留 PMIC / pinctrl 占位。
4. 验收：串口不再依赖 fixed-clock 也能输出，`/sys/kernel/debug/clk/clk_summary` 能看到完整树。

**当前状态：只做了事实提取，驱动未写。** 在阶段 2 完成前，DTS 里的 19.2 MHz 占位仍可能
与实机不符，别把串口无输出直接归咎于 PL011。
