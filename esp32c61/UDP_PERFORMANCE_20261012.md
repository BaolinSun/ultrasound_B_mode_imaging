# SDIO → Wi-Fi UDP 提速修改与板测

日期：2026-10-12。修改前代码工作区干净，基线 `fb1a241`；用户提供的 `esp32c61/sdio_20frames.jsonl` 保留原样，不纳入功能提交。本次用户重新确认 COM3，并授权 ESP32 烧录及当前 Vitis/JTAG 应用下载。

功能提交：`47aae37`。

## 结果

同一接收主机 `192.168.3.31:50001`、设备 `192.168.3.27`、固定 DDR 测试模式，完整有效吞吐从约 **10.59 Mbit/s / 1.68 fps** 提升至约 **12.59 Mbit/s / 2.00 fps**，短时实测提升约 **19%**。

这不是 30 Mbit/s 的空口/iperf 测试，也不是无丢帧承诺。无线环境仍有波动，本次未验证 48 小时稳定性。

| 测试 | 实际接收时长 | 完整有效帧 | 丢弃部分帧 | 有效吞吐 | 有效帧率 |
|---|---:|---:|---:|---:|---:|
| 原固件 + 新 PC 工具（基线2） | 22.001 s | 37 | 7 | 10.594 Mbit/s | 1.682 fps |
| 首轮优化 | 22.005 s | 44 | 4 | 12.596 Mbit/s | 2.000 fps |
| 最终优化 | 42.017 s | 84 | 6 | 12.594 Mbit/s | 1.999 fps |

以上三轮使用相同的新 PC 工具，均无中途设备停止、无 PC 接收队列溢出、无非法数据报或重复包；完整帧 CRC32/固定模式校验通过。基线2/最终优化的首个到末个有效格式数据报区间吞吐分别为 10.762/12.693 Mbit/s；这个补充指标排除了启动等待，不能当作空口 PHY 吞吐。

首次基线日志 `udp-opt-baseline.jsonl` 中设备提前取消，只有约 7 秒有数据；它被保留作为异常阶段证据，**没有用于上述性能结论**。

## 修改内容

### SDIO 和 CRC

- ESP32 在已有接收窗口且仍缺 DMA 块时，等待 `sdio_slave_recv(..., 1 tick)` 的数据事件。数据就绪可以立即唤醒，避免对每轮数据处理无条件休眠。无数据、ACK 未到及背压状态仍有有界等待，不进行无限忙轮询。
- 接收等待发生在控制互斥锁之外；只有接收任务能重置/卸载驱动。等待期间收到停止请求，会抑制该块消费，先归还描述符，再在下一轮清理驱动；不覆盖 SENDING 缓冲区，不重发失败 DMA。
- Zynq 等待新窗口额度时，先查询状态及 grant，避免反复读四个 session 字节和其他元数据。每个实际 DMA 窗口前仍校验主机模式、会话、窗口大小和超时；没有降低复位恢复保护。
- ESP32 将软件半字节 CRC 路径替换为经验证的 IDF ROM CRC，并增加 1 KiB 内部 SRAM 字节查表候选。启动先用独立参考校验两种算法，再比较 32 KiB 的处理时间，选择更快者。两者都保持 IEEE/zlib 增量语义，不是硬件 CRC 加速器。
- 本板启动比较：ROM **1973 µs**，SRAM 表 **2281 µs**，最终选择 ROM。查表并未更快，因此不强制采用它。计时受调度影响，是本次启动的比较结果，不是所有环境的保证。
- 五个数据通路源文件单独使用 `-O2`；保留断言及其他应用配置。实际 compile_commands 已核对，最终 `-O2` 覆盖全局 `-Os`。
- SD1 保持四线 25 MHz、16×512 B DMA 窗口、双 PSRAM 完整帧槽位，没有扩大窗口或切换 50 MHz。

### UDP

- 状态进度由每包刷新改为每 32 包、帧尾、重试及异常退出时刷新，减少互斥操作。
- 带头 UDP 的 offset/index 独立按成功发送的载荷长度推进，不再依赖 UI 进度回调；重试保持相同包头和原始载荷。
- 每包仍检查取消/断网/截止时间；保留 100 次暂时错误重试、每 32 包至少 1 ms 让出 CPU、30 s 总超时。
- UDP 载荷仍最多 1440 B，加 32 B 包头；无 ACK、补包或重传。原始无包头自测保留。
- 记录最近一次本地发送耗时，下一帧开始时不把该历史计时清零。

### PC 接收和可观测性

- 网络接收、组帧校验、日志/文件输出分离。网络队列默认最多 4096 数据报，输出队列最多 64 个事件；组帧仍最多两个待完成帧，没有无限积压。
- 网络队列满时明确统计 `local_queue_drops`；输出端长期堵塞会明确失败，不无限阻塞数据接收。文件只保存 CRC/测试模式通过的完整帧，仍拒绝覆盖已存在的捕获文件。
- 日志增加 `run_start`、`run_id`、完整帧目标、时间限制及是否校验测试模式，避免把同一追加日志中的不同运行混为一谈。
- 坏帧增加 `missing_indices` 和 `missing_packets`；连续缺 5 包的位置现在可以直接看到。
- 增加 `queue_high_water`、停止时剩余队列数据报数量及数据活动区间吞吐。它们不替代原有统计，也不将帧号跳过直接视为网络丢包。
- 网页/API 增加最近一帧 SDIO 接收、CRC 和 UDP 本地发送计时，以及 CRC 算法/启动比较时间。

