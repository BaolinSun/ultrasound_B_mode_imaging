# SDIO → PSRAM → Wi-Fi TCP/UDP 固定 DDR 测试

## 当前范围与启动

下文固定 DDR 测试是历史版本说明。当前 PS 已接入真实成像 DDR，仍存在覆盖与帧起点限制；网页上传不自动启动成像。新增控制入口见 [Wi-Fi 下发命令](WIFI_COMMANDS.md)，不要将历史单向日志说明理解为当前 GPIO11 无发送功能。

Zynq 日志现可经 ESP32 的 USB `idf.py monitor` 查看，单向接线和操作见 [Zynq 串口日志](ZYNQ_UART_MONITOR.md)。需同时更新含 UART0 的新 BOOT.BIN 和 ESP 固件，不需要独立 Zynq USB 串口。

本版本只发送 **Zynq 独立 DDR 测试缓冲区**，不是实时成像数据。不会调用 BEAM START、CFG END 或 HV ENABLE。USB 数据与 SDIO 数据互斥，USB 参数配置始终保留。ESP32 默认不自动发送。

ESP32 的 `main/wifi_transport_config.h` 选择 Wi-Fi 帧协议，当前默认 TCP。仅切换 TCP/UDP 不修改 Zynq 或 SDIO 协议，不需要更新 bit/XSA/BOOT.BIN。详细协议、宏切换和验证见 [TCP_TRANSPORT.md](TCP_TRANSPORT.md)。下文 UDP 格式说明仅适用于选择 UDP 时，原始 PSRAM UDP 自测始终保持不变。

