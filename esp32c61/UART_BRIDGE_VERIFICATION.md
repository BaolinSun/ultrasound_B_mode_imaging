# Zynq 单向 UART 日志转发验证记录

日期：2026-10-11。修改前工作树干净，基线提交 `5933503`，无需额外保存未提交改动。本次未烧录、未复位板卡，以下为离线软件和构建结果，不是板级验收。

## 实现范围

链路：Zynq UART0 MIO27 TX → ESP32-C61 GPIO10 UART0 RX → USB Serial/JTAG；115200、8N1、无流控、共地。GPIO11 输入/高阻，无 UART 发送调用，无 monitor 输入转发。

原 UART1 MIO16/17 的参数入口保持 460800。BSP stdin 为 UART1、stdout 为 UART0；UART1 按 `0xe0001000` 查找配置，从编码中断 `0x4032` 推导 GIC IRQ 82，不再假定 SDK 实例 0。UART0 在第一条应用打印前初始化；ARM 反汇编确认 `outbyte()` 调用 `XUartPs_SendByte(0xe0000000, ...)`。

未修改成像 RTL、SDIO/UDP 协议或 USB/SDIO 编译宏选择。FSBL 使用默认配置，`USER_COMPILE_DEFINITIONS` 未增加 FSBL 调试宏。日志故障不停止 ESP32 数据服务；启动前日志和过载日志不保证保存。

## 软件验证

- `esp32c61/tests/run_tests.ps1`：所有 C 测试通过；68 项 Python 测试、7 项网页测试通过。
- 新增 UART 分行测试：LF/CRLF、跨接收块、中文 UTF-8、512 B 长行分段、200 ms 空闲刷新、不完整 UTF-8 尾部等待、溢出后丢弃残行。独立分行模块行覆盖 100%，分支 taken 覆盖 97.50%。
- UART 驱动桩测试：RX-only 引脚/115200/4 KiB 缓冲/任务优先级 2，重复初始化、六个初始化失败点清理与重试、五类接收事件错误、读失败、USB 断开及输出拥塞恢复、警告未排空时计数保留、关闭转发配置；断言系统日志锁不嵌套于串口打印锁。接收模块行覆盖 100%，分支 taken 覆盖 94.44%。
- Zynq UART0 初始化及 outbyte 自检查：查找失败/错误基地址、初始化/波特率/格式失败、8N1、无 RX 中断、UART0 输出。行覆盖与分支 taken 覆盖 100%。
- 本地 Vitis BSP 生成修正测试：独立 stdout 解析、weak 默认 outbyte、未知模板拒绝、缺失成功标记时禁止旧 ELF 打包。
- 回归原始 UDP、带帧头 UDP、SDIO 窗口与错误恢复、缓冲区所有权、取消、配置保存及网页交互测试通过。UART1 中断和 USB 物理参数交互仍需实机回归，不能以驱动桩代替。

测试日志：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\tests\output\uart-bridge-tests.log`。

USB 默认 VFS 保留有限等待行为；`is_connected()` 只判断 USB 接线/SOF，不判断 monitor 是否打开。用默认 VFS 的 `fsync()` 有限等待检测未排空输出，恢复后提示可能丢失的批次，不能推算精确丢失字节。未切换无限阻塞驱动模式。

## ESP-IDF 6.1 构建

目标 ESP32-C61，默认/throughput 均构建成功，配置中 UART bridge 默认启用、控制台为 USB Serial/JTAG。

| 配置 | DIRAM 已用 / 可用 | 应用 bin 字节数 |
| --- | --- | --- |
| default | 117643 / 256624（45.84%） | 910640 |
| throughput | 129605 / 256624（50.50%） | 912096 |

DIRAM 为静态链接占用，不代表运行期堆内存低水位；任务栈及 UART 驱动缓冲分配仍需板测观察。

固件绝对路径与 SHA256：

```text
2C0257CA6E532C50810C58111A909485411CB1C7DC3AE09EAA106FA0EE4DE4C0  D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build\ultrasound_psram_udp.bin
854BF6294C6ADD6D1CBDF046BD5B3678072311777BA5D305FD9C4E9B0E622B61  D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput\ultrasound_psram_udp.bin
```

构建/size 日志位于 `esp32c61/tests/output/uart-bridge-{default,throughput}-{build,size}.log`；烧录使用相应目录的完整 `flash_args`，不只写应用 bin。

## Vivado / Vitis 2026.1

完整 Vivado 综合、实现、bit/XSA 导出成功；已同步规范 BD 与当前 Vivado BD。XSA 检查 UART0 MIO26/27、UART1 MIO16/17、SPI1 SS1/SS2 关闭，SD0 MIO40–45 与 SD1 MIO10–15 保留。

新软件工作区：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\sdio_vitis_20261011_192152731`。平台/BSP、FSBL、应用成功构建；旧 IDE 应用源同步，但旧平台不自动覆盖，后续 IDE 开发请使用新平台。

