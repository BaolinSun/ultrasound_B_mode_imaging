# SDIO 帧 TCP/UDP 切换与测试（2026-10-12）

## 当前可直接运行

板上 COM3 已恢复为 TCP 性能固件，ESP32 为 `192.168.3.27`，PC 目标已恢复为 `192.168.3.31:50001`。使用现有 Zynq SDIO 应用，不需要更新 BOOT.BIN。默认不自动启动测试。

在 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61` 运行：

```powershell
# 单帧：先启动接收，再点网页“SDIO 单帧”
python .\pc_tools\sdio_tcp_receiver.py --source-ip 192.168.3.27 --test-pattern --output-dir .\tcp_capture

# 连续 60 秒：自动监听、启动和停止，无需点击网页
python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 60 --log tcp_test_60s.jsonl
```

每次 benchmark 使用新的日志名；不覆盖既有日志。单帧文件只包含 787456 字节原始数据，名称包含 session/frame，已有文件不覆盖。接收器只有通过长度、CRC 和已启用的测试模式校验的帧才能输出。

仅监听连续流、不自动控制设备：

```powershell
python .\pc_tools\sdio_tcp_receiver.py --source-ip 192.168.3.27 --frames 0 --test-pattern --log tcp_stream.jsonl
```

随后网页启动连续测试；结束时先停止设备，再退出接收器。接收器异常退出会使设备停止本轮，需要重新启动。

## C 宏选择

修改 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\main\wifi_transport_config.h`：

```c
#define WIFI_FRAME_UDP 0
#define WIFI_FRAME_TCP 1
#ifndef WIFI_FRAME_TRANSPORT
#define WIFI_FRAME_TRANSPORT WIFI_FRAME_TCP
#endif
```

将选择行改为 `WIFI_FRAME_UDP` 即使用带头 UDP，重新构建并烧录 ESP32 后生效。非法值编译失败。不存在网页、NVS 或串口运行时切换；Zynq 的 USB/SDIO 输出宏与本宏独立。本次未修改 Zynq、SDIO 协议或 RTL，未重新生成 bit/XSA/BOOT.BIN，既有时序/CDC 风险不因此关闭。

```powershell
.\build.ps1 -Profile throughput
.\build.ps1 -Profile throughput -Action flash -Port COM3
.\build.ps1 -Profile throughput -Action monitor -Port COM3
```

正常构建不传 `-Transport`，它会清除构建缓存中的验证覆盖值，始终按 C 头文件选择。仅用于软件交叉验证时可传 `-Transport tcp` 或 `-Transport udp`，产生独立 `build-default-tcp/udp`、`build-throughput-tcp/udp`，不会污染正常 `build/`、`build-throughput/` 配置。

选择 UDP 时的单帧接收：

```powershell
python .\pc_tools\sdio_udp_receiver.py --source-ip 192.168.3.27 --test-pattern --output-dir .\udp_capture
```

`sdio_benchmark.py` 会通过 `/api/status.sdio_transport` 自动选工具，TCP/UDP 使用相同测试数据、校验和时长口径。首次缺少该字段的旧固件按 UDP 兼容。工具会先告知接收器计划停止（QUIESCE），保持 socket 开放，然后停止设备、等待发送任务退出，再关闭接收器（STOP）。提前空闲退出或设备提前停止都使 benchmark 失败，不能拿部分运行当完整连续测试。

原始自测仍使用 `raw_udp_receiver.py` / `soak_test.py`，只对应网页“上传一次 PSRAM 自测数据”，不要用 TCP 接收工具接它。

## 协议与所有权

TCP 由 ESP32 主动连接配置的 PC IPv4/端口，PC 监听。连续帧复用一个连接，单帧接收器回执后可以正常退出。TCP 连接建立最多等待 5 秒；从请求接受起，发送与应用回执共享 30 秒总截止时间。非阻塞 socket 轮询最多 100 ms 检查取消/断网，部分写入和分段回执按精确字节数处理。暂时背压不受 UDP 的 100 次重试上限限制。

帧头为 24 字节，网络字节序，对应 Python `!IBBHIIII`：

`magic:u32="UBST", version:u8=1, header_len:u8=24, flags:u16=0, session_id:u32, frame_id:u32, frame_bytes:u32, frame_crc32:u32`

紧接完整帧载荷，不添加 UDP 分包头。帧长默认 787456，上限 1 MiB，必须与接收器 `--length` 相符。非法头部关闭连接，不从载荷搜索 magic 来猜测恢复同步。

回执为 20 字节、网络字节序，对应 Python `!IBBHIII`：

`magic:u32="UBSA", version:u8=1, header_len:u8=20, status:u16, session_id:u32, frame_id:u32, actual_crc32:u32`

status 0=通过，1=CRC 不匹配，2=测试模式不匹配。ESP32 必须匹配版本、长度、会话、帧号、status 和 CRC；成功才完成回调、释放 SENDING 槽位并增加确认计数。PC 回执代表内存中完整收帧和校验通过，不代表文件已经持久保存。

