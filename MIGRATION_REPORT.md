# ultrasound_B_mode_imaging 迁移报告

## 1. 迁移结果

- 源工程：`ultrasound_focus_B_mode_imaging`
- 目标工程：`ultrasound_B_mode_imaging`
- 器件：`xc7z030sbg485-2`
- 顶层：`heart_top`
- Block Design：`pus_ctrl_system`
- 工具：Vivado/Vitis 2026.1，Build 6511674
- 结果：Vivado 综合、实现、DRC、Bitstream、含 Bitstream XSA、Vitis SDT Platform、Standalone Application、FSBL 和 BOOT.BIN 均生成成功。
- 旧工程未修改。迁移开始时旧仓库位于 `usb_debug`，commit 为 `a8dbaef3190d7107840b88696217efcecdb3397d`，工作区 clean；迁移结束后 158 个受控旧源文件的 SHA-256 复核通过。

目标目录结构：

```text
build/src/          受控 BD、XCI、RTL 和 PS 手写源码快照
build/constraints/  迁移后的 XDC
build/output/       Bitstream、XSA、ELF、BOOT.BIN 和实现报告
build/reports/      迁移快照、参数、Bootgen 和哈希报告
ip_repo/            自定义 IP
vivado/             Vivado 2026.1 工程
workspace/          Vitis 2026.1 组件
build/scripts/      可重复构建和校验脚本
```

未复制旧 `xparameters.h`、BSP、Platform、FSBL、`ps7_init.*`、链接脚本、Bitstream、XSA、BOOT.BIN、DCP、IP cache、Output Products 或旧 HDL wrapper。

## 2. 可重复构建

从工程根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build\scripts\build_all.ps1 -Stage project
powershell -NoProfile -ExecutionPolicy Bypass -File .\build\scripts\build_all.ps1 -Stage hardware
powershell -NoProfile -ExecutionPolicy Bypass -File .\build\scripts\build_all.ps1 -Stage all
```

分步软件构建和复核：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build\scripts\run_vitis_2026_1.ps1 -Stage platform
powershell -NoProfile -ExecutionPolicy Bypass -File .\build\scripts\run_vitis_2026_1.ps1 -Stage all
powershell -NoProfile -ExecutionPolicy Bypass -File .\build\scripts\create_boot_image.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\build\scripts\check_migration.ps1
```

`check_migration.ps1` 会复核旧源哈希、受保护的软件序列/常量、RTL 允许变更范围、IP CONFIG 参数、地址、SDT 中断处理、XSA 内容、时序、必需产物和 BOOT.BIN 分区顺序。

## 3. 硬件配置与地址

| 项目 | Vivado 2026.1 结果 | 变化 |
|---|---:|---|
| PS FCLK0 | 100 MHz | 无 |
| DDR3 | 533.333374 MHz | 无实质变化 |
| M_AXI_GP0 | Enable | 无 |
| S_AXI_HP0 | Enable | 无 |
| AFE AXI-Lite | `0x43C00000`, 64 KiB | 无 |
| AXI BRAM | `0x40000000`, 16 MiB | 无 |
| W_DDR master address space | `0x00000000`, 1 GiB | 无 |
| W_DDR write target | `0x20000000` | 无 |
| PS DDR usable range | `0x00100000`–`0x3FFFFFFF` | 无 |
| BRAM SPI start | `0x40011528` | 无 |
| BRAM beamform control | `0x4001152C` | 无 |

AXI-Lite `s00_axi_aresetn`、Processor System Reset 的 peripheral reset 和自定义 IP reset 极性仍为低有效。ADC reset deassertion 通过原三级寄存器同步；实现后自检确认三个同步寄存器的异步 PRE pin 均存在。

BD validation 通过。2026.1 仍报告两项接口元数据 Critical Warning：

- 自定义 `SPI_O_CLK` 输出接口缺少 `FREQ_HZ` bus parameter。
- ADC 自定义 AXIS `m_axis_adc` 未在 IP 元数据中关联 clock interface。

没有在不确定接口语义的情况下臆造频率或修改自定义数据通路，这两项留待后续 IP 元数据完善。

## 4. 官方 IP 升级

