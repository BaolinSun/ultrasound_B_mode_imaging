# 固定 DDR SDIO/UDP 链路验证记录

日期：2026-10-09，工具为 ESP-IDF 6.1、Vivado/Vitis 2026.1。

本记录只确认源码、自检查与离线构建。**没有烧录 ESP32，没有更新板上 Zynq 启动介质，没有执行 SDIO 板测或 48 小时验收。**此前 COM3 授权未沿用。本次交付数据源为固定独立 DDR 测试帧，不是成像采集。

## 改动基线与源一致性

- 实施前工作树干净，基线为 `6bdf818`；没有需要补存的任务相关改动，未创建空检查点提交。
- `build/src/ps` 与当前 `workspace/B_mode_focus_imaging/src` 的变更源保持一致；公共协议副本由自检查核对。
- 将当前已修复的 Vivado BD 同步到规范 `build/src/bd/pus_ctrl_system.bd`，保留原有 USB、UART、SPI 和板级配置，避免干净重建回退。未修改成像 RTL。
- 软件构建在独立的 `build/sdio_vitis_*` 目录完成，未删除或重新生成用户当前 `workspace`。当前 IDE 的旧 BSP 没有被强制替换；调试新硬件时应使用新平台，或按新 XSA 更新平台，不能混用旧 PS 初始化与新应用。

## 自检查

命令：`esp32c61/tests/run_tests.ps1`，最终日志在 `esp32c61/tests/output/latest-tests.log`。

- C 原始 UDP 与配置事务、带包头发送、实际 ESP bridge 任务模型、实际 Zynq bridge 状态机全部通过。
- ESP 接收模型检查 1538 块及最后 2 块、重装所有权、双缓冲背压、发送租约、排队顺序、单帧/连续、元数据提交、重复提交、CRC、停止、模式切换、断网、接收/提交错误、30 秒接收及末窗口确认超时、复位与初始化失败。驱动和无线调用为严格 test double，不是无线或 SDIO 电气仿真。
- Zynq 状态机检查固定 DDR 逐字节内容、四帧窗口序号回绕、元数据提交顺序、拒绝/背压/重复授权、CRC/故障结果、超时、输出切换和 DMA 失败不重放；另对前 160 个寄存器访问位置逐一注入失败。
- Python 58 项通过，包括新协议乱序、一致/冲突重复、缺首/中/末包、超时后下一帧恢复、旧会话迟到、非法长度、CRC/内容错误、两帧上限，以及实际 UDP 回环 CLI 完整文件/缺包不输出文件。
- 网页脚本 7 项通过，包括原配置/密码/上传/停止/断线恢复和 SDIO 显式启动条件；DOM/HTTP 使用模拟后端，未代替真实设备网页验收。
- 负面测试中出现的 `Soak FAILED`、`passed:false` 是预期故障用例输出，整体以 unittest `OK`、Node 零失败和脚本退出 0 为准。

主要生产 C 文件的 GCC gcov 结果（测试输出 `20261009-115713989`）：

| 文件 | 行执行 | 分支执行 | 分支取值至少一次 |
|---|---:|---:|---:|
| raw_transfer.c | 100% | 100% | 100% |
| config_model.c | 100% | 100% | 100% |
| framed_transfer.c | 100% | 100% | 100% |
| ESP sdio_bridge.c | 99.48% | 99.25% | 90.23% |
| Zynq sdio_bridge.c | 100% | 100% | 95.16% |

覆盖率仅针对表内文件；不宣称移植的 SDPS 硬件驱动、SDK、完整固件或公共头每份内联实例达到同等覆盖率。`FREE/RECEIVING/READY/SENDING/RAW_TEST` 与主机四个阶段均有测试场景，但未使用独立 FSM 覆盖工具量化。未执行门级仿真：本次没有 RTL 功能增量。

## ESP32 构建与内存

默认、throughput 两种配置构建及 `size` 成功，均为 C61/4 MiB Flash/Quad 40 MHz PSRAM；未烧写。构建与 size 日志在 `tests/output/sdio-*-build.log`、`sdio-*-size.log`。

| 配置 | bin 字节数 | 链接期 DIRAM | DIRAM 余量 |
|---|---:|---:|---:|
| default | 883088 | 114043 / 256624，44.44% | 142581 |
| throughput | 884560 | 126005 / 256624，49.10% | 130619 |