CRC32 使用 IEEE/zlib 算法。测试数据每个 32 位小端字为 `i XOR 0xA5C30000`，本帧 CRC 为 `3aa5aebf`。

两个 PSRAM 槽位及内部 SDIO DMA 缓冲继续复用，不增加第三份整帧缓冲。TCP staging 为内部 SRAM 4096 字节，每 16 块让出 CPU；原始 UDP 仍使用同块 staging 的前 1440 字节。`psram_frame_submit()` 使用编译选择；原 `psram_udp_submit()` / `psram_udp_submit_frame()` 始终 UDP。

socket 由唯一发送任务建立/关闭。停止也关闭空闲持久连接。连续测试中连接故障、错误回执或超时会停止本轮、清理未完成/待发送帧；不自动重连或重放。恢复连接后必须由用户重新启动。目标更新在下一次帧提交生效：先完成旧帧回执，再关闭旧连接并连接新目标。

网页显示编译模式、当前上传协议、连接状态、确认帧数、连接/回执错误和耗时。TCP 成功为 `received_verified`（PC 校验通过），UDP 为 `sent_local`（本地发送完成）；本地发送字节进度满并不代表 TCP 已获得回执。`tcp_chunks` 是内部 staging 块数，`tcp_waits` 是等待次数，都不是 TCP 线上的分段/重传数。

## 已完成的软件验证

- ESP-IDF v6.1，esp32c61，TCP/UDP × default/throughput 四种构建成功；正常不覆盖宏的性能构建也成功。
- 96 项 Python 测试、8 项网页测试通过；旧 UDP、SDIO、UART 日志桥、配置与主机协议自检查回归通过。
- TCP 发送核心覆盖部分写入/拆分读取、完整原始字节、4 KiB 边界、超过 100 次背压、所有回执字节错误、EOF/IO 错误、取消、断网、发送/回执超时和截止边界。该核心行覆盖 100%，分支执行 100%，分支方向覆盖 97.44%。这些指标不代表完整 ESP-IDF 平台层或无线驱动覆盖率。
- PC 实际 CLI 测试验证合法输出、CRC/模式错误、坏头、部分帧、重启连接、源过滤、停止及有界输出；错误帧不保存，ACK 不是磁盘写入保证。

主机全回归命令：

```powershell
.\tests\run_tests.ps1 -Python 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe'
```

四种固件应用分区均为 1536000 B，Flash 4 MiB。默认静态 DIRAM 使用 118803/256624 B，性能配置 130765/256624 B（约 51%）。这里是链接静态占用，不含运行时动态 heap/PSRAM 分配；板测低水位另列。

| 配置/目录 | 应用 bin 字节 | SHA256 |
|---|---:|---|
| build-default-tcp | 923328 | 5CA93ABCD4996E8E3E622B52F84DBFEB11E3F7E4B13F55F895ACEAAB1846708D |
| build-default-udp | 923232 | 325159AAC00634282BEE70F5A3D47201516A5308F1CC72C25BA5A2F8B108F0A3 |
| build-throughput-tcp | 924880 | 95E7C2D83C996C58F0658256672386FFACCDFE86372CD1292E8CD0DAEC1A6A18 |
| build-throughput-udp | 924784 | 20C192CC081794AED19BA4992E6D472B458B2CC35C324D53A647619236E07DAE |
| build-throughput（正常头文件选择，已烧录） | 924880 | D578952041F3B88AFD68ED3592CD580F992DF9E71D82780781832E11DEB8B405 |

应用绝对路径为 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\上述目录\ultrasound_psram_udp.bin`；相同源码在独立目录构建的 ELF/app 描述可能不同，bin 哈希无需相同。实际烧录的应用为 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput\ultrasound_psram_udp.bin`，脚本同时烧录该目录的 `bootloader\bootloader.bin` 和 `partition_table\partition-table.bin`，保留 NVS。

## 实际板测（与软件自检查分开）

固定 DDR 787456 B，现有 SDIO 应用，ESP32 192.168.3.27 → PC 192.168.3.31:50001，同一 Wi-Fi 环境。只启用测试链路，未启动成像或高压。COM3 烧录已取得本次授权。

`tcp_board_acceptance_final_20261012.json`：8 个场景通过，包含拆分 ACK、回执前保持缓冲所有权、下一帧切换目标端口、PC 拒绝回执、错误会话 ACK、30 秒不回执超时、接收端中途退出、连接拒绝及后续手动启动恢复。单帧逐字节等于测试模式。目标切换只实测同一 PC 的两个端口，不能当作“两台 PC IPv4 切换”验收；两台 PC、实际 Wi-Fi 断网和 48 小时测试尚未执行。

