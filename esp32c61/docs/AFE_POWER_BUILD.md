# AFE 独立电源控制：离线验证记录（2026-10-22）

基线：`4d8c78c`；工作分支：`feature/afe5832-power-control`。本次不连接板卡、不执行下载/烧录、不覆盖 BOOT.BIN；不能把离线结果视为实际待机/唤醒已通过。

## 软件与仿真

- 完整主机回归：209 项 Python 测试中 208 通过、1 项因历史采集文件缺失跳过；网页 27 项、PC 图像页 5 项通过。
- AFE C 状态机：代码行覆盖 100%，已取到的分支方向覆盖超过 90%；覆盖错误代次、回绕、GPIO/停止失败、排空失败、SPI 故障、取消/超时及明确重试。
- AFE RTL 自检查：停钟/恢复、假旧锁定、恒定 FCLK、失锁、恢复代次、缺钟时 PL 复位及 Gray 单位变化断言通过。未执行门级仿真或模拟电路验证。
- DDR/AXI 自检查：五档长度、短突发、AXI 背压、槽位租约、FIFO 溢出及复位恢复通过。
- UART/控制服务：验证睡眠提交前同时等待 ESP32 槽位及 PS DMA/租约，重复请求不重放，失败不会发送后续睡眠提交；安全停止始终可用。
- 曾在并行构建负载下遇到既有 UDP 网页测试超时，独立重跑和随后全套回归通过；未修改 UDP 接收/图像协议以规避测试。

日志位于工程根目录 `build/afe_power_*.log`、`build/afe_final_*.log`，均为忽略的构建输出。

## 构建矩阵

Vitis 2026.1 USB/SDIO 两种宏模式已编译并通过链接范围检查；应用结束地址分别为 `0x0017CB70`、`0x0017CF90`，不占用 USB 缓冲区 `0x1C000000` 或三个 DDR 槽位 `0x20000000～0x202FFFFF`。

ESP-IDF 6.1 验证 TCP/UDP × 默认/性能四种配置，并单独更新日常 `build.ps1` 的 combined/TCP 默认输出。未使用 flash/monitor 动作。运行时堆低水位、实际吞吐及温度漂移需板测。

| ESP32 构建 | 应用字节数 | 应用分区空余 |
|---|---:|---:|
| 默认 / TCP | `0xEE600` | 36% |
| 默认 / UDP | `0xEE5A0` | 36% |
| 性能 combined / TCP | `0xEE610` | 36% |
| 性能 combined / UDP | `0xEE5B0` | 36% |

日常不指定参数的构建仍为 combined/TCP，使用 `esp32c61/build-throughput-fast-socket-exp-combined/`。`build-throughput-tcp-fast-socket-exp-combined/` 为显式指定 TCP 的独立验证目录，两者均已构建。

## Vivado、CDC 与产物

最终完整综合、实现及 bit/XSA 生成成功。实现报告 setup WNS **0.728 ns**、hold WHS **0.017 ns**，TNS/THS 为 0，内部无未定义时钟和未约束端点；WNS 达到本工程 0.5 ns 目标。既有部分 I/O 延时约束缺失仍存在，不据此宣称所有外部接口时序已签核。

| 资源 | 最终使用 | 使用率 | 相对备份报告 |
|---|---:|---:|---:|
| LUT | 31219 | 39.72% | +156 |
| FF | 62778 | 39.94% | +219 |
| BRAM | 188.5 | 71.13% | 不变 |
| DSP | 293 | 73.25% | 不变 |
| BUFG | 9 | 28.13% | +1，原始 DCLK 活性计数 |

新增 Gray 总线约束 2.5 ns，布线偏斜 0.585 ns、偏斜余量 1.915 ns。八个发起寄存器、八个捕获寄存器及原始发起时钟均保留；不能以 CDC 详细表中没有显示安全路径为验证依据。代次保持总线使用 10 ns max-delay 和请求/确认握手。

`ila_bf_i` 的原始五组探针宽度 `17/2/17/32/7` 通过实现网表检查，无额外探针、无未解析的非原语黑盒；未改 ADC/MMCM 的既有频率配置。