## 验证

- C 自检查通过：原始/带头字节一致性，分包尾部，重试、取消、失败时最终计数，ROM/SRAM CRC 选择及异常拒绝，实际接收任务事件等待，窗口交接、回绕、停止/复位和缓冲所有权。
- 85 项 Python、7 项网页测试通过。新增 PC 队列满计数、输出堵塞、文件覆盖拒绝、缺包索引及下一帧恢复测试；自动板测工具覆盖设备忙碌拒绝、旧日志保护、监听后启动、启动超时仍尝试停止。
- ESP-IDF 6.1 默认/throughput 配置构建通过；COM3 实际烧录 throughput 配置，保留 NVS。
- 当前 Vitis 2026.1 工作区 SDIO/USB 两种宏模式构建通过，最终恢复默认 SDIO；用户仍可在原 workspace Build/Download/Run。JTAG 下载的是相同应用 ELF，没有更换工作区。
- 最终 SDIO ELF：text 176059 B、data 141004 B、bss 953040 B。USB 验证：text 172155 B、data 140996 B、bss 164624 B。既有旧警告没有全部清零。
- 完整源码副本、SD0/SD1/UART 映射、BSP console、DDR 测试帧对齐/区域、BOOT 顺序及 bit 哈希检查通过。
- 本次不改 RTL、bit、XSA 或 FSBL，不重新实施硬件，不把既有时序/CDC 风险标记为已解决。

实际板测还包括：

- SDIO 单帧：787456 B / 547 包、CRC32 `3AA5AEBF`、测试内容通过，无丢弃；PC 首包到末包 0.249832 s（约 25.22 Mbit/s），这是单帧接收区间指标，不是连续有效吞吐。
- 原始 PSRAM UDP：787456 B / 547 包、逐字节 mismatch=0，约 24.30 Mbit/s，原始工具行为保持正确。
- 最终状态：网络/上传服务就绪，SDIO 已握手，`running=false`、`busy=false`、`raw_busy=false`、空闲槽位 2，SDIO CRC 错误 0。内部堆低水位 32220 B。
- 最终累计错误 1、丢弃 2来自连续测试主动停止时取消正在发送/等待的帧；停止前状态采样错误/丢弃均为 0，不能把取消当作 CRC 错误。
- 成功发送仍可能显示历史 `socket_errno=12` 及 retries，这是恢复过的暂时资源不足，结果以 `sent_local` 和 PC 校验为准。

诊断证据位于 `build/reports/udp-opt-*.jsonl`、`*.device.json`、`udp-opt-final-status.json`，原始自测为 `udp-opt-raw.log`。构建/测试日志位于 `esp32c61/tests/output/udp-opt-*.log`；BOOT.BIN 仅生成到交付目录，没有写入启动卡。

## 最短测试命令

进入 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61` 后，自动监听、启动连续 SDIO、采样状态并停止：

```powershell
python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 60 --log sdio_optimized_60s.jsonl
```

无需点击网页；运行期间不要同时手动启停。工具为监听/启动留 2 秒余量，因此接收上限约 62 秒。已有同名日志会拒绝使用，请换新文件名。UDP 端口不是 50001 时加 `--port`，设备网页目标端口须一致。

仍可按原流程先运行接收器，再点击网页“SDIO 连续测试”：

```powershell
python .\pc_tools\sdio_udp_receiver.py --source-ip 192.168.3.27 --frames 20 --seconds 30 --test-pattern --log sdio_optimized_20frames.jsonl
```

接收器退出后点击“停止全部传输”。网页 Ctrl+F5 刷新后可看到新增计时。

## 当前产物与 SHA256

BOOT 输入顺序：

1. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\zynq_fsbl.elf`
   - `A71159C76FA6ACBBF5EBC9EDB4D58AABE21B3F4A1A81E67C98C69D4AED0C74C4`
2. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.bit`
   - `29B2A863B48FD3C284AA89CD69B90D42372C7AD9E0A85CE74274FADAE5C3E939`
3. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging.elf`
   - `D7BEDA6B53707C9ED4D323BCD49EA969D226A95603A37A98311ED48526CE40DA`
   - 与 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf` 一致。

交付输出：

- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\BOOT.BIN`
  - `154C5D4706AB79EEB12709AEC3AB62D085163EE56CB37DF0A944564A918B7772`
- 实际烧录：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput\ultrasound_psram_udp.bin`
  - `ECBA1B221FA18C4202D42A0887CA130CDA26D689195722317554E50BE36B5F1B`
- 默认配置（构建未烧录）：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build\ultrasound_psram_udp.bin`
  - `F205D9F00E57CB8395C6479F1488D0B2703A8592C9D28CA808DBAE9D167779B5`

本记录取代之前 SDIO 初始化修复报告中的“当前产物”哈希；旧报告保留为历史证据。
