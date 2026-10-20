# 从 release/ubmi-pcb-v2 到当前版本的 PL / PS 修改清单

## 1. 比较范围与结论

- 整理日期：2026-10-18。
- 基准：`release/ubmi-pcb-v2`，提交 `094b976c85f6bfe2ea7584d729ce7811bcaf4804`。
- 当前：`hardware/ubmi-pcb-v2`，提交 `562222ad2f195d1bf527411722dc0f1fffd0ca3d`。
- 本文依据 Git 基准到当前提交的净差异，并核对关键函数和 BD 参数。中间实施后撤回的内容另列，不能当作当前功能。
- 清单包含构建规范源、Vivado 管理副本、当前 Vitis 应用源及构建脚本；ESP32/PC 应用不属于 Zynq PL/PS，本表只说明其接口关系。
- 文件附录共 149 项：新增 59、修改 38、删除 52。同一功能的两份源码分别计数，不能理解为 149 个功能模块。
- 本次为只读审计并新增本文档，没有修改功能代码、重新构建、下载或烧录。

**核心结论：当前相对基准，成像算法、AFE LVDS 接收、AFE SPI 状态机、扫描 FSM 和 DDR 写入器的自定义功能 RTL 没有净修改。硬件变化集中在 PS7 外设映射和恢复真实 BF ILA；PS 软件增加 SDIO、UART/Wi-Fi 控制、温度及诊断，并清理了旧驱动。**

## 2. PL / 硬件工程修改

| 项目 | 相对基准的修改 | 当前影响 |
|---|---|---|
| SD0 | 基准 BD 中禁用，当前明确启用，映射 MIO40–45；禁用卡检测、写保护、供电控制引脚 | 保留 SD 卡启动所需接口；不是“基准中已经启用且完全没改” |
| SD1 | 启用 SD1，映射 MIO10–15，禁用卡检测、写保护、供电控制引脚 | 为 Zynq 到 ESP32 的 SDIO 主机链路提供 PS 外设 |
| SDIO 参考时钟 | BD 的 SDIO 外设参考时钟由 10 MHz 改为 100 MHz | 不代表线上运行 100 MHz；驱动枚举 400 kHz、正常默认 25 MHz |
| UART0 | 启用 MIO26/RX、MIO27/TX，115200、无流控 | 日志/温度上传及 ESP32 下发控制；UART1 的 MIO16/17 保留 |
| SPI1 与引脚复用 | 保留 SPI1；SS1/SS2 保持禁用，避免与 MIO26/27 冲突 | 未将 AFE 的 PL SPI 控制器换成 PS SPI1 |
| 真实 BF ILA | 移除空的 `ila_bf` 替身，启用真实 IP，恢复对应综合/OOC run 元数据 | Hardware Manager 可识别真实 BF ILA；不等于采样时钟必然运行 |
| 其他 ILA | `ila_tx`、`ila_timing` 仍为空替身；实现前脚本只清理两个已核实的输入型黑盒实例 | 没有将其他 ILA 全部恢复 |
| Debug Hub | 实现前脚本将调试 Hub 接到独立 100 MHz 时钟 | 不改变 BF ILA 本身的 `data_clk` 采样时钟 |
| XDC | `pin.xdc` 的基准到当前差异为调试相关注释 | 没有改变 AFE 物理引脚、外部时钟周期或新增功能时序例外 |
| BD/IP 元数据 | Vivado 2026.1 更新 IP 元数据、单 bit 端口表示、局部实例标识及工程引用 | 大量 BD 文本差异不是同等规模的数据通路改造；AFE 时钟/复位端点未改接 |

### 2.1 BF ILA 的最终探针没有更改

`heart_top.sv` 中原来的 `ila_bf_i` 实例保持原连接，采样时钟为 `data_clk`：

| 探针 | 原连接 | 位宽 |
|---|---|---:|
| probe0 | `{proc_en, proc_in[15:0]}` | 17 |
| probe1 | `{rtc_rxen, proc_top_i.rtc_bfen}` | 2 |
| probe2 | `{up_dataen, up_data}` | 17 |
| probe3 | `proc_top_i.rx_foc_delta_rddata` | 32 |
| probe4 | `rtc_line` | 7 |