AFE CDC 延时约束必须使用寄存器 C 引脚或实例作为起点，不能用 Q 引脚导致路径分段。依据 [AMD UG903 路径分段说明](https://docs.amd.com/r/en-US/ug903-vivado-using-constraints/Path-Segmentation)，最终构建保留发起时钟信息，再检查 CDC，不把被分段后缺失的 CDC 报告当成安全证明。

原有备份报告有 54 项 CDC 关键问题；本次默认原始报告有 55 项。两处 AFE 全局/局部复位组合结构被报 CDC-10：`hold_sync[0]/PRE` 与 `rst_clkdiv_reg[0]/PRE`。它们只用于异步置位，分别通过两级、三级 ASYNC_REG 链同步释放；配套停钟/恢复仿真通过。`build/scripts/afe_power_cdc_waivers.tcl` 仅对这两个精确的源/目的端点设置复位结构豁免，不豁免数据通路或整个时钟域。原始报告与豁免报告分别保存，既有 ILA/成像数据路径 CDC 风险不据计数变化视为已解决，整个设计不能标为 CDC clean。

独立、无旧设计例外的 AFE 模块结构综合检查补充列出了所有数据总线：64 项 CDC-15 分别对应两个 32 位保持数据握手，1 项 CDC-6 对应 8 位 Gray 两级同步器。握手的请求/确认和保持条件经源码与仿真审查，并结合实际布线 10 ns 限定；Gray 有单位变化断言及实际布线 2.5 ns/偏斜限定。无数据路径豁免，仅对同一复位组合结构的精确端点豁免后，独立 AFE 报告无未豁免 Critical。该结构报告使用模块端口独立时钟，不是板级时序签核。

Vivado 会在 CDC 报告中略去部分 pin-based max-delay 数据路径，因此采用“交付检查点原始约束时序 + 独立模块无例外结构 CDC”两份互补证据，不用空的 CDC 详细表冒充通过。原始/审查/结构报告均在 `build/reports/afe_power_*.rpt`。ADC AXIS 时钟关联元数据缺失提示和原有大范围旧 CDC 时序例外仍需后续单独治理。

独立审查命令（仅离线，不打开硬件目标）：

```powershell
& 'D:/AMDDesignTools/2026.1/Vivado/bin/vivado.bat' -mode batch -source .\build\scripts\verify_afe_power.tcl
& 'D:/AMDDesignTools/2026.1/Vivado/bin/vivado.bat' -mode batch -source .\build\scripts\verify_afe_power_structure.tcl
```

## 配套产物与 SHA256

以下均为绝对路径；默认 SDIO。USB 宏模式的 ELF 单独列出，不能把它当作 Wi-Fi 图像输出应用。

| 产物 | 路径 |
|---|---|
| bit | `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/ultrasound_B_mode_imaging.bit` |
| XSA | `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/ultrasound_B_mode_imaging.xsa` |
| LTX | `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/ultrasound_B_mode_imaging.ltx` |
| SDIO 应用 ELF | `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/B_mode_focus_imaging.elf` |
| USB 应用 ELF | `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/vitis_frame_store_usb/B_mode_focus_imaging.elf` |
| ESP32 combined/TCP 应用 | `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/esp32c61/build-throughput-fast-socket-exp-combined/ultrasound_psram_udp.bin` |

```text
bit     FFB29BDBB04E3D14E4007534AEE3C6681A62A9B9AE19541CA6031F3F5A7C387A
XSA     A9812830FFA4D7155DF992BD5D64D846DDFCA07650127A6611721B83D3B025FB
LTX     14C3551092C8CE1BC4DE3249568A9235D6020986ED08B74237C43DB9331FB559
SDIO    A32CB420F67901A7DE6E70A57101165761F250250D78A2475C1502C70F66A6C5
USB     8E76D79A3E63442EA7FA7B2CA3978C78705F9E93FC3AEA1C1F8B87926BE30EBC
ESP32   30B44A3192AB45409BA7D20C8F1AF57D52A73AD725E30B925FBD4D81E37A857A
```

已同步 Vitis 日常使用位置（先备份原文件，再核对拷贝 SHA256）：

- `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/_ide/bitstream/ultrasound_B_mode_imaging.bit` 及同名 `.ltx`。
- `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/build/B_mode_focus_imaging.elf`。
- `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/ubmi_p/hw/ultrasound_B_mode_imaging.xsa`。
- `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/ubmi_p/export/ubmi_p/hw/ultrasound_B_mode_imaging.xsa` 及同名 `.bit`。

新旧 XSA 中 `ps7_init.c/.h/.tcl` 逐字节一致，PS 外设/引脚和地址映射未变，复用现有 BSP/FSBL；本次新 CSR 通过现有 BRAM 地址空间访问，不需新增 BSP 驱动。不要把“复用”理解成重新生成了 FSBL。

`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/BOOT.BIN` 未修改，原 SHA256 为 `30BE9B06D8E058C6D26B249EA960DC460CB467EE3EF7D57B3812F2DDDB256360`；它不包含本次 AFE 功能。

## 上板前

必须更新配套 bit/XSA、PS 应用及 ESP32 固件；旧 BOOT.BIN 不会自动包含这些修改。请先单独确认设备可用及高压关闭条件，再进行只读查询和 AFE 掉电/恢复验证。实际功耗、JTAG 可访问性、图像恢复、USB/Wi-Fi 吞吐和 48 小时稳定性均尚未验收。