1. 使用本次生成的新 Zynq 启动镜像和 ESP32 固件（烧录前确认板卡及端口）。
2. Zynq 使用 `DATA_OUTPUT_MODE=DATA_OUTPUT_SDIO` 编译的 BOOT.BIN 启动后，自动建立 SDIO 链路；等待 `@SDIO ONLINE`，无需发送串口命令。当前交付默认 SDIO，但仍由网页启动单帧/连续发送。
3. 网页配置 PC 的 IPv4 与 TCP/UDP 共用端口，等待 Wi-Fi 连接。
4. 在 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61` 启动接收，再点网页“SDIO 单帧”或“SDIO 连续测试”。

单帧接收命令：

```powershell
python .\pc_tools\sdio_tcp_receiver.py --source-ip 192.168.3.27 --test-pattern --output-dir .\sdio_capture
```

3 分钟连续测试（自动识别 TCP/UDP；自动先监听再启动，结束先停止设备再停止接收，无需点击网页）：

```powershell
python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 180 --log sdio_3min.jsonl
```

原来的 `raw_udp_receiver.py`、`soak_test.py` 仍只用于网页“PSRAM 自测上传”，不能接收带包头 SDIO 数据。

## C 宏选择 USB / SDIO

修改规范源 `build/src/ps/data_transfer/output_config.h` 中的一行，然后重新构建 Zynq 应用和 BOOT.BIN：

```c
#define DATA_OUTPUT_MODE DATA_OUTPUT_SDIO  /* SDIO → ESP32 → TCP/UDP，目前默认 */
/* 或改为：#define DATA_OUTPUT_MODE DATA_OUTPUT_USB */
```

两种模式不会同时输出，不支持运行时切换。`OUTPUT USB/SDIO/STATUS` 串口/USB 命令已删除。UART/USB 的其他参数配置命令保留，串口日志仅用于观察。USB 模式不分配 SDIO DDR 测试帧、不枚举或访问 SD1。

`build_sdio.ps1` 从规范源导入新 Vitis 应用；当前 IDE 副本为 `workspace/B_mode_focus_imaging/src/data_transfer/output_config.h`，若使用该 IDE 工程构建，需同步同一宏，并使用含 SD1 的新平台。更新启动镜像并重启后模式才生效；切换镜像前先停止网页测试，切换后重新启动，不跨模式续传。

## 接线与硬件配置

| 信号 | Zynq SD1 | ESP32-C61 |
|---|---|---|
| CLK | MIO12 | GPIO26 |
| CMD | MIO11 | GPIO25 |
| D0 | MIO10 | GPIO27 |
| D1 | MIO13 | GPIO28 |
| D2 | MIO14 | GPIO22 |
| D3 | MIO15 | GPIO23 |

两端共地、电平兼容；CMD/DAT0..3 需符合 SDIO 要求的外部上拉。**不接 GPIO2↔MIO24 READY**：当前 MIO24 属于 SPI1。SD0 保留在 MIO40..45 用于 SD 卡启动；不改变 SPI1、UART1、USB0 PHY MIO49 配置。

默认四线 25 MHz，枚举 400 kHz。`build/scripts/build_sdio.ps1 -Stage software -SdioClockMHz 50` 可生成 50 MHz 应用（包含 CCCR 高速使能）；默认参数为 25。底层编译定义为 `SDIO_RUN_CLOCK_HZ`。必须重新构建并经过信号完整性/板级验证，不保证飞线在 50 MHz 可稳定运行。

## 协议和所有权

数据帧长度 787456 B = 1538 个 512 B SDIO 块；每窗口最多 16 块，最后窗口 2 块。ESP32 有 16 个内部 DMA 接收块和两份 PSRAM 帧缓冲；PSRAM 使用约 1.502 MiB。原始自测只在 SDIO 停止且所有租约归还时借用其中一份，不再额外常驻第三帧。

共享寄存器是**逻辑字节编号**；主机通过参考驱动的 CMD52 地址映射访问。32 位寄存器为小端；元数据写完后最后更新命令序号，直到 ESP 确认前不可修改。

| 寄存器 | 写入方 | 含义 |
|---|---|---|
| 0 / 1 | Zynq | 命令 NOP=0、BEGIN=1、ABORT=2 / 提交序号 |
| 2 / 7 | ESP | 设备标识 0xC6 / SDIO 协议版本 2 |
| 3 | ESP | READY=1、STREAM=0x10、BACKPRESSURE=0x20 |
| 4 / 5 / 6 | ESP / ESP / Zynq | 窗口块数 / 授权序号 / 确认序号 |
| 8 / 9 | ESP | 命令确认序号 / ACCEPTED=0、DONE=1、REJECTED=2、BAD_CRC=3、FAULT=4 |
| 10 | Zynq | 输出模式 USB=0、SDIO=1 |
| 32..35 | ESP | 当前会话号 |
| 36..39 / 40..43 / 44..47 / 48..51 | Zynq | 帧号 / 字节数 / CRC32 / 会话号回显 |

授权序号按 1..255 循环，0 不代表有效窗口。前一窗口收齐、所有块重新装载 DMA 且主机确认后才授权下一窗口；两帧均占用时，在下一帧 BEGIN 前背压。帧状态为 FREE→RECEIVING→READY→SENDING→FREE，异步发送回调归还读租约后才可覆盖。

CRC32 为 IEEE reflected，多项式 0xEDB88320，初值和终值取反，与 `zlib.crc32` 一致。Zynq 初始化时计算固定 DDR 内容 CRC，ESP 收齐并校验后才提交 Wi-Fi 帧；失败不转发。

UDP 包头 32 B，全部字段使用网络字节序：

`magic:u32, version:u8, header_len:u8, flags:u16, session_id:u32, frame_id:u32, frame_bytes:u32, offset:u32, payload_bytes:u16, packet_index:u16, frame_crc32:u32`

magic 为 ASCII UBSD、version=1、header_len=32、flags=0。每包数据最多 1440 B；共 547 包，末包原始内容 1216 B。IPv4 UDP 总载荷最大 1472 B，IP 包最大 1500 B，不主动制造分片。

PC 允许乱序和一致的重复包，最多维护两个未完成帧；默认 2 秒超时，CRC 错误或冲突重复包整帧丢弃。只写出完整且校验通过的原始数据，不把包头写入文件。UDP 没有补包、ACK 或重传；本地发送完成不表示 PC 收齐。统计中的丢帧率只包含观察到的部分帧，完全没有任何数据包到达的帧无法由接收工具计数。保存文件使用独占创建，绝不静默覆盖既有采集文件。

## 控制、恢复和构建

- `POST /api/sdio/start` JSON `{"frames":1}` 单帧，`{"frames":0}` 连续；未选 SDIO、忙碌时 409，SDIO/网络/目标未就绪时 503，非法参数 400。
- `POST /api/sdio/stop` 和 `POST /api/stop` 停止全部上传。`GET /api/status` 的 `sdio` 对象显示接收帧、CRC 错误、背压、发送帧数/字节及槽位状态。
- 目标修改下一帧生效；Wi-Fi 修改、断网或两端复位会终止当前测试，恢复后须显式启动，不自动重放。输出模式只能重新编译后切换。
- SDIO 错误、ABORT 或单帧/末窗口确认超时 30 秒，重建驱动描述符并刷新会话。主机重连先请求一次 ABORT，再枚举。DMA 失败不回退重发 PIO，避免未知部分传输后重复数据。
- 本次未新增 PL 时钟域或 RTL CDC 路径，SDIO 使用 PS 硬核 MIO。软件所有权通过互斥锁和任务通知交接，不能把软件锁当成 RTL CDC 同步器。

从项目根目录构建硬件及独立 Vitis 临时平台（不删除当前 workspace）：

```powershell
.\build\scripts\build_sdio.ps1 -Stage all
.\build\scripts\check_sdio_build.ps1
```

ESP32 默认/性能配置及自检查：

```powershell
.\esp32c61\build.ps1 -Profile default
.\esp32c61\build.ps1 -Profile throughput
.\esp32c61\tests\run_tests.ps1
```

Zynq 输出为 `build/output/BOOT.BIN`，ESP32 输出分别为 `esp32c61/build`、`esp32c61/build-throughput`。硬件源、Vitis BSP 和 FSBL 必须来自同一次 XSA；不能只更新应用 ELF 而继续使用未启用 SD1 的旧 FSBL。受管理的 PS 源是 `build/src/ps`，当前应用副本也同步更新；公共协议正本在 `esp32c61/protocol`，自检查确认其与 PS 副本一致。

只修改输出宏时，无需重新实现 PL，使用已有 SD1 XSA/bit 执行 `build/scripts/build_sdio.ps1 -Stage software` 即可。bit 导出和 BOOT 打包均统一使用 `build/output/ultrasound_B_mode_imaging.bit`；Vivado 实现目录内的原始文件仍叫 `heart_top.bit`，不代表导出文件未更新。

BOOT.BIN 打包输入（顺序固定，绝对路径）：

1. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\zynq_fsbl.elf`
2. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.bit`
3. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging.elf`