采样深度 8192，输入流水级 0。此前讨论/试加的扩展探针没有保留到当前版本。

### 2.2 当前没有修改的自定义功能 RTL

- AFE 控制及 SPI：`afe5832_ctrl_module.v`、`afe5832_spi_r_w_module.v`、AFE AXI 自定义 IP 的 HDL。
- AFE 接收：`afe5832_adc_14b_16lane.v`、`iserdese14_ddr.v`；DCLK 引脚、解串及其 MMCM 逻辑。
- 波束合成、解调、滤波、包络、对数压缩和扫描控制功能 RTL。
- DDR AXI 写入器及原来的重复覆盖行为。
- 未增加完整帧冻结、DDR 乒乓所有权、帧完成硬件标志或待机控制寄存器。

“没有净修改”是 Git 源码结论，不表示当前板卡加载的 bit 与基准一致，也不表示现有 CDC 或时序风险已解决。

## 3. PS 软件修改

| 类别 | 修改内容 | 主要文件 |
|---|---|---|
| SDIO 主机驱动 | 新增 CMD52/CMD53、枚举、块传输和错误处理；按 SD1 基地址 `0xE0101000` 选控制器，避免误选 SD0 | `data_transfer/sdio_host.*`、`sdio_cmd_engine.*` |
| SDIO 帧桥接 | 新增版本、会话、帧号、长度、CRC32、BEGIN/ABORT、窗口额度和确认；512 B/块、最多 16 块/窗口 | `sdio_bridge.*`、`bridge_protocol.h` |
| SDIO 稳定性 | 恢复轮询事件使能，改为 SDIO 专用 DMA 设置；失败后有限等待复位 CMD/DAT，增加诊断、背压和会话检查；减少重复 CMD52 轮询 | 上述 SDIO 文件 |
| USB/SDIO 选择 | 初期曾支持串口切换，后按要求改为 C 宏；当前默认 `DATA_OUTPUT_SDIO`，仍可编译 USB 模式 | `output_config.h`、`main.c` |
| 真实 DDR 数据 | 初期发送独立固定测试缓冲区，当前改为 PL 成像 DDR 固定片段；不再发送合成测试帧 | `sdio_bridge.c` |
| UART0 日志 | 启动早期初始化 UART0 115200/8N1，将 `xil_printf` 等的底层字符输出重定向；修复 Vitis/BSP 链接冲突 | `uart/uart_log.*`、`outbyte.c` |
| UART1 兼容 | 使用 UART1 基地址 `0xE0001000` 查配置及其中断，避免新增 UART0 后实例编号变化 | `uart/uart.c` |
| Wi-Fi 控制接入 | UART0 RX 有界队列；前台解析 `@WFC1`，通过 `@WFR1` 回执；CRC、会话/请求号、重复结果缓存和迟到请求处理 | `uart/uart.c`、`common/wifi_control_protocol.h` |
| 配置事务保护 | 区分 USB/UART1/Wi-Fi 入口及 Wi-Fi 客户端；Wi-Fi CFG BEGIN 关高压、停止扫描；CFG END 不自动启动；失败/ABORT/60 秒无有效命令时安全退出事务 | `uart/uart.c` 及 USB 接入 |
| 命令校验 | 复用既有 AFE、TX7332、BRAM、扫描、高压、增益命令，新增 STATUS；Wi-Fi 严格校验参数并保护 6144 点布局 | `uart/uart.c` |
| 扫描请求状态 | 不再通过写专用 BRAM 字读回推断 run；增加 PS run/spi 请求缓存，STATUS 标明 `run_src=ps` | `bram/bram.*`、`uart/uart.c` |
| AFE SPI 有限等待 | 无限轮询改为 20 ms/轮询上限保护，加入空指针检查；读取函数返回操作状态，数据经指针返回 | `AFE5832/afe5832_spi.c` |
| AFE 寄存器错误传播 | 读写寄存器映射时检查事务和映射恢复错误，不再无条件成功 | `AFE5832/afe5832.c` |
| 三路温度 | PS XADC 芯片温度、IIC0 0x48 电源温度、0x49 AFE 区域温度；首次及每 10 秒采集，独立失败状态、CRC UART 报告 | `system_ctrl/temp.*`、`temp_hw.*`、`common/temperature_protocol.h` |
| 温度正确性/限时 | 外部 12 bit 有符号左对齐、0.0625°C 解码；失败不冒充 0°C；IIC/XADC 有限等待，报告发送有限等待 | 温度模块 |
| 前台服务 | 主循环、USB 等待和长发送循环加入命令/温度服务机会，避免等待数据时永远不处理控制；保留 USB 数据入口 | `main.c`、`usb/usb.c` |
| 初始化简化 | 删除旧时钟器件配置，移除 AFE 外层包装函数，在 main 中直接初始化 AFE，保留合计约 2 秒稳定等待 | `main.c` |

