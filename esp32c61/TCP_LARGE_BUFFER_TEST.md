# 大容量 TCP 发送缓冲实验（2026-10-13）

## 范围和选择

仅增加上传 socket 的发送容量；普通 TCP/HTTP 默认 5760 B、staging 5760 B、接收窗口 5760 B、发送前内部堆 headroom 16384 B 均保持不变。CRC、SDIO、Zynq、TCP 帧格式和应用回执不变，不更新 bit/XSA/BOOT.BIN。

| 显式档位 | 上传容量 | 相对 fast | lwIP 队列上限 |
|---|---:|---:|---:|
| fast（默认） | 23040 B | 1× | 64 |
| large | 46080 B | 2× | 128 |
| xlarge | 57600 B | 2.5× | 160 |

容量通过已有 IDF 6.1 专用适配层，在新 socket 连接前、lwIP 串行核心中设置并回读；不修改已安装的 IDF。队列上限同步编译到 lwIP 和适配层，但不预分配整块容量。实际排队量还受内部 SRAM、内存背压、对端窗口和无线速率限制。不得关闭内存保护来掩盖大缓冲的内存压力。

## 命令

在 ESP32 工程目录执行（先停止 monitor）：

```powershell
.\build.ps1 -Profile throughput -TcpTuning large -Action flash -Port COM3
python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 60 --log tcp_large_manual_60s.jsonl
# 更大容量，仅在设备状态和内存安全允许时测试
.\build.ps1 -Profile throughput -TcpTuning xlarge -Action flash -Port COM3
python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 60 --log tcp_xlarge_manual_60s.jsonl
# 恢复原默认容量
.\build.ps1 -Profile throughput -TcpTuning fast -Action flash -Port COM3
```

日志文件名必须未存在；Zynq 需运行现有 SDIO 固定 DDR 应用，设备需联网、空闲且 SDIO 握手完成。烧录后仅重启 ESP32，保留 NVS。测试前再次确认实际设备和端口。

## 自检查

适配层分别以 80/128/160 队列上限编译，覆盖大容量成功回读、队列不足拒绝、普通 HTTP PCB 不受影响、运行中的 PCB 禁止修改，以及原有错误路径。分档测试检查普通容量/staging 不随实验档增大、默认仍选 fast。

## 板测结果

COM3 实际烧录，仅 ESP32 重启，Zynq 保持现有固定 DDR SDIO 应用。PC 192.168.3.31:50001，ESP32 192.168.3.27，每帧 787456 B。顺序为 fast → large → fast，三次均运行计划的 60 秒。统计以 PC 完整帧 CRC/测试内容校验通过的字节数除以整个接收测试时长为准；仅活跃数据片段的速率另列，不能替代完整有效吞吐。

| 容量 / 次序 | 完整帧 | 接收测试时长 | 完整有效吞吐 | 活跃数据吞吐 | 内部堆低水位 |
|---|---:|---:|---:|---:|---:|
| 23040 B / 原基线 | 105 | 60.875 s | 10.866 Mbit/s | 11.061 Mbit/s | 21132 B |
| 46080 B / 大缓冲 | 119 | 60.633 s | 12.364 Mbit/s | 12.528 Mbit/s | **5056 B** |
| 23040 B / 恢复后重测 | 102 | 60.508 s | 10.620 Mbit/s | 10.760 Mbit/s | 16316 B |
| 57600 B | — | — | **未运行板测** | — | — |

46080 B 实际容量回读为 46080，完整有效吞吐比前/后 fast 对照高约 13.8%/16.4%。这是同环境顺序短测，不是无线环境严格可复现的提升保证，也不能证明 48 小时稳定。原基线低水位包含此前同次启动活动；恢复 fast 后的低水位为新启动周期。

三次 receiver_exit=0、unexpected_stop=false、upstream_stall=false，PC 内容/ACK/非法头错误均为 0、单一连接，ESP32 确认帧增量与 PC 完整帧一致。各自停止时丢弃 1 个部分帧，PC incomplete_errors=0；SDIO errors 增量 1 是现有 reap() 对计划取消回调也计 errors 的行为，不是运行中 CRC 错误。不能只凭设备 errors 总数判断出错。

诊断均值（只统计状态轮询实际观察到的唯一完整帧，并非每帧）：

| 指标 | fast 原基线 | large | fast 恢复 |
|---|---:|---:|---:|
| body 时间 | 552.6 ms | 480.6 ms | 565.2 ms |
| 等待写就绪 | 196.9 ms | 110.3 ms | 207.7 ms |
| 内存保护等待次数/帧 | 1.35 | 55.58 | 2.36 |
| 部分写入次数/帧 | 22.13 | 0.37 | 23.42 |
| PSRAM staging copy 时间 | 108.5 ms | 120.8 ms | 107.9 ms |

时间项存在包含/重叠关系，不能相加得到总耗时。大容量减轻发送额度瓶颈，但压力转移至内部 SRAM；发送前 headroom 检查不是跨任务全局预留，Wi-Fi/lwIP 等异步分配仍可降低余量。**5056 B 不适合默认使用**，本次未出现内容错误不等于安全长期运行。因该风险不继续烧录 57600 B，仅完成其构建和主机自检查。若后续要保留大容量，需先做更强的内存预算/背压验证，而不是直接继续放大或取消保护。

已经恢复 fast 23040 B，并完成上述恢复后 60 秒重测；最终设备联网、busy=false、SDIO ready/host_sdio=true、running=false、free_slots=2，不自动发送。默认选择没有修改。

原始证据（同名 `.device.json` 为设备采样和阶段统计）：

- `tcp_large_baseline_23040_60s.jsonl` / `.device.json`
- `tcp_large_46080_60s.jsonl` / `.device.json`
- `tcp_large_restore_23040_60s.jsonl` / `.device.json`

## 构建和固件

指定 ESP-IDF 6.1、esp32c61，large/xlarge 构建均通过。fast/large/xlarge 的 sdkconfig 对比仅上传容量不同；lwIP 队列上限在 build.ninja 确认为 64/128/160。应用二进制均 929776 B，应用分区 1536000 B，余量 606224 B（39%）。large 静态 DIRAM 130057/256624 B（50.68%）；运行时动态堆压力应以板测低水位为准。

自检查：102 个 Python、8 个网页 Node 测试及 C 数据链路/故障注入测试通过；socket 适配层覆盖全部 29 行、全部 38 分支被执行、其中 97.37% 分支方向至少命中一次。未改 RTL、引脚、时钟或约束，因此未重新跑 Vivado 实现/CDC；既有时序/CDC 风险不因本次软件测试而解决。

本次构建应用 SHA256（后续重新构建可能随应用描述信息变化）：

| 文件（绝对路径） | SHA256 |
|---|---|
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-large-socket\ultrasound_psram_udp.bin` | `302F0AEED4AC374F8227135C10C65B88B96BE1360C8CDCC4726E774C331878F9` |
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-xlarge-socket\ultrasound_psram_udp.bin`（未烧录） | `EF934A8B1E95F0DD39831006CBA1515DB5E27BBF86A7A1DF6468C991A5DDEC7A` |
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket\ultrasound_psram_udp.bin`（已恢复、重测） | `4A903CF8905901F5FC9D39FE8220133BCCB8A8442CE501936B3A14AB18291AD4` |