打包清单：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\boot.bif`。生成结果：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\BOOT.BIN`。

## 板级验收与 48 小时测试

1. SDIO 单帧：Zynq 日志 DDR 地址不与 PL/USB 区域重叠；ESP CRC 正确，PC `passed=true`、787456 B、547 包，输出逐字节等于测试模式。
2. 连续：记录接收有效帧率、观察到的丢帧率、CRC 错误、内部堆最小空闲及 SDIO/UDP 耗时。UDP 丢帧可接受；不允许把坏帧输出为有效图像。
3. 两台 PC 切换目标：正在发送的一帧保持原目标，下一帧发往新目标。
4. 在传输中测试停止、ESP 复位、Zynq 复位、断网/重新配网；不得卡死或覆盖发送缓冲，重新开始后必须能再次收到完整帧。分别构建 SDIO/USB 镜像，停止当前测试、更新镜像并重启，回归 USB 配置与数据通路，不使用串口命令切换。
5. 48 小时接收命令（启动后网页选择连续测试）：

```powershell
1..48 | ForEach-Object {
  python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 3600 --log "sdio_48h_$_.jsonl"
  if ($LASTEXITCODE -ne 0) { throw "第 $_ 轮失败，检查日志，不自动重放" }
  Start-Sleep -Seconds 2
}
```

保存首尾网页状态和两端日志，确认堆内存无持续下降、无复位/挂死、有效帧均内容正确。断网恢复不自动继续是设计行为，故障注入与无中断 48 小时分别执行。软件模拟通过与板测通过必须分别记录，未执行的板测不能标成完成。

当前宏选择与统一 bit 名称的构建结果见 [2026-10-11 输出模式验证](OUTPUT_MODE_VERIFICATION.md)。原始 SDIO 构建、覆盖率及尚未关闭的 CDC 风险见 [2026-10-09 SDIO 验证记录](SDIO_VERIFICATION.md)。DMA 缓冲区与所有权管理依据 [ESP-IDF 6.1 SDIO Slave 文档](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c61/api-reference/peripherals/sdio_slave.html)。