### 3.1 当前 SDIO 实际读取区域

- 起点：`0x20000000`。
- 每次长度：`787456` B（`0xC0400`）。
- 本次读取末地址：`0x200C03FF`，含末字节。
- 原 DDR 环形区域总长度：`1024000` B。
- 每次接收新帧请求后进行相应 cache invalidation，并重新计算所读片段的 CRC。
- 不清空 DDR、不等待硬件帧完成、不阻止 PL 覆盖、不自动启动扫描或高压。
- PS→ESP 接收完成与 PC 校验完成是不同阶段；网络 CRC 正确也不能证明所有图像线来自同一次采集。

### 3.2 AFE 初始化究竟改了什么

**构建规范源的 `AFE5832_init()` 初始化寄存器表与基准保持一致。** 当前净修改主要在 SPI 等待/返回状态和上层错误传播，不是改 AFE 采样/串化配置表。

当前 Vitis 应用副本另有已提交的调试编辑：

- 将寄存器 `0x0003` 读回打印移到 14X 串化配置之后。
- 日志明确标出 `0x0003`、`0x00C7` 地址。
- 当前仍使用 `PAT_MODE_NORMAL`，没有保留临时同步测试模式。

读回寄存器正常只能证明 SPI 控制链路工作，不能据此证明 ADC_CLK、DCLK/FCLK 或 FPGA 接收采样时钟正常。

### 3.3 删除的旧时钟初始化与 AFE SPI 不是同一个接口

删除的 `hw_spi_init()` 配置的是 **PS SPI1 / 0xE0007000**，用于旧 AD9512；删除的 `sys_clk_init()` 配置旧 LMK61E2/AD9512 及其相关控制引脚。

AFE 使用 **PL AFE 控制器 / 0x43C00000**，驱动和 RTL 仍保留。新板已确认采用硬件配置 AD9508，直接产生 AFE/TX/FPGA 时钟。因此，删除旧时钟软件配置不等于删除 AFE SPI 初始化；实际时钟是否正常仍需独立验证。

### 3.4 扫描/高压命令的边界

- AFE RD/WR、TX7332 RD/WR、BRAM WR、BEAM START/STOP、HV ENABLE/DISABLE、GAIN SET 的底层命令并非全部新写；基准已有解析/执行能力，本次主要增加 Wi-Fi 入口与保护。
- STATUS 的 run 是 **PS 发出的扫描请求状态**，hv 是 GPIO 状态；不是实际发射、物理高压、电路安全排空或 DDR 更新的硬件证明。
- Wi-Fi CFG END 与旧 USB/UART1 CFG END 的行为刻意区分，后者保留原兼容路径。
- 配置事务超时保护不等于一般运行状态下“断网必然自动关闭高压”的全局看门狗。
- 没有将网页上传按钮改成扫描启动按钮。

## 4. PS 源码清理与搬移