18 个锁定的独立官方 IP 已升级；最终工程共 29 个 IP，全部为 `Up-to-date` 或 `Up-to-date (discontinued)`。

| IP 类别 | 实例 | 2020.2 → 2026.1 |
|---|---|---|
| Block Memory Generator | `blk_mem_gen_0`、4 个 `tdpram_*` | Rev. 4 → Rev. 13 |
| Clocking Wizard | `clk_gen` | Rev. 6 → Rev. 19 |
| DSP48 Macro | 4 个 `dsp_*` | `xbip_dsp48_macro 3.0` → `DSP Macro 1.0 Rev. 10` |
| FIFO Generator | `fifo_32bx8192` | Rev. 5 → Rev. 15 |
| Multiplier | 4 个 `mult_*` | Rev. 16 → Rev. 25 |
| Processor System Reset | `proc_sys_reset_0` | Rev. 13 → Rev. 17 |
| CORDIC | `sqrt_44`、`square_33b_d17` | Rev. 16 → Rev. 26 |

升级前后共 4347 项 `CONFIG.*` 属性逐项完全一致，包含位宽、signedness、rounding、latency、BRAM 深度/read latency 和 Clock Wizard 配置。原 3 个 ILA XCI 完整保留；release 构建排除这些 XCI，并使用端口完全匹配的空逻辑 stub。

BD 内的两个 AXI Interconnect 和 `xlconstant` 在 2026.1 标记为 discontinued，但状态为 Up-to-date，未替换结构，以避免改变 AXI3/AXI4 协议转换和时序行为。

## 5. 自定义 IP

### `xilinx.com:user:afe5832_ctrl_ps:1.0`

- Vendor/Library/Name/Version 保持不变。
- AXI-Lite 数据宽度、接口和寄存器偏移 `0x00`–`0x24` 保持不变。
- 原 RTL、xgui Tcl、MDD、Makefile 和软件驱动保留。
- 新增 `data/afe5832_ctrl_ps.yaml` 和 `src/CMakeLists.txt`，并加入 `component.xml` software-driver file set。
- 新 XSA 包含 YAML、CMake 和驱动源；Vitis BSP 已成功构建 `libafe5832_ctrl_ps.a`。

### `xilinx.com:user:w_ddr:1.1`

- VLNV、AXI3 结构、32-bit 数据、256-beat INCR burst 和 `0x20000000` 目标地址保持不变。
- 修复了控制 CDC：状态机改用原有两级同步链的第二级 `wr_ddr_start_ff2`，不再直接采样异步 `wr_in_ddr_start`。
- 实现后自检确认第一同步级仍在 routed netlist 中。

## 6. RTL 与 XDC 修改

功能 RTL 只包含以下迁移相关改动：

- `build/src/imports/sources/proc_top.sv`
  - 显式声明旧代码的隐式 `scan_enable` wire。
  - 为 200 MHz → 25 MHz 的 `beamform_active` 两级同步寄存器增加 `ASYNC_REG`、`SHREG_EXTRACT=NO` 和 `DONT_TOUCH`，防止实现优化合并。
- `build/src/imports/.../afe5832_adc_14b_16lane.v`
  - 为原 `rst_200MCLK_reg`、`rst_clkdiv_reg` 三级复位同步链增加 `ASYNC_REG`。
- `ip_repo/xilinx.com_user_w_ddr_1.1/hdl/w_ddr_v1_0_M00_AXI.v`
  - DDR writer FSM 使用已同步的 start 信号。
- `build/src/imports/release/ila_release_stubs.v`
  - 新增 release ILA stub；不接入功能数据通路。
- `build/constraints/pin.xdc`
  - 删除 2026.1 中已失效的 5 组层次路径和 XDC 不支持的 Tcl `if`。
  - 只对已确认的同步器第一极和 ADC async reset PRE 添加精确 false path。

实现后四项自检结果：

```text
W_DDR_CDC_STAGE1       1 pin
SCAN_ENABLE_CDC_STAGE1 1 pin
BEAMFORM_ACTIVE_CDC_STAGE1 1 pin
ADC_RESET_SYNC_PRE     3 pins
```

