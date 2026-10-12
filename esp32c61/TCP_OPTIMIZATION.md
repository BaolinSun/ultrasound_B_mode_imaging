# TCP 发送性能优化与分级板测（2026-10-12）

本文件保留第一次提速及高档选择的历史记录。后续上传 socket 独立容量、当前构建目录和进一步板测见 [上传连接专用缓冲](TCP_SOCKET_TUNING.md)。下文历史全局缓冲/路径不要用于判断后续新配置。

## 当前交付与运行

按用户“高档，优先传输速度”的选择，性能配置默认改为**高档 TCP**，使用 COM3 更新。Wi-Fi 配置和 PC 目标保留，Zynq 继续使用原固定 DDR SDIO 应用。默认不自动发送。未修改 Zynq、SDIO 协议、RTL、UART 引脚或 UDP 格式，未重新生成 bit/XSA/BOOT.BIN。当前烧录和复测记录见文末。

在 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61` 运行，先确认 SDIO 已握手、设备空闲、目标是运行接收器的 PC：

```powershell
# 自动监听、启动和停止；每次选新的日志名
python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 60 --log tcp_optimized_check_60s.jsonl

# 单帧：先运行接收器，再点网页“SDIO 单帧”
python .\pc_tools\sdio_tcp_receiver.py --source-ip 192.168.3.27 --test-pattern --output-dir .\tcp_optimized_capture
```

## 性能档位

| TcpTuning | TCP 发送缓冲 | 内部 staging | 构建目录（普通头文件选择） |
|---|---:|---:|---|
| baseline | 5760 B | 5760 B | build-throughput-baseline |
| mid（内存余量回退） | 11520 B | 5760 B | build-throughput-mid |
| fast（性能配置默认） | 23040 B | 5760 B | build-throughput-fast |

TCP MSS 仍为 1440 B，ESP32 接收窗口保持 5760 B。扩大的是**发送**缓冲，不扩大只接收短回执的 RX 窗口。每档独立 sdkconfig，不修改用户根 `sdkconfig`、旧 `build-throughput/` 或另一档配置；已生成配置仍保留用户 menuconfig 设置，网页/API 的 `tcp_send_buffer` 才是实际生效值。

```powershell
# 默认性能高档
.\build.ps1 -Profile throughput
.\build.ps1 -Profile throughput -Action flash -Port COM3
.\build.ps1 -Profile throughput -Action monitor -Port COM3

# 显式高档与默认性能档一致，需持续观察内部 heap
.\build.ps1 -Profile throughput -TcpTuning fast -Action flash -Port COM3