| 删除/搬移项 | 处理结果 |
|---|---|
| `CLOCK/AD9512.*`、`CLOCK/system_clk.*` | 删除旧软件时钟配置及调用 |
| `TX7332/tx7332.*`、`tx7332_spi.*` | 删除未使用的旧 PS 驱动；保留通过 BRAM/PL 执行的 TX 配置路径 |
| `data_process/compress.*` | 删除未使用的 PS 压缩代码，不是删除 PL 对数压缩 |
| `data_transfer/axidma.*`、`data_recv.*` | 删除未使用的 PS 驱动；不是删除 PL DDR AXI master |
| `eeprom/`、`key/`、`linux_wifi/`、`timer/` | 删除已确认不用的模块及构建引用 |
| `pus_operation/pus_oper.*` | 删除初始化包装层，直接调用 AFE5832_init |
| `tmp/tmp.*` | 先移到 system_ctrl，再重命名 temp；温度功能保留并扩展 |

规范源和 Vitis 源中对应文件均处理，构建列表/头文件引用同步调整。最初清理按用户要求保留温度模块，并非把所有旧文件不加区分删除。

## 5. 构建、工程及诊断脚本

- Vivado 创建、修复、检查流程同步 SD0/SD1/UART0 映射，避免重新生成后关闭接口。
- Vitis 2026.1/SDT 驱动适配：外设基地址查找、UART 中断配置、BSP stdout UART0、原 UART1 输入及兼容头处理。
- `outbyte` 改为链接包装 `__wrap_outbyte` / `-Wl,--wrap=outbyte`，避免与 BSP 的强符号重定义冲突。
- `UserConfig.cmake` 增加 SDIO、日志、温度源及 common 头文件目录，删除旧模块引用。
- 当前工作区 `ps7_init.tcl` 更新 MIO、UART/SDIO 时钟/外设初始化；未观察到 DDR 初始化时序参数净改变。
- BOOT 构建输入统一使用 `build/output/ultrasound_B_mode_imaging.bit`，并记录打包输入与哈希；bit/XSA/LTX 配套导出。
- 新增 SDIO 构建/检查、Vitis 工作区修复、BF ILA 准备/验证、Vivado 启动配置修复及自检查脚本。
- Vivado 写入 `C` 问题对应启动/工程配置修复，不是改乘法器运算逻辑；旧 GUI 回写导致 ILA run 元数据丢失的恢复也已加入。
- 新增扫描读回、DDR CRC 快照、ILA 清单、SDIO JTAG 诊断脚本；脚本存在不代表本次审计执行过板上操作。
- 构建输出清理、归档及 Git 忽略调整属于工程管理，不是成像 RTL 修改。

本次没有重跑综合/实现或 CDC。已有异步采样探针和其他 CDC 风险不能因增加 ILA 或以往单次构建成功就标记为已消除。

## 6. 当前两份 PS 源码并非完全一致

对比 `build/src/ps`（构建规范源）与 `workspace/B_mode_focus_imaging/src`（当前 Vitis 源）：

| 文件 | 当前 Vitis 副本的额外差异 |
|---|---|
| `main.h` | LED1=54、LED2=55；规范源为 LED1=55、LED2=54；Vitis 启用 USE_DEBUG_PRINT，规范源未启用 |
| `AFE5832/afe5832.c` | 上述 0x0003 读回位置、寄存器日志文字及空行差异 |
| `main.c` | 注释清理及删除无实际 CPU1 启动作用的旧调试文字；不能据此声称关闭了 CPU1 |

这些现状已经包含在当前提交中；不能把整个快照都归因为同一次功能开发。重新从规范源生成工作区时，Vitis 副本独有编辑存在被覆盖风险。本次仅列明，未擅自同步覆盖。

## 7. 实施过但不在当前分支的待机修改

独立保存在：

- 分支：`wip/safe-standby-uart-wake-20261015`。
- 保存提交：`c4dda8e`。
- 当前分支按用户要求回到 `507daffc` 的成像基线后继续开发，没有合入该待机实现。

该分支保留过的修改范围包括：

- PS 电源状态机、UART 待机/唤醒控制、AFE 掉电/恢复、TX 待机、温度暂停、CPU0 WFI/CPU1 禁用相关代码。
- PL quiesce/排空控制、TX 与扫描控制、处理流水线及 DDR flush/AXI 排空逻辑。
- AFE 时钟诊断/恢复、接收复位及跨域握手相关改动。
- 涉及 `power_hw_ctrl.sv`、`afe_dclk_probe.sv`、`afe_mmcm_recovery.sv`、`scan_sync.v`、处理链 RTL、DDR 自定义 IP 和 PS power_manager 等文件。