两份 PSRAM 帧池共 1574912 B，内部 DMA 数据块共 8192 B，UDP staging 1472 B，另外还有驱动描述符、任务栈与 Wi-Fi/HTTP 动态开销。链接期余量不能等同于运行时空闲堆。堆低水位、实际 PSRAM 分配及吞吐必须板测。

## Zynq 构建与实现

- Vivado 综合、布局布线、bitstream 和含 bit 的 XSA 导出成功。
- 新 XSA 构建的 BSP 同时包含 SD0 `0xe0100000` 和 SD1 `0xe0101000`；应用按 SD1 基地址查找，不按实例 0 选择。
- 独立 Vitis 平台、FSBL、应用 ELF 和 BOOT.BIN 生成成功。启动顺序为 FSBL → bitstream → 应用。
- `build/scripts/check_sdio_build.ps1` 核对规范/当前/构建 PS 源、协议副本、XSA SD0/SD1/SPI1、BSP 两控制器、DDR 对齐/边界和 BOOT 分区顺序。
- 默认 SDIO 25 MHz；50 MHz 为可选构建参数，驱动分支交叉编译检查不等于 50 MHz 板级通过。
- 最终独立平台：`build/sdio_vitis_20261009_120005488`。应用 text/data/bss 为 174795 / 140976 / 952944 B；测试缓冲区占用 `[0x00151900, 0x00211d00)`，64 字节对齐，与 USB 保留区 `0x1c000000`、PL 写入区 `0x20000000` 不重叠。

最终交付 SHA256：

```text
FAF27936451E6D6B001B65C9BF5DF1D4DBB90F671DEC3B1785A5CF57F8D6E8DB  build/output/BOOT.BIN
1CF9BB7C78D0FC1554A4CBC3C3F1C8CA828507FC3C463F9E60545107E8F34D6D  build/output/B_mode_focus_imaging.elf
BA542A3B3C97833C5797E300A6EEC12FF867445822C08D26B924A9B365579BAD  build/output/ultrasound_B_mode_imaging.xsa
5DF930A55688EFC96026A195C6E659B90B0C12EB9057885FDD06B151FD1EA92B  esp32c61/build/ultrasound_psram_udp.bin
EBE1A8B399151B05FE0C6A876E542CC1A70D865BBC2732A4338C25E4D4A208E0  esp32c61/build-throughput/ultrasound_psram_udp.bin
```

实现报告：`build/reports/sdio_timing.rpt`、`sdio_utilization.rpt`、`sdio_drc.rpt`、`sdio_cdc.rpt`、`sdio_bus_skew.rpt`、`sdio_methodology.rpt`。

| 项目 | 结果 |
|---|---|
| WNS / TNS | +0.597 ns / 0 |
| WHS / THS | +0.017 ns / 0 |
| LUT | 28039 / 78600，35.67% |
| FF | 58797 / 157200，37.40% |
| BRAM Tile | 170.5 / 265，64.34% |
| DSP | 293 / 400，73.25% |
| 三项异步 FIFO bus skew slack | +9.438 / +9.552 / +9.493 ns |

**仍未达到整工程发布验收条件**：CDC 报告中 `clk_out1_clk_gen → clkdiv2_mmcm` 有 6 Unsafe、27 Unknown、6 No ASYNC_REG；`clk_out2_clk_gen → clkdiv2_mmcm` 有 2 Unsafe、25 Unknown。另有 22 项缺少 I/O delay 方法学警告、5 项 LUT 驱动异步复位等警告。没有给这些路径新增豁免，也没有用 false path 掩盖问题。现有约束下 setup/hold 通过不等于 CDC clean 或外部 I/O 时序已验收。本任务不修改成像 RTL；这些风险需另行审查关闭。

## 板级状态

| 验证项 | 本次结果 |
|---|---|
| 原始 PSRAM UDP 实机回归 | 未执行，新固件待烧录 |
| SDIO 单帧 CRC / PC 逐字节一致 | 未执行 |
| 连续传输有效帧率 / 丢帧率 / SDIO 与 UDP 耗时 | 未测量 |
| USB 配置 / USB 数据 / 输出切换实机回归 | 未执行 |
| 断网、ESP/Zynq 复位、重新配网恢复 | 未执行 |
| 堆内存低水位与 48 小时稳定性 | 未执行 |

板测前须重新确认两端设备可用、ESP 串口和 Zynq 下载/启动方式。接线、简短 Python 命令及 48 小时流程见 `SDIO_BRIDGE.md`。
