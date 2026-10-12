# 上传连接专用 TCP 缓冲提速（2026-10-12）

本文件是上传 socket 容量实验的历史记录。后续保持 23040 B、CRC/首块合并/内存背压和停帧诊断的当前实现见 [TCP 数据路径优化](TCP_PIPELINE_OPTIMIZATION.md)。历史应用哈希不要当作最新烧录文件。

## 改动范围

只修改 ESP32 TCP 发送缓冲的容量选择，原始 UDP、SDIO 协议、Zynq 应用和成像 RTL 不变。CRC 应用回执、双 PSRAM 槽位所有权、超时/取消/断网停止与持久连接规则保留。staging 仍为 5760 B，不增加完整帧缓冲，不把 Wi-Fi DMA 转移到 PSRAM。

此前配置 `CONFIG_LWIP_TCP_SND_BUF_DEFAULT=23040` 对所有新 TCP 连接生效。新配置将普通 TCP（网页）默认容量保持 5760 B，通过 `CONFIG_PSRAM_TCP_SEND_BYTES` 只对数据上传 socket 指定更大容量。新增实验档 faster=28800 B，保留 fast=23040 B、mid=11520 B、baseline=5760 B 作对照和回退。

## IDF 6.1 适配边界

本机 IDF Kconfig/在线文档提到 `TCP_SNDBUF`，但安装源码 `sockets.c` / `sockets.h` 没有实现该选项，不能假定 setsockopt 会成功。项目内 `main/tcp_socket_tuning.c` 使用 IDF 6.1 lwIP 的私有接口；显式限制 IDF 主/次版本为 6.1，升级 SDK 时必须复核。

- 上传任务独占刚创建的 socket，尚未 connect/send；没有并发关闭者。
- `tcpip_api_call()` 同步串行进入 TCP/IP 线程或核心锁，再获取该 socket 的 PCB。
- 检查 TCP 类型、CLOSED、netconn 空闲、无 unsent/unacked/队列且初始 snd_buf 未使用；否则拒绝，不操作运行中的连接。
- 设置单 PCB 的 snd_buf，并回读确认；将 CLOSED PCB 的初始 ssthresh 与该容量匹配（与原 tcp_new 按发送容量初始化的语义一致），不强制 cwnd、不改变运行中拥塞控制或重传算法。适配失败终止本轮上传，走现有单次回调/连接关闭/槽位清理，不悄悄回退或重放。
- 项目 CMake 为 lwIP 和应用适配层一致设置 `TCP_SND_QUEUELEN` 上限，按较大的上传/普通容量计算；28800 B、MSS 1440 时为 80。它不是预分配，普通网页 snd_buf 仍限制为 5760 B。不能只扩大 snd_buf 而保留不足的队列上限。
- 不修改 `D:\Program Files\esp\v6.1\esp-idf`，不依赖文档中未实现的 socket 选项，也不从普通任务直接写 PCB。