最终审查另修正了空闲连接关闭与停止标志检查的一处竞态，并重新完成四配置构建、烧录。修正后的发布复查见 `tcp_release_acceptance_20261012.json/.log`：单帧通过，但下一场景等待连接 6 秒超时，清理后仅 1 个接收/确认帧、TCP 连接及回执错误为 0。该轮整体**没有通过**，不能用先前 8 场景通过覆盖这个失败。随后设备重新就绪后的 20 秒复测通过（`tcp_release_20s_20261012.jsonl/.device.json`），8 帧、2.451 Mbit/s、1 个计划取消帧、运行中 CRC/ACK/不完整错误均 0、单连接，说明能够恢复运行，不证明偶发停帧风险已解决。

| 60 秒测试 | PC 有效帧 | 有效吞吐（完整校验字节/总接收运行秒） | 接收端丢弃 | 内部 heap 低水位 |
|---|---:|---:|---|---:|
| TCP 首轮 | 26 | 2.690 Mbit/s | 1 个结束时取消帧，运行中 CRC/ACK/不完整错误均 0 | 63724 B |
| TCP 最终复测 | 24 | 2.502 Mbit/s | 1 个计划停止时取消帧，运行中 CRC/ACK/不完整错误均 0 | 65492 B |
| UDP 完整时长重测 | 114 | 11.685 Mbit/s | 19 帧，含 1 个结束时未完成帧；观察丢帧率 14.29% | 24316 B |

TCP 两轮均只有一个连接，PC 有效帧数与设备确认计数增量一致，正常停止不增加连接/回执错误。SDIO 的既有统计会将发送取消记录为错误/丢弃，必须与故障注入前的计数作差，不能把累计计数当本次 TCP CRC 故障。最终复测停止后两槽均释放。

最终 TCP 完整帧接收阶段平均 2.415 s、中位约 2.390 s；UDP 成功帧平均 0.404 s。最终 TCP 状态的最近 SDIO 接收 247.187 ms（CRC 累计 65.307 ms）；这些是各阶段耗时，不是 PHY 速率，也不是端到端采集时延。测试前存在故障注入计数，TCP 最终轮确认增量恰为 24，连接/回执错误增量均 0。

UDP 首次对照约 11 秒后不再出帧，接收端在约 41 秒空闲退出，ESP32 仍显示 running，完整帧接收计数停在 23；该轮不作为 60 秒性能样本。修正 benchmark 以拒绝提前退出后，显式停止/重新启动的第二轮完成了完整时长。首次链路停止推进的具体原因未证实，本次不改变 SDIO/Zynq 协议掩盖它，需后续结合 Zynq 串口日志定位。

当前 TCP **更完整，但没有提速**，约为成功 UDP 重测有效速率的 21–23%。维持原 TCP MSS 1440、发送缓冲和接收窗口 5760 B，没有盲目扩大内存。小发送缓冲、ACK/往返时延等是后续调优候选，尚未用窗口/RTT 专项 A/B 证明瓶颈，不把推测写成已确认结论。

原始 PSRAM UDP 在最终 TCP 固件下额外回归 3 帧全部通过：每帧 787456 B、547 包、mismatch=0，单帧接收阶段约 24.33–27.10 Mbit/s。该速度不包含 SDIO 和测试循环开销，不能与上表的全链路有效吞吐直接比较。

原始证据：`tcp_final_60s_20261012.jsonl/.device.json`、`tcp_60s_20261012.jsonl/.device.json`、`udp_compare_60s_retry_20261012.jsonl/.device.json`、`tcp_board_acceptance_final_20261012.json`、`raw_udp_tcp_regression_20261012.jsonl`。本次产生的构建/烧录及自检查日志保留在工程内；用户此前删除的日志未恢复，用户新日志未混入功能提交。

交付状态为功能实现及软件验证完成、短时板测有成功也有未定位的上游停帧失败，**不是板级稳定性验收完成**。下一步应在不出帧发生时同步采集 Zynq UART 与 SDIO 状态，定位链路停在哪个握手阶段；本次没有修改 Zynq/SDIO 协议绕过问题。

## 后续板级验收与 48 小时流程

1. 两台 PC 同时监听 TCP（不做第二台主机可用的假设），连续运行时网页修改目标 IPv4；旧帧 ACK 后下一帧到新目标。记录 session/frame，禁止旧目标半帧作为有效数据。
2. 连续运行中实际断 Wi-Fi、结束接收程序及停止按钮；确认设备停止、当前帧丢弃、两槽归还。网络恢复/接收器重启后由用户重新启动，不自动重放。
3. 用新日志名执行 48 轮各 1 小时测试，每轮显式重新启动，不自动重试失败轮；每轮检查确认帧增量、CRC/连接/回执错误增量、heap 低水位、帧耗时与 PC 有效吞吐。任何非计划断开或挂死均不通过验收。

```powershell
1..48 | ForEach-Object {
  python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 3600 --log "tcp_48h_$_.jsonl"
  if ($LASTEXITCODE -ne 0) { throw "第 $_ 轮失败，检查日志；不重放" }
  Start-Sleep -Seconds 2
}
```

以上长稳流程仅交付步骤，未执行，不以短测承诺 48 小时稳定。