**这些不能算作“当前版本仍然存在的修改”。当时唤醒/时钟恢复未通过板级验收，不能视为已验证的低功耗交付。当前也不能宣称 CPU1 一直处于该待机分支所实现的关闭状态。**

## 8. 关键提交索引

| 提交 | 内容 |
|---|---|
| `4a4872e` | 固定 DDR 测试帧 SDIO → ESP32 带头 UDP 桥接 |
| `5933503` | USB/SDIO 编译期宏选择，统一 BOOT bit 输入 |
| `9500712` | UART0 日志转发所需 Zynq 修改 |
| `719f5dc` | Vitis IDE/BSP UART 日志链接修复 |
| `e892657` | SDIO 轮询事件与专用 DMA 初始化修复 |
| `47aae37` | 减少 SDIO 重复轮询 |
| `7ea9876` | SDIO 进度/等待诊断（TCP 主要优化在 ESP32） |
| `a08b4b0` | 删除确认未使用的 PS 模块，保留温度 |
| `0e0df67` | 移除旧软件时钟芯片配置 |
| `5c7e22b`、`3d7685f` | 温度模块移动和重命名 |
| `f7e5d5c` | 移除 AFE 初始化包装层 |
| `507daff` | 三路温度采集与状态上报 |
| `8210e74` | 真实 DDR 数据、Wi-Fi 命令控制及相关 AFE 限时/错误处理 |
| `4ee0633` | 扫描请求状态不再读写专用 PL 字 |
| `9cee0b9` | 恢复真实 BF ILA，保留原探针 |
| `43804de` | Vivado 启动配置及 ILA run 元数据修复 |
| `562222a` | 保存当前 Vitis 调试编辑与 Vivado 工程状态 |

ESP32 TCP/UDP 宏、socket 缓冲、PC TXT/网页解包等变化发生在另一侧，不应混写成 Zynq PS TCP 协议栈或 PL 算法改造。

## 9. 文件级完整净差异

以下按 Git `--no-renames` 输出，A=新增、M=修改、D=删除；搬移/重命名因此可能表现为一删一增。路径相对成像工程根目录，不包含生成 bit/ELF 等忽略产物，也不将 ESP32/PC 应用文件算作 Zynq PL/PS 文件。

### 9.1 PL、BD、Vivado 管理副本

```text
M build/constraints/pin.xdc
M build/src/bd/pus_ctrl_system.bd
M build/src/imports/release/ila_release_stubs.v
M build/src/ip/ila_bf/ila_bf.xci
M vivado/ultrasound_B_mode_imaging.srcs/constrs_1/imports/constraints/pin.xdc
M vivado/ultrasound_B_mode_imaging.srcs/sources_1/bd/bd/ip/pus_ctrl_system_axi_bram_ctrl_0_0/pus_ctrl_system_axi_bram_ctrl_0_0.xci
M vivado/ultrasound_B_mode_imaging.srcs/sources_1/bd/bd/ip/pus_ctrl_system_processing_system7_0_0/pus_ctrl_system_processing_system7_0_0.xci
M vivado/ultrasound_B_mode_imaging.srcs/sources_1/bd/bd/pus_ctrl_system.bd
M vivado/ultrasound_B_mode_imaging.srcs/sources_1/imports/imports/release/ila_release_stubs.v
A vivado/ultrasound_B_mode_imaging.srcs/sources_1/ip/ila_bf/ila_bf.xci
M vivado/ultrasound_B_mode_imaging.xpr
```

### 9.2 PS 构建规范源