官方配置说明：[IDF 6.1 TCP 发送缓冲](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32c61/api-reference/kconfig-reference.html#config-lwip-tcp-snd-buf-default)。适配具体行为以本机安装源码与项目测试为准。

## 构建与运行

各档使用全新 `*-socket` 构建目录，避开旧生成 sdkconfig 中的全局大缓冲；不覆盖根配置和历史固件。已生成的新 sdkconfig 仍保留 menuconfig 改动，API 的实际值优先于 defaults 文件。

`-Profile throughput` 自动选择 fast（23040 B），保持此前用户选择的默认容量；faster（28800 B）显式实验选项，不自动升档。

```powershell
# 28800 B 上传专用档
.\build.ps1 -Profile throughput -TcpTuning faster -Action flash -Port COM3
# 回退原 23040 B 容量，但仍仅对上传连接生效
.\build.ps1 -Profile throughput -TcpTuning fast -Action flash -Port COM3

# 自动监听、启动、停止；使用新日志名
python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 60 --log tcp_socket_check_60s.jsonl
```

状态字段：`tcp_send_buffer` 为上传配置容量，`tcp_send_buffer_actual` 为最近一次新 socket 成功设置后的回读容量（首次连接前为 0），`tcp_default_send_buffer` 为其他新 TCP 连接的默认容量。关闭连接后保留最近回读值，不将其解释为当前仍有连接。

## 验证与风险

连接适配自检查覆盖参数/空 socket/错误类型、已连接/已使用 PCB 拒绝、核心调用失败、队列不足、各档容量回读及另一网页 PCB 不变；运行真实适配源而非重新实现逻辑。软件回归不代替无线/板级稳定性测试。

28800 B 尚需按完整有效吞吐、内部堆低水位、网页并发和断网恢复决定是否值得默认采用。不直接跃升到 34560/46080/65535 B。较小网页默认缓冲不是预先释放固定 17280 B 内存：lwIP 动态按实际数据分配，收益取决于网页发送量及并发。

此前偶发 SDIO 上游停帧未定位，本改动不视为修复。未运行 48 小时，不承诺大缓冲必然更快或长期稳定。

## 初始 28800 B 板测失败证据

`tcp_socket_28800_60s_20261012` 与 `tcp_socket_28800_retry_60s_20261012` 均未完成计划的 60 秒；benchmark 退出 1、`unexpected_stop=true`。分别收到 25 帧、9 帧，已收帧全部 CRC/测试模式校验和应用回执通过，设备实际容量回读 28800 B；运行停帧时 TCP 空闲/无失败、SDIO running/host=true、两个槽位空闲，SDIO 接收计数不推进。第一次内部堆低水位 13456 B（约 13.1 KiB），第二次同次启动累计低水位相同，不能视为独立峰值。

25 帧/49.24 秒有效吞吐 3.20 Mbit/s、9 帧/35.50 秒 1.60 Mbit/s，包含停帧等待，不能视为完整持续性能基准；更不能只用前几秒活跃速率宣称优化成功。这两轮是初始适配版本，尚未将 CLOSED PCB 的初始 ssthresh 从普通默认同步到上传容量，最终源已补充该初始化并再次测试。因此不把这些结果当作最终实现的定量对照，也不能凭这次 TCP 配置变化确定上游停帧根因。

保留所有失败日志，默认暂时仍为 23040 B；不继续增至 34560 B，不修改 SDIO/Zynq 握手或隐式重启来掩盖停帧。

## 最终实现验证与交付

最终源的软件回归：100 项 Python、8 项网页和 C 核心自检查通过；新增 socket 适配源行覆盖 100%、分支执行 100%、分支方向 97.37%（覆盖仅指适配层，不代表真实 lwIP/驱动覆盖）。IDF 6.1 完成 TCP/UDP × 默认/性能配置构建，性能目录分别验证 fast 和 faster。所有应用分区检查通过；最终 fast 应用 928448 B，小于 1536000 B 分区。

最终 fast（23040 B）`tcp_socket_acceptance_20261012.json` 的 8 个板测场景通过：逐字节单帧、ACK 前槽位不可复用、分段回执、下一帧目标端口切换、CRC/会话错误回执、30 秒无 ACK、接收端断开、连接拒绝及手动恢复。测试后恢复原 PC 192.168.3.31:50001；没有实测两台 PC IPv4 切换、实际 Wi-Fi 断开或多浏览器并发压力。

最终版本计划 60 秒的连续测试均失败，不能宣称完成持续吞吐验证：

| 实际容量 | 原始日志前缀 | 完整帧 | 实际接收器运行时间 | 包含停帧等待的有效吞吐 | 内部堆低水位 |
|---|---|---:|---:|---:|---:|
| 23040 B | tcp_socket_23040_60s_20261012 | 18 | 40.321 s | 2.812 Mbit/s | 20412 B |
| 28800 B | tcp_socket_28800_final_60s_20261012 | 29 | 45.774 s | 3.991 Mbit/s | 10488 B |

两轮均 `unexpected_stop=true`、benchmark 退出 1，PC 已收帧与设备确认增量一致、CRC/连接/回执错误增量 0，无 PC 部分帧。停帧期间 TCP idle、连接仍在、tcp_failed=false，SDIO running/host=true、两个槽位空闲，接收计数不推进；接收器随后空闲退出，benchmark 停止设备。只表示与此前上游停帧特征相似，未采到 Zynq 停帧串口证据，不声称根因已证明。

活跃数据片段约 11.411 / 11.858 Mbit/s，不是完整连续有效吞吐，不能凭短片段的差距决定持续提速成功。最终 28800 B 内部余量仅约 10.2 KiB，还可能受到无线/网页瞬态压力。因此恢复 COM3 的 fast 上传专用 23040 B，默认不自动发送；faster 28800 B 仅保留显式实验选项。不增加到 34560 B，不以 CPU/内存风险换取未经验证的速率。

最终默认烧录输入（NVS 不擦除）：

| 地址 | 绝对路径 | SHA256 |
|---|---|---|
| 0x0 | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket\bootloader\bootloader.bin` | F3AB58D5E1E88D6478A3B4BA620324F2DB90869075270E30D5C0D09B89A499AC |
| 0x8000 | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket\partition_table\partition-table.bin` | 5295E8C42D6FB5F95E4E8A185932C6191D27CFC5078322475347E40E71AF815A |
| 0x10000 | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket\ultrasound_psram_udp.bin` | 51E77A25D00D620C83654EAAD08E1F03BF840EFA3A6EEEB6C1DF8B55BD9B3A80 |

最终实验档应用 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-faster-socket\ultrasound_psram_udp.bin`，SHA256 `50B24077254F2A439230AC4FFC91955AF38CF2BAFB40D3A8A3B227723F8A87A1`。测试日志见上述前缀 `.jsonl/.device.json` 和 `tests/output/tcp-socket-*.log`。提交前代码基线已保存为 `3820c80`；本次不恢复用户删除的旧日志，也不提交用户新增的其他测试报告。

恢复最终 fast 固件后的原始 UDP 回归：首次严格检查收到 770400 B / 535 个接受包，尾包在预期完整长度前到达，判为坏帧并退出；保留 `tcp_socket_raw_udp_20261012.jsonl`，不掩盖此次缺包/不完整接收。一次独立有界重测 `tcp_socket_raw_udp_retry_20261012.jsonl` 的 3 帧均为 787456 B / 547 包 / mismatch 0。未抓包区分网络与 PC 接收侧的具体丢失位置。最终设备 Wi-Fi 已连接、发送空闲、SDIO stopped/ready/host_sdio=true、两个槽位可用。