# 分级对照或回退
.\build.ps1 -Profile throughput -TcpTuning baseline
.\build.ps1 -Profile throughput -TcpTuning mid -Action flash -Port COM3
```

`-Profile default` 不强行改变根配置的 TCP 缓冲值，适合保留用户配置，不等于性能高档。要获得本次中/高档参数，请使用对应性能构建，不要把旧 `build/` 固件当作优化档烧录。ESP-IDF shell 下直接重建已生成高档可使用：

```powershell
idf.py -B build-throughput-fast -DSDKCONFIG=build-throughput-fast/sdkconfig build
```

协议仍由 `main/wifi_transport_config.h` 的 C 宏选择，默认 TCP；`-Transport tcp/udp` 只是隔离构建验证覆盖值，对应目录如 `build-throughput-udp-fast`。档位不是运行时网页/串口切换。最大长度、超时及单发送任务规则不变。

## 实际代码变化

1. 分级配置 `CONFIG_LWIP_TCP_SND_BUF_DEFAULT`，性能默认按用户选择改为高档。高档 TX 容量与本地 IDF 6.1 C61 iperf 示例一致，但没有照搬关闭看门狗等实验设置。
2. 正常 `EAGAIN/EWOULDBLOCK` 等待 `select()` 就绪，不再每次额外休眠。`ENOMEM/ENOBUFS` 单独归类为资源等待并保留约 1 ms 以上退让；连续 8 次立即就绪却未推进时也退让，避免忙循环。单次等待仍不超过 100 ms 检查取消、链路及总截止时间，不限制为 UDP 的 100 次重试。
3. 内部 staging 从 4096 改为 5760 B（四个 MSS），可通过 `CONFIG_PSRAM_TCP_STAGE_BYTES` 调整。只增加 1664 B 内部 SRAM，不增加第三份完整帧。UDP 继续使用 staging 前缀，原始数据包大小/行为不变。
4. 按约 64 KiB 已发送数据量让出 CPU，而非把固定块数与 staging 大小绑定；5760 B 时为每 12 块（69120 B）退让。保留 SDIO、Wi-Fi/网页任务运行机会。
5. PC TCP 接收切片上限从 4 KiB 增至 64 KiB，仍正确处理部分读取、EOF、100 ms socket 超时及严格帧头。CRC/测试模式校验、回执、输出有界队列、文件不覆盖规则不变。
6. 新增分阶段耗时、等待原因、部分写入和调用次数；上一帧统计保留到下一帧结束，带 session/frame/result/bytes，避免下一次提交清零后状态采样看不到回执耗时。

TCP 原生网络重传次数与 RTT **没有**在本次统计中测量，不能将 `tcp_send_waits` 当作重传次数。`socket_errno=11` 可表示正常非阻塞等待，不是 CRC 错误。

## 分阶段字段解释

- `tcp_phase`：当前 idle/connect/header/payload/ack。
- `tcp_metric_session/frame/result/bytes`：以下上一帧统计的身份和结果；成功 `received_verified` 意味着 PC 完整校验回执通过。
- `tcp_header_us` / `tcp_payload_us` / `tcp_ack_us`：帧头、载荷、应用回执阶段的墙钟时间，连接另有 `tcp_connect_us`。
- `tcp_write_wait_us` / `tcp_read_wait_us`：socket 等待时间，包含资源退让/防忙循环退让；写等待可发生在帧头或载荷阶段。
- `tcp_yield_us`：主动 CPU 退让，包含资源等待和防忙循环，不可与上述等待简单相加。
- `tcp_copy_us`：PSRAM→staging 复制周围的墙钟时间，包含可能的调度抢占，不是独立的 PSRAM 带宽测试。
- `tcp_write_calls/read_calls/partial_writes` 和 `tcp_send_waits/ack_waits/resource_waits`：调用与等待次数；不是线上 TCP 分段数。

等待、复制及退让是阶段内的重叠子集，不要把全部字段相加当作总耗时。网页显示关键字段；benchmark 的 `.device.json.tcp_phase_summary` 按会话/帧号去重，只汇总状态采样实际看到的成功帧，**不假定采样覆盖所有 PC 有效帧**。

## 软件验证

- 全套 99 项 Python、8 项网页测试通过，C 自检查及原始 UDP、SDIO、UART 桥、配置和主机驱动回归通过。
- TCP 核心行覆盖 100%，分支执行 100%，分支方向覆盖 97%；不代表完整 IDF/无线驱动覆盖率。
- 4096/5760/11520 B staging 的边界、尾块、部分写入、原始字节一致性、超过 100 次正常/资源等待、阶段统计、CPU 退让、拆分/错误回执、断网、取消、IO 错误和超时通过。
- TCP 默认/性能及 UDP 默认/性能交叉构建成功；另构建全部三档。根默认宏仍 TCP，协议覆盖值隔离。
- 4 MiB Flash，应用分区 1536000 B。默认静态 DIRAM 118899/256624 B，性能配置 130861/256624 B；runtime heap 另看板测低水位。

```powershell
.\tests\run_tests.ps1 -Python 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe'
```

## 板测结果

固定 DDR 帧 787456 B，测试模式 CRC `3aa5aebf`，PC 192.168.3.31:50001，ESP32 192.168.3.27。所有档位使用同一优化代码和 5760 B staging。有效吞吐统一为**PC 完整校验通过字节/接收器总运行秒**。Wi-Fi 环境存在波动，顺序测试不是可重复的射频实验室基准。

| 配置 | 时长 | PC 有效帧 | 完整有效吞吐 | 内部堆低水位 |
|---|---:|---:|---:|---:|
| 旧 TCP（历史参考） | 60.89 s | 26 | 2.690 Mbit/s | 63724 B |
| 新 baseline 5760 B | 61.00 s | 28 | 2.892 Mbit/s | 63608 B |
| 新 mid 11520 B | 60.66 s | 57 | 5.920 Mbit/s | 52440 B |
| 新 fast 23040 B 完整重测 | 60.52 s | 105 | 10.930 Mbit/s | 20712 B |
| 此前 mid 复测 | 30.87 s | 26 | 5.305 Mbit/s | 52408 B |

最后代码审查补充了“成功写入/读取即清零立即就绪计数”，使防忙循环退让只针对真正未推进的连续等待。重新完成全配置构建和中档烧录，发布版 20 秒复测（`tcp_opt_release_20s_20261012`）为 21 帧、6.407 Mbit/s，单连接、运行中 CRC/连接/回执错误增量 0。上表分级测量保留原始结果，不用短时发布复测替代 60 秒数据或声称全部收益来自这处修正。

各完整测试均单连接复用，PC 有效帧与设备确认增量一致；运行中 CRC、连接及回执错误增量为 0。停止时各有 1 个 PC 不完整帧丢弃，是计划取消，不计为运行中 CRC/协议故障；SDIO 的既有取消计数可能同时增长，不可把累计值当本轮 TCP 失败。

| 分阶段平均（采样成功帧） | baseline | mid | fast |
|---|---:|---:|---:|
| 载荷发送 | 2117.7 ms | 1030.9 ms | 552.7 ms |
| 写 socket 等待 | 1828.9 ms | 718.9 ms | 189.9 ms |
| 应用回执等待 | 16.5 ms | 14.7 ms | 16.2 ms |
| 主动退让 | 16.8 ms | 17.3 ms | 18.7 ms |
| 复制（含调度抢占） | 73.8 ms | 85.7 ms | 114.0 ms |
| 覆盖的成功帧 | 28/28 | 56/57 | 97/105 |

分级扩大 TX 缓冲后，写等待显著下降，证明小发送缓冲是当前性能的重要限制因素；应用回执并非秒级耗时的主要来源。baseline 对旧版还同时改变了等待策略、staging 和 PC 读取，所以其约 7.5% 提升不能全部归因于移除休眠。

高档约为旧 TCP 的 4.06 倍，已接近此前成功 UDP 重测的 11.68 Mbit/s，但不是同一时间射频条件下的严格 TCP/UDP 优劣证明。高档仅约 20 KiB heap 低水位，尚未证明高并发网页访问、网络重连和长稳压力下安全。用户选择速度优先，故性能默认为高档；中档保留约 51 KiB 余量，可显式回退。不宣称高档长期稳定。

`tcp_opt_acceptance_20261012.json`：在高档完成 8 个场景，包括单帧逐字节校验、ACK 前缓冲所有权、分段回执、下一帧目标端口切换、CRC/会话错误回执、30 秒无回执超时、接收端退出、连接拒绝及手动恢复；全部通过。只实测同一 PC 的两个端口，没有两台 PC IPv4 切换或实际断 Wi-Fi 板测。

此前中档上的原始 UDP 回归 3 帧均通过，每帧 787456 B / 547 包 / mismatch 0，保留旧工具和原始 UDP 接口。未运行 48 小时，长期验收流程仍见 TCP_TRANSPORT.md/SDIO_BRIDGE.md。

### 未解决的上游停帧

高档首次测试 `tcp_opt_fast_60s_20261012` 只接收 7 帧后无新增数据，约 34 秒接收器空闲退出；benchmark 正确标记 `unexpected_stop=true` 并失败。失败前 TCP 空闲、连接仍在、已收帧全部确认、TCP/SDIO CRC 错误增量 0，SDIO running=true 但接收计数不再推进，两个槽位均空闲。该失败不作为正常 60 秒吞吐样本，不能用开头约 4 秒的速度宣称完成连续验收。

显式停止/重新启动后高档重测完成完整时长。这个停帧现象在优化前 TCP/UDP 测试中也出现，本次未修改 SDIO/Zynq 握手去绕过它，具体原因仍未定位。提速成功不等于这个风险已解决；后续应独立采集停帧时的 Zynq UART 和 SDIO 握手状态。

## 构建产物与证据

以下为此前分级验证产物的历史哈希；高档选择后重新构建的实际烧录哈希另列文末。应用绝对路径前缀：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\`，各行目录内文件为 `ultrasound_psram_udp.bin`。

| 目录 | 字节 | SHA256 |
|---|---:|---|
| build-throughput-baseline | 927840 | AD8C72901C924CAAD3837DB4BB8337215D2B41328CF3A9AD2E856E12BE327FF0 |
| build-throughput-mid（此前烧录） | 927856 | A7FFC049E33AD58CF8E6A5B40FA576EB7E4D121ACB573B2BF93F034DA0D5FFB9 |
| build-throughput-fast | 927856 | 92EC77B7944452A042F1E191B95DDD290CCC574422DA1247F36DA927DB3507E4 |
| build-default-tcp-baseline | 926288 | 3150D034F78AEEF5D867BEEC92DDC6074DB5964D0FC05F705938AC1994DEEE07 |
| build-default-udp-baseline | 926192 | 56E236E642208DF464552AF6D718399C979D6A426AD30A2FB1EF6531ED909FD2 |
| build-throughput-udp-mid | 927760 | 7919BD702F82FE5CCCFD742225A77F0FC1CBFD7562484A84CD225E4B1C462FAF |

此前中档应用：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-mid\ultrasound_psram_udp.bin`，地址 0x10000。同目录 `bootloader\bootloader.bin` 地址 0x0，SHA256 `C0738541742E260AA3E64E211FC3742E16A716D453F092157698A3AF7BA63CE3`；`partition_table\partition-table.bin` 地址 0x8000，SHA256 `5295E8C42D6FB5F95E4E8A185932C6191D27CFC5078322475347E40E71AF815A`。NVS 未擦除。