```text
M build/src/ps/AFE5832/afe5832.c
M build/src/ps/AFE5832/afe5832_spi.c
D build/src/ps/CLOCK/AD9512.c
D build/src/ps/CLOCK/AD9512.h
D build/src/ps/CLOCK/system_clk.c
D build/src/ps/CLOCK/system_clk.h
D build/src/ps/TX7332/tx7332.c
D build/src/ps/TX7332/tx7332.h
D build/src/ps/TX7332/tx7332_spi.c
D build/src/ps/TX7332/tx7332_spi.h
M build/src/ps/bram/bram.c
M build/src/ps/bram/bram.h
D build/src/ps/data_process/compress.c
D build/src/ps/data_process/compress.h
A build/src/ps/data_transfer/SDIO_LIVE_DDR.md
D build/src/ps/data_transfer/axidma.c
D build/src/ps/data_transfer/axidma.h
A build/src/ps/data_transfer/bridge_protocol.h
D build/src/ps/data_transfer/data_recv.c
D build/src/ps/data_transfer/data_recv.h
A build/src/ps/data_transfer/output_config.h
A build/src/ps/data_transfer/sdio_bridge.c
A build/src/ps/data_transfer/sdio_bridge.h
A build/src/ps/data_transfer/sdio_cmd_engine.c
A build/src/ps/data_transfer/sdio_cmd_engine.h
A build/src/ps/data_transfer/sdio_host.c
A build/src/ps/data_transfer/sdio_host.h
D build/src/ps/eeprom/eeprom.c
D build/src/ps/eeprom/eeprom.h
D build/src/ps/key/key.c
D build/src/ps/key/key.h
D build/src/ps/linux_wifi/linux_wifi.c
D build/src/ps/linux_wifi/linux_wifi.h
M build/src/ps/main.c
A build/src/ps/outbyte.c
D build/src/ps/pus_operation/pus_oper.c
D build/src/ps/pus_operation/pus_oper.h
A build/src/ps/system_ctrl/temp.c
A build/src/ps/system_ctrl/temp.h
A build/src/ps/system_ctrl/temp_hw.c
A build/src/ps/system_ctrl/temp_hw.h
D build/src/ps/timer/timer.c
D build/src/ps/timer/timer.h
D build/src/ps/tmp/tmp.c
D build/src/ps/tmp/tmp.h
M build/src/ps/uart/uart.c
M build/src/ps/uart/uart.h
A build/src/ps/uart/uart_log.c
A build/src/ps/uart/uart_log.h
M build/src/ps/usb/usb.c
```

### 9.3 当前 Vitis 应用与初始化

