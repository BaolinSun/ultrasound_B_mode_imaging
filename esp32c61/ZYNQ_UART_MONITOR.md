# 在 idf.py monitor 中查看 Zynq 日志

2026-10-12 后续 SDIO 握手修复、板测结果及当前固件哈希见 [SDIO 调试记录](SDIO_INIT_DEBUG_20261012.md)。

## 接线和功能

`Zynq PS UART0 MIO27/TX → ESP32-C61 GPIO10/UART0 RX → ESP32 USB Serial/JTAG → PC`

两端共地、电平兼容；串口为 **115200、8N1、无流控**。只需发送方向与 GND；反向 `GPIO11 → MIO26` 线无需连接。即使该反向线已经接好，ESP 应用将 GPIO11 设为输入/高阻，不发送串口数据。

Zynq UART0 的 MIO26 是 RX、MIO27 是 TX。原 UART1 的 MIO16/17、460800 参数处理保留，BSP stdin 仍指向 UART1；应用日志及 BSP stdout 改为 UART0。UART1 按固定基地址 `0xe0001000` 查找，GIC 中断从返回配置解析，不能再依赖 UART 实例 0。SPI1 SS1/SS2 关闭，不与 UART0 引脚复用冲突。

没有导入参考工程的 READY 引脚或反向命令，没有修改 SDIO/UDP 协议。USB/SDIO 数据通路仍由 `DATA_OUTPUT_MODE` 编译宏选择。

## 最短运行步骤

1. 使用本次新构建的 ESP32 固件及 Zynq BOOT.BIN。此前未启用 UART0 的旧 BOOT.BIN 无法通过 MIO27 输出应用日志；仅更新 ESP32 不够。
2. 打开 ESP-IDF 6.1 环境，进入 ESP32 工程并启动 monitor。以下 COM3 仅为示例，使用 ESP32 USB Serial/JTAG 实际端口：

```powershell
cd D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61
idf.py -p COM3 monitor
```

性能配置固件使用对应构建目录：

```powershell
idf.py -B build-throughput -p COM3 monitor
```

3. 先确认 ESP32 打印 `zynq_uart: RX-only ...`，再复位/启动 Zynq。无需输入串口命令，monitor 中应出现：

```text
[ZYNQ] @UART_LOG UART0 MIO27/TX -> ESP32 GPIO10/RX 115200 8N1
[ZYNQ] ...应用启动及已有日志...
[ZYNQ] @SDIO DDR_TEST ... compiled=SDIO
[ZYNQ] @SDIO ONLINE SD1 4bit; waiting webpage start
```

如果没有 `@SDIO ONLINE`，查看同一 monitor 中的 `[ZYNQ] [SDIO]` 初始化错误和 `@SDIO ERROR`，不要强制解除网页启动条件。

## 缓冲与限制

ESP 接收缓冲 4096 B，任务栈 3072 B，优先级 2（低于 SDIO 的 5）；每批处理后让出 CPU。日志按 CRLF/LF 分行，最长分段 512 B，不破坏 UTF-8 完整字符；无换行内容在空闲 200 ms 后刷新，未收齐的 UTF-8 尾部等待后续字节。每段添加 `[ZYNQ]`，长行可能显示为多段。

不启用 USB 控制台的无限等待驱动模式。USB 未连接时丢弃日志字节，恢复并收到后续日志时提示累计丢弃字节数。USB 已接线但 monitor 未读取/过载时，通过 IDF 默认 USB VFS 的有限等待 `fsync()` 检测未排空的输出，恢复后提示 `USB console stalled ...; Zynq log may be incomplete`；统计的是可能缺失的批次数，而不是无法确认的丢失字节数。默认 VFS 等待截止为最后成功写入后 50 ms，不改为无限等待。

FIFO/接收缓冲溢出、帧/奇偶错误或 break 会清理当前不完整日志并提示缺失。UART 错误提示按秒限流，输出未恢复时保留错误计数。日志仅用于调试，不是无损采集接口，不对 Zynq 或数据链路施加背压。

接收模块启动失败会释放已安装的 UART 驱动并报告错误，Wi-Fi/UDP/SDIO 服务继续启动。可在 menuconfig 的 `Zynq receive-only UART log` 中关闭转发，默认启用。控制台必须保持 USB Serial/JTAG。

ESP 接收任务启动前的日志无法补录；同上电顺序可能漏掉最早的 Zynq 输出。建议先运行 ESP，再复位 Zynq。未额外开启 FSBL 详细调试，也不承诺捕获 ROM 输出。

## 重建及交付路径

从成像工程根目录执行：

```powershell
.\esp32c61\tests\run_tests.ps1
.\esp32c61\build.ps1 -Profile default
.\esp32c61\build.ps1 -Profile throughput
.\build\scripts\build_sdio.ps1 -Stage all
.\build\scripts\check_sdio_build.ps1
```

本次需要启用 UART0 的硬件 XSA 和 FSBL，不能只重编译应用后混用旧 UART0 未启用的硬件。2026-10-12 已修复并验证现有 `workspace/ubmi_p` 与 `workspace/B_mode_focus_imaging`，平时直接在原 Vitis 工作区 Clean、Build、Download 即可，无需换到 `build/sdio_vitis_*`。本次未实际 Download/运行设备。

重定向使用应用的 `__wrap_outbyte()` 与 `UserConfig.cmake` 中的 `-Wl,--wrap=outbyte`，不再依赖将 BSP 的 `outbyte` 改成 weak；SDK/BSP 保留强符号也能正常链接，普通平台/BSP 重建不会重新产生重复定义。不要删除这个链接选项。

Vitis 2026.1 的 `xilstandalone.cmake` 实际用 stdin 推导 stdout。当前 BSP 已独立解析 stdout，配置为 UART0 stdout/UART1 stdin。若重新生成整个 BSP，该元数据修正可能被 SDK 覆盖，但应用的字符包装仍固定输出 UART0，UART1 参数处理也不受影响。需要恢复平台元数据时，从成像工程根目录执行 `.\build\scripts\repair_vitis_workspace.ps1`；它只修复和构建现有 BSP/应用，不删除工作区、不改安装目录、不下载设备。运行期间不要同时点击 IDE Build。界面属性未刷新时可重开工作区。

BOOT 打包输入（绝对路径，按顺序）：

1. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\zynq_fsbl.elf`
2. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.bit`
3. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging.elf`

结果：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\BOOT.BIN`。

ESP 固件：`esp32c61/build/ultrasound_psram_udp.bin`、`esp32c61/build-throughput/ultrasound_psram_udp.bin`，烧录需包含相应 bootloader/分区表并使用各构建目录的 flash_args，不能仅拿应用 bin 代替完整烧录配置。

输入清单及哈希为 `build/output/boot_inputs.sha256`；最新 IDE 构建与修复记录见 [VITIS_IDE_UART_FIX.md](VITIS_IDE_UART_FIX.md)。原 `UART_BRIDGE_VERIFICATION.md` 为已注明缺陷的历史记录。烧录及实机验收必须另行确认设备，不沿用此前 COM3 授权。