原始证据：`tcp_opt_baseline_60s_20261012`、`tcp_opt_mid_60s_20261012`、`tcp_opt_fast_60s_20261012`（失败）、`tcp_opt_fast_retry_60s_20261012`、`tcp_opt_mid_final_30s_20261012`、`tcp_opt_release_20s_20261012` 对应 `.jsonl/.device.json`；以及 `tcp_opt_acceptance_20261012.json`、`tcp_opt_raw_udp_regression_20261012.jsonl`、`tcp_opt_release_raw_udp_20261012.jsonl`。构建/烧录日志位于 `tests/output/tcp-opt-*.log`，软件回归日志 `tests/tcp_opt_regression_20261012.log`。

实现前功能代码已在 `38cef37` 提交，板测证据基线为 `ade9363`；没有待保存的旧代码改动。用户删除的旧日志不恢复，用户新建的 `tcp_test_60s.*` 和 `sdio_optimized_60s.*` 不混入本次提交。既有 RTL 时序/CDC 风险不因本次 C 软件优化被标记为已解决。

## 用户选择高档后的交付复测

2026-10-12 按用户速度优先选择，将 `build.ps1 -Profile throughput` 的自动档位从 mid 改为 fast；普通 `-Profile default` 仍保留根 sdkconfig，不强行覆盖。COM3 高档固件已烧录且写入哈希验证通过，NVS 未擦除。板上 API 确认 TCP、TX 23040 B、RX 5760 B、staging 5760 B；Zynq 与 SDIO 协议不变。