```text
M workspace/B_mode_focus_imaging/_ide/psinit/ps7_init.tcl
M workspace/B_mode_focus_imaging/src/AFE5832/afe5832.c
M workspace/B_mode_focus_imaging/src/AFE5832/afe5832_spi.c
D workspace/B_mode_focus_imaging/src/CLOCK/AD9512.c
D workspace/B_mode_focus_imaging/src/CLOCK/AD9512.h
D workspace/B_mode_focus_imaging/src/CLOCK/system_clk.c
D workspace/B_mode_focus_imaging/src/CLOCK/system_clk.h
D workspace/B_mode_focus_imaging/src/TX7332/tx7332.c
D workspace/B_mode_focus_imaging/src/TX7332/tx7332.h
D workspace/B_mode_focus_imaging/src/TX7332/tx7332_spi.c
D workspace/B_mode_focus_imaging/src/TX7332/tx7332_spi.h
M workspace/B_mode_focus_imaging/src/UserConfig.cmake
M workspace/B_mode_focus_imaging/src/bram/bram.c
M workspace/B_mode_focus_imaging/src/bram/bram.h
D workspace/B_mode_focus_imaging/src/data_process/compress.c
D workspace/B_mode_focus_imaging/src/data_process/compress.h
D workspace/B_mode_focus_imaging/src/data_transfer/axidma.c
D workspace/B_mode_focus_imaging/src/data_transfer/axidma.h
A workspace/B_mode_focus_imaging/src/data_transfer/bridge_protocol.h
D workspace/B_mode_focus_imaging/src/data_transfer/data_recv.c
D workspace/B_mode_focus_imaging/src/data_transfer/data_recv.h
A workspace/B_mode_focus_imaging/src/data_transfer/output_config.h
A workspace/B_mode_focus_imaging/src/data_transfer/sdio_bridge.c
A workspace/B_mode_focus_imaging/src/data_transfer/sdio_bridge.h
A workspace/B_mode_focus_imaging/src/data_transfer/sdio_cmd_engine.c
A workspace/B_mode_focus_imaging/src/data_transfer/sdio_cmd_engine.h
A workspace/B_mode_focus_imaging/src/data_transfer/sdio_host.c
A workspace/B_mode_focus_imaging/src/data_transfer/sdio_host.h
D workspace/B_mode_focus_imaging/src/eeprom/eeprom.c
D workspace/B_mode_focus_imaging/src/eeprom/eeprom.h
D workspace/B_mode_focus_imaging/src/key/key.c
D workspace/B_mode_focus_imaging/src/key/key.h
D workspace/B_mode_focus_imaging/src/linux_wifi/linux_wifi.c
D workspace/B_mode_focus_imaging/src/linux_wifi/linux_wifi.h
M workspace/B_mode_focus_imaging/src/main.c
M workspace/B_mode_focus_imaging/src/main.h
A workspace/B_mode_focus_imaging/src/outbyte.c
D workspace/B_mode_focus_imaging/src/pus_operation/pus_oper.c
D workspace/B_mode_focus_imaging/src/pus_operation/pus_oper.h
A workspace/B_mode_focus_imaging/src/system_ctrl/temp.c
A workspace/B_mode_focus_imaging/src/system_ctrl/temp.h
A workspace/B_mode_focus_imaging/src/system_ctrl/temp_hw.c
A workspace/B_mode_focus_imaging/src/system_ctrl/temp_hw.h
D workspace/B_mode_focus_imaging/src/timer/timer.c
D workspace/B_mode_focus_imaging/src/timer/timer.h
D workspace/B_mode_focus_imaging/src/tmp/tmp.c
D workspace/B_mode_focus_imaging/src/tmp/tmp.h
M workspace/B_mode_focus_imaging/src/uart/uart.c
M workspace/B_mode_focus_imaging/src/uart/uart.h
A workspace/B_mode_focus_imaging/src/uart/uart_log.c
A workspace/B_mode_focus_imaging/src/uart/uart_log.h
M workspace/B_mode_focus_imaging/src/usb/usb.c
```

### 9.4 共用协议

```text
A common/temperature_protocol.h
A common/wifi_control_protocol.h
```

### 9.5 构建、修复及自检查脚本

```text
M build/scripts/Vivado_init_2026_1.tcl
A build/scripts/bf_ila_impl_pre.tcl
M build/scripts/build_current_project_2026_1.tcl
A build/scripts/build_sdio.ps1
A build/scripts/build_sdio_current.tcl
M build/scripts/build_vitis_2026_1.py
M build/scripts/check_migration.ps1
A build/scripts/check_sdio_build.ps1
A build/scripts/check_sdio_implementation.tcl
A build/scripts/cleanup_build_outputs.ps1
A build/scripts/configure_bf_ila.tcl
A build/scripts/configure_sdio.tcl
M build/scripts/create_boot_image.ps1
M build/scripts/create_vivado_2026_1.tcl
M build/scripts/fix_ps7_peripheral_io_2026_1.tcl
A build/scripts/prepare_bf_ila.tcl
M build/scripts/repair_current_project_2026_1.tcl
A build/scripts/repair_vitis_workspace.ps1
A build/scripts/repair_vitis_workspace.py
A build/scripts/repair_vivado_startup.ps1
M build/scripts/run_vitis_2026_1.ps1
A build/scripts/scan_ddr_crc_snapshot.tcl
A build/scripts/scan_ila_inventory.tcl
A build/scripts/scan_readback_snapshot.tcl
A build/scripts/sdio_debug_download.tcl
A build/scripts/sdio_debug_runtime.tcl
A build/scripts/sdio_debug_snapshot.tcl
A build/scripts/sdio_debug_targets.tcl
A build/scripts/tests/test_bf_ila.py
A build/scripts/tests/test_vitis_source_cleanup.py
A build/scripts/tests/test_vivado_startup.py
A build/scripts/uart_bsp_compat.py
A build/scripts/verify_bf_ila.tcl
A build/scripts/verify_vivado_startup.tcl
```