除上述允许变更外，波束合成、后处理和其他 RTL 与旧受控源的 SHA-256 一致。AFE/TX 写入序列、UART/USB 协议常量、BRAM 表、RF/B-mode 数据格式和 PL 启停地址比较通过。

## 7. Vitis 2026.1 与 API 修改

从新 XSA 创建：

```text
Platform:    ubmi_p
Domain:      standalone_ps7_cortexa9_0
Application: B_mode_focus_imaging
FSBL:        zynq_fsbl
```

应用使用 Vitis 2026.1 新生成的链接脚本；旧 Platform/BSP/FSBL 未导入。`data_transfer/axidma.c` 作为旧源码参考保留，但因 BD 无 AXI DMA 且 `IRQ_F2P` 接常量 0，不参与编译。

主要 API 修改：

- `xtime_l.h` → `xiltimer.h`。
- GIC、GPIO、SPI、IIC、UART、TTC 和 USB 的 SDT `LookupConfig` 参数逐处改为实际 `XPAR_X*_BASEADDR`。
- `CfgInitialize` 继续使用各配置表的 `BaseAddr`/`BaseAddress`，未进行全局文本替换。
- AFE 和 BRAM 使用新生成的 `XPAR_XAFE5832_CTRL_PS_0_BASEADDR`、`XPAR_XBRAM_0_BASEADDR`。
- DDR 使用 `XPAR_PS7_DDR_0_BASEADDRESS`。
- 删除旧 `init_platform()` 依赖。
- AFE calibration interrupt 注册改为 no-op；不再使用不存在的硬编码 PL 62 号中断。
- 删除未映射 `0x43C10000` 控制路径和硬编码 PL 61/62 中断依赖。

实际 2026.1 `xparameters.h` 的 SPI interrupt 属性为编码值，必须同时使用 `XGet_IntrId()` 和 `XGet_IntrOffset()` 后传给 GIC：

| 外设 | SDT 属性 | 最终 GIC ID |
|---|---:|---:|
| GPIO | `0x4014` | 52 |
| TTC0 | `0x400A` | 42 |
| UART1 | `0x4032` | 82 |
| USB1 | `0x402C` | 76 |

这四处已按实际生成的 `xinterrupt_wrap.h` 修正。新 `xparameters.h` 中没有 `XPAR_FABRIC_*` 或 AXI DMA 宏。

Vitis Application 和 FSBL 最终均为 0 compile errors。日志包含 60 条 warning，主要来自旧 USB 源的 unused parameter、signedness、fallthrough 和 static declaration，以及自动生成 `ps7_init.c` 的 signedness；未为消除告警而修改协议或业务逻辑。

## 8. Vivado 实现结果

```text
Synthesis: 0 errors, 0 critical warnings, 242 warnings
Route:     WNS +0.502 ns, TNS 0.000 ns
           WHS +0.019 ns, THS 0.000 ns
DRC:       0 errors
```

旧 2020.2 基线为 WNS `-1.991 ns`、TNS `-4.190 ns`。新实现无需增加算法流水级即可收敛。

资源：

| 资源 | 使用 | 比例 |
|---|---:|---:|
| Slice LUT | 27,518 | 35.01% |
| Slice Register | 57,702 | 36.71% |
| BRAM Tile | 168.5 | 63.58% |
| DSP | 293 | 73.25% |
| BUFGCTRL | 7 | 21.88% |
| MMCM | 2 | 40.00% |

剩余 DRC warnings：

- DPOP-1 48、DPOP-2 2：建议增加 DSP 输出流水级；按要求未修改波束算法延迟。
- PDCN-1569 3、REQP-101 17、REQP-1839/1840 各 20、RTSTAT-10 1：旧结构/存储器和无负载网络告警。
- PLIO-3 1：旧 XDC 未给 `tx_sdout[1]` 分配 package pin；必须依据实际 PCB 原理图确认，未臆造 LOC。
- Bitgen 有 3 条 Memdata Critical Warning：Vivado Bitstream 未关联 ELF，因此未填充 processor BRAM init。应用是 BOOT.BIN 的独立 ELF 分区，不影响本次启动镜像组织。

## 9. 产物与哈希