`tcp_fast_selected_30s_20261012.jsonl` / `.device.json`：30.490 s 收到 51 个完整校验帧，共 40160256 B，完整有效吞吐 **10.537 Mbit/s**，1.673 帧/s；单连接，设备确认增量 51，CRC/连接/回执错误增量 0，`unexpected_stop=false`。内部堆低水位 22636 B（约 22.1 KiB）。计时到停止时丢弃 1 个 PC 部分帧，设备清理 2 个在途/待发槽位，SDIO 取消错误计数增加 1；运行中的状态采样 SDIO 错误为 0，不将计划停止计为运行故障。停止后发送任务空闲、TCP 已关闭、两个槽位均可用。

高档原始 UDP 回归 `tcp_fast_selected_raw_udp_20261012.jsonl`：3 帧均通过，787456 B / 547 包 / mismatch 0。软件全套 99 项 Python、8 项网页及 C 核心自检查通过。构建/烧录/测试日志在 `tests/output/tcp-fast-selected-*.log`。这次短测未出现上游停帧，但不表示已修复前述偶发 SDIO 停帧，也未完成 48 小时稳定性验证。

本次实际烧录输入绝对路径与 SHA256：

| 地址 | 文件 | SHA256 |
|---|---|---|
| 0x0 | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast\bootloader\bootloader.bin` | EA957DD6778D95E900FF2CB573789A30B63F8BC65F2D0C8B462577C762DFB302 |
| 0x8000 | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast\partition_table\partition-table.bin` | 5295E8C42D6FB5F95E4E8A185932C6191D27CFC5078322475347E40E71AF815A |
| 0x10000 | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast\ultrasound_psram_udp.bin` | C0317B4CE7EBCB0F11C9044B41B39E857E1D061F37794A49AD912BFE60775FBC |

应用 927856 B，应用分区 1536000 B，尚余 608144 B。高档默认以速度为优先，内部 heap 余量较小；出现内存压力时可用 `-TcpTuning mid` 显式回退，不在运行时自动降档。