首次构建发现两个 2026.1 BSP 兼容问题：stdout 实际跟随 stdin、强符号 outbyte 重复定义。已在工程脚本中修正新工作区生成源，不改安装目录；第二次完整构建成功。首次失败日志保留为 `build/reports/uart-bridge-vitis-attempt1.log`，最终成功日志为 `uart-bridge-vitis.log`。修正构建成功标记检查，避免启动器错误返回 0 时误打包旧 ELF。

USB 模式 ARM 构建通过，`main()` 调用 `usb_send_adc_data()`，不调用 SDIO poll，也不分配 `test_frame`；SDIO 模式 ARM 构建通过，`main()` 调用 SDIO poll，独立 DDR 测试帧为 `0x151940..0x211d40`，64 B 对齐、长度 787456 B，未与保留 USB/PL 区重叠。最终恢复 SDIO 模式并重新打包 BOOT；USB 验证 ELF 单独保留，不打包到最终 BOOT。

`check_sdio_build.ps1` 全部通过：规范/IDE/实际构建源一致、XSA 外设映射、双 UART SDK 实例、stdout/stdin、UART1 IRQ、DDR 范围、BOOT 分区顺序及 canonical bit 与当前实现 bit 哈希一致。

### 时序、资源与未解决风险

- 受约束路径 WNS +0.597 ns、WHS +0.017 ns、TNS/THS 0、WPWS +0.264 ns，setup/hold/pulse-width 失败端点均为 0；WNS 达到 >0.5 ns 目标。
- LUT 28039/78600（35.67%），FF 58797/157200（37.40%），BRAM 170.5/265（64.34%），DSP 293/400（73.25%）。
- FIFO Gray bus skew 三项均满足，最小余量 +9.438 ns。
- **CDC 不 clean**：CDC-1 26、CDC-4 4、CDC-10 6、CDC-13 2 个 Critical 报告项；这些涉及既有成像控制/参数、复位/FIFO 等跨域路径，本次不作为已豁免或已修复。
- 方法学报告保留 LUT 异步复位 5 项、TIMING-18 缺失 I/O 延迟 22 项等警告。`check_timing` 缺少 input/output delay 的端口各 19；未约束内部端点和无时钟寄存器均为 0。正余量不覆盖这些未充分约束的接口。
- 综合日志含一次 `Common 17-354 Could not open 'C' for writing`（RTL 统计输出环节）；综合、实现与 bitgen 成功完成，要求的时序/CDC/DRC/资源报告均已生成。没有把该消息或既有警告隐去。

当前报告绝对目录：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\reports`，对应 `sdio_timing.rpt`、`sdio_cdc.rpt`、`sdio_bus_skew.rpt`、`sdio_utilization.rpt`、`sdio_drc.rpt`、`sdio_methodology.rpt`。

## BOOT.BIN 最终输入与哈希

打包顺序为 FSBL → bit → 应用，最终应用为 SDIO 模式。以下是最终检查时实际产物的 SHA256；重新构建后哈希可能改变。

```text
46D0647430B5F1FBDFACA48B1EDCCFFD0E3A220FFD2EBB7253EA516CDF4A318B  D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\zynq_fsbl.elf
29B2A863B48FD3C284AA89CD69B90D42372C7AD9E0A85CE74274FADAE5C3E939  D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.bit
976B1091FAB4D224FA37B0B718B444005AC5CE402C5FCFF9FCE46390F63C2B68  D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging.elf
1321EAB7EB9463B80F64EA15123F6FF9DE759573E4461BFB8FA685B02BEAAE97  D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\BOOT.BIN
CE6C1D71E7979169A9B5468D16B4B73CD61BB0EC7C5D8F226988107E77C8AAFC  D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.xsa
50E7A0A392845574C73ED53A9FA3DF9625DED13FF54BE6C43AA38CD7DA0115CA  D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging_USB_verify.elf
```

自动输入清单：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\boot_inputs.sha256`；打包配置同目录 `boot.bif`。产物按仓库规则留在本地构建目录，不将大二进制强行纳入 Git。

## 板级验收（未执行）

设备可用性及 ESP32 实际 COM 端口必须另行确认，不沿用此前 COM3 烧录授权。操作见 [ZYNQ_UART_MONITOR.md](ZYNQ_UART_MONITOR.md)。

1. 烧录匹配 ESP 固件/BOOT 后先启动 ESP 和 monitor，再复位 Zynq；检查 `[ZYNQ] @UART_LOG`、应用启动、SDIO ONLINE 或具体初始化错误。
2. 检查 GPIO11 保持输入，无 ESP→Zynq 命令；测试中文、长行和无换行输出。不增加成像/高压启动命令。
3. 关闭 monitor/断开 USB 后恢复，检查日志缺失提示及 UART 错误恢复；同时观察 SDIO/UDP 不挂死。不能把日志丢失等同于数据帧损坏。
4. 回归网页配网、原始 PSRAM UDP、SDIO 单帧/连续帧、USB 参数配置；记录帧完整率、传输耗时、堆低水位及串口溢出。
5. 分别烧录 USB/SDIO 编译模式验证原数据输出；持续 48 小时重复测试和日志观察，确认没有任务挂死/资源泄漏。板级记录另存，当前不填写虚构通过结果。