| 文件 | SHA-256 |
|---|---|
| `heart_top.bit` | `79ea19858db58897054885cd4ed56517835aa902bf2c49eaede0526e09fa6748` |
| `ultrasound_B_mode_imaging.xsa` | `513e35a652a012310637c9af8c10f2d383eb6a7bc48a37b0cb7bb42f5c071c26` |
| `B_mode_focus_imaging.elf` | `d342c98979479b17b77f33503697c986780777dfb75cb08024e575a77bd968fe` |
| `zynq_fsbl.elf` | `89123546f759317cfe1a7378463fe179662e30f3786e18e292c402ea7e3bb077` |
| `BOOT.BIN` | `cf63f568a288de245bc8206976ccaf50c9c5e6421ae8602fad470264111aa4f4` |

XSA 归档中已确认包含 `ultrasound_B_mode_imaging.bit`、AFE SDT YAML/CMake 和驱动源。

Bootgen 读取结果确认顺序：

```text
[bootloader] zynq_fsbl.elf
             heart_top.bit
             B_mode_focus_imaging.elf
```

应用 ELF 的 entry/load address 为 `0x00100000`；BOOT.BIN 分区读取报告位于 `build/reports/bootgen_read.txt`。

## 10. 尚未验证

本报告不宣称已完成硬件上板运行验证。以下项目仍需在目标板验收：

- FSBL/QSPI 或 SD 启动和 UART 启动日志。
- AFE5832、TX7332 SPI 实际波形和寄存器回读。
- PL start/stop、完整帧边界停止行为和 CDC 稳定性。
- DDR 写入地址、RF 数据排列、B-mode 数据排列和缓存一致性。
- 波束合成、后处理及最终成像结果与 2020.2 基线逐帧/逐像素比较。
- USB/UART 主机协议互操作。
- 根据 PCB 原理图补齐或确认 `tx_sdout[1]` 的管脚约束。
- 自定义 IP 的 SPI clock/ADC AXIS clock interface 元数据。

## 11. 通道映射与延时增量移植

本次在 Vivado/Vitis 2026.1 工程中继续移植旧仓库
`a8dbaef3190d7107840b88696217efcecdb3397d..b0d10d56251da3a93220aa829b0eb9fbc2252e90`
的两个线性提交，未纳入其他分叉调试分支。

- 新增统一的 64 阵元映射表，以及 Python/MATLAB 映射、TX7332 打包和一致性检查工具。
- RX 入口按 AFE 相邻通道两两交换；重排为纯组合连线，后接原有 `clk_25` 输入寄存器，不增加流水延迟或 CDC。
- 保留 `beamform_active` 两级同步器属性、AFE 复位同步链、W_DDR 第二级同步信号和原精确 XDC false path。
- TX 模型采用 0.3 mm pitch 和 `6 us - (L-F)/c`，启动表为 16384 words；扫描时序和 TX7332 寄存器 0x18 更新到旧仓库最终值。
- PC/GUI JSON 按确认要求逐字保留旧仓库受控版本。核查发现该 JSON 不等于同提交 MATLAB 当前公式重新生成的 5 us C 表，因此通过 SHA-256 锁定旧文件，不对其做语义重生成。

验证结果：

```text
RX XSim self-check: PASS
Python channel/TX/profile self-check: PASS
Vivado synthesis/implementation: PASS
Route: WNS +0.681 ns, TNS 0.000 ns
       WHS +0.028 ns, THS 0.000 ns
DRC: 0 errors
CDC routed-netlist assertions: W_DDR=1, scan_enable=1,
                               beamform_active=1, ADC reset PRE=3
Vitis Platform/Application/FSBL: PASS
Bootgen BOOT.BIN: PASS
```

实现后资源保持为 Slice LUT 35.01%、Slice Register 36.71%、BRAM Tile 63.58%、DSP 73.25%，均低于 80%。
最终产物哈希记录在 `build/reports/artifact_manifest.sha256`。当前环境未安装 MATLAB/Octave，因而未直接执行 `.m` 自检；已使用独立 Python 数学模型逐 word 验证 6 us 启动表，并保留 MATLAB self-check 供安装 MATLAB 的环境复验。
