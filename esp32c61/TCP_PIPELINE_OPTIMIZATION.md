# TCP 数据路径优化、内存背压与停帧诊断

## 当前配置与简短命令

COM3 使用 TCP 性能 fast 档：上传专用发送容量 23040 B、网页默认 5760 B、staging 5760 B，内部堆发送前 headroom 16384 B。默认不自动发送。保留原始 UDP 自测、C 宏 TCP/UDP 选择、USB 配置、双 PSRAM 槽位以及 PC CRC 回执；未修改 UDP/TCP 包格式、SDIO 共享寄存器协议、引脚、时钟、RTL、bit 或 XSA。

在 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61` 运行：

```powershell
python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 60 --log tcp_checked_60s.jsonl
```

工具先监听、再启动，最后停止。新日志名不能覆盖已有文件。新默认 `--stall-timeout 5`：设备仍 armed/handshaken、两个槽位空闲且发送任务连续空闲 5 秒，判为上游停帧并返回失败；不自动重放或假装正常完成。TCP 正常发送/等待 ACK 或两槽位背压不会触发该条件。需要特殊业务间隔时可显式设置 1..120 秒，本固定 DDR 测试不建议放宽来掩盖停帧。

## 实际修改

1. CRC 使用四字节 slicing-by-4 候选，4 KiB 内部 SRAM 表，只在启动对照 ROM 和独立参考算法校验通过且更快时保留。分配失败或 ROM 更快时使用 ROM、释放候选表，不增加整帧缓冲。保留 IEEE/zlib 增量语义、非对齐及短尾支持。本板启动 32 KiB 实测 ROM 1973 us、slice4 1319 us，约减少 33% 时间；不是整个系统吞吐提升 33%。
2. TCP 把 24 B 帧头和首段 5736 B 载荷放在同一 staging 中提交，随后仍按 5760 B 搬运。首块的部分写入按实际越过帧头的字节计数，失败/取消不会把帧头误计为载荷。PC 仍收到完全相同的“一份帧头+完整载荷”字节流，ACK 后才释放槽位。`tcp_header_us` 现在包含首段载荷发送，`tcp_payload_us` 为剩余载荷阶段；不可与旧版本的纯帧头/全部载荷阶段直接逐项等价比较。
3. TCP 入队前检查内部 8-bit heap，要求至少 headroom + 两倍本次写入量；不足时走资源背压、让出 CPU，仍受 30 秒总期限和停止/断网检查约束。`tcp_heap_guard_waits` 为最近一帧主动内存背压次数，包含在 resource waits 中，不是网络重传。此策略不是针对其他任务的硬内存预留，不能保证堆永远不低于 16 KiB。
4. ESP32 的 SDIO 每帧 INFO 日志降为 DEBUG，减少在数据任务/控制锁内等待 USB 控制台的机会；Zynq 原 FRAME 日志保留。不能据此证明 USB 日志就是历史停帧的根因。
5. `/api/status.sdio` 增加接收槽位/偏移/帧号、窗口数量与已收数量、grant/window_ack、cmd_seq/cmd_ack、command/result/register_status、距最近接收推进的时间。只读采样，不改变握手协议。
6. Zynq 长时间未推进时每 5 秒最多打印一条 `@SDIO WAIT`，包含主机 phase、frame、offset、session、状态、命令及窗口确认。phase 0/1/2/3 对应 IDLE/BEGIN_WAIT/WINDOW/DONE_WAIT；诊断读不会改状态、自动重放或复位。规范源与当前 Vitis 源同步，正常 Vitis GUI 构建包含改动。
7. 构建脚本在组件 Kconfig 比生成头更新时重新配置，避免旧 Ninja 目录遗漏新增宏；不替换用户 sdkconfig。

## 软件及板级验证

102 项 Python、8 项网页测试及 C 自检查通过，覆盖首块边界、帧头内部及跨越帧头的失败/部分写入、不同 staging、原始字节一致性、停止/断网/超时、CRC 非对齐/短尾/增量分割/分配失败/表损坏、内存准入阈值及整数边界、停帧工具完整停止流程。保留既有网页、NVS、UART、SDIO 与 USB 回归。

使用 IDF 6.1 完成 TCP/UDP × 默认/性能配置构建。当前 Vitis 2026.1 SDIO 应用真实编译/链接成功并通过 JTAG 下载；独立 `build/tcp_pipeline_usb` 使用相同 BSP 实际构建 USB 宏模式，不改变或下载当前 SDIO 配置。旧 USB 代码的符号/有符号比较等警告仍存在，不标记为已消除。

所有测速按 PC 完整校验字节 / 接收器总运行时间。顺序无线测试，不是同一射频条件的严格 A/B。

| 实测 | PC 完整帧 | 总时间 | 完整有效吞吐 | 内部堆低水位 |
|---|---:|---:|---:|---:|
| 修改前干净基线 `tcp_pipeline_probe` | 179 | 100.885 s | 11.177 Mbit/s | 21592 B |
| 首轮 CRC/合并首块、无主动内存背压 `tcp_pipeline_optimized_120s` | 220 | 120.636 s | 11.488 Mbit/s | 9744 B |
| 最终带内存背压 `tcp_pipeline_guard_120s` | 219 | 120.731 s | 11.427 Mbit/s | 19792 B |
| 最终重复 `tcp_pipeline_guard_repeat_180s` | 320 | 180.896 s | 11.144 Mbit/s | 18416 B（同启动累计） |

最终两轮均未检测到停帧，CRC/连接/回执错误增量 0，单连接复用。120 秒停止时 PC 最后一帧已完整校验并发送 ACK，但设备优先处理停止、未确认该 ACK，PC 219 / 设备确认增量 218；这是停止边界，不把它列为运行中 CRC 错误。180 秒 PC 320 / 设备确认增量 320，计划停止时 PC 丢弃 1 个部分帧；设备在途/待发槽位取消计数单独保留。原始 `.jsonl/.device.json` 不删减。

`tcp_pipeline_baseline` 的旧工具 60 秒结果虽然显示通过，最后约 19 秒无新帧（75 帧/7.789 Mbit/s），属于原工具未及时发现停帧，**不作为干净提速基线**。新工具自检查确保即使接收器 summary 显示 passed，也能将上游停帧判失败。

CRC 的局部优化有效，但整体吞吐仍约 11.1..11.4 Mbit/s，未证明显著提升：相对干净 100 秒基线，120 秒约高 2.2%、180 秒基本相当，差异可能包含无线波动。内存背压恢复了余量，未明显牺牲实测吞吐。复制/等待计时含调度抢占且互相重叠，不将全部耗时相加。

`tcp_pipeline_acceptance.json`：8 个板测场景全部通过，含完整单帧、ACK 前槽位所有权、拆分回执、下一帧目标端口切换、错误回执、30 秒 ACK 超时、接收端断开、连接拒绝和手动恢复；测试后目标恢复 192.168.3.31:50001。`tcp_pipeline_raw_udp.jsonl` 的 3 帧原始 UDP 回归通过，787456 B/547 包/mismatch 0。

未完成 48 小时验证、两台 PC 切换、实际 Wi-Fi 拔断或多网页并发压力。120+180 秒无停帧不能证明历史停帧已根治；根因未定位，不用自动复位/重连掩盖。

## Zynq BOOT.BIN 与打包输入

只是重建诊断应用，未重新生成硬件。新 BOOT.BIN 已打包但没有替用户写入 SD 卡。用户 Vitis 下载路径：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf`，与下面打包应用相同。

| 输入绝对路径 | SHA256 |
|---|---|
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\zynq_fsbl.elf` | A71159C76FA6ACBBF5EBC9EDB4D58AABE21B3F4A1A81E67C98C69D4AED0C74C4 |
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.bit` | 29B2A863B48FD3C284AA89CD69B90D42372C7AD9E0A85CE74274FADAE5C3E939 |
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging.elf` | B4C99AF78473AC7D793899DEE258FA475858C9CC1CFCB4482B6EB40DE9317AE1 |

BOOT.BIN：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\BOOT.BIN`，SHA256 `C212B217572CCD861506C9D12A2263FC2A557643BCDF895A741041540D07BDEE`；顺序 FSBL → 统一 bit → 应用，清单为同目录 `boot_inputs.sha256`。DDR 测试区 0x00151940..0x00211D3F、长度 0xC0400，由链接器分配，不在 PL 0x20000000 区域；USB 宏构建不分配该整帧数组。既有时序/CDC 风险未因软件改动标记已解决。

## ESP32 最终烧录产物

最终源重建并烧录后 `tcp_pipeline_release_20s`：35 帧完整校验成功，20.941 s / 10.529 Mbit/s，设备确认 35，CRC/连接/回执错误增量 0，内部堆低水位 23148 B；计划停止时 1 个 PC 部分帧丢弃。短测包含启停开销，不用它替代 120/180 秒结果。

| 地址 | 绝对路径 | SHA256 |
|---|---|---|
| 0x0 | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket\bootloader\bootloader.bin` | F3AB58D5E1E88D6478A3B4BA620324F2DB90869075270E30D5C0D09B89A499AC |
| 0x8000 | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket\partition_table\partition-table.bin` | 5295E8C42D6FB5F95E4E8A185932C6191D27CFC5078322475347E40E71AF815A |
| 0x10000 | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket\ultrasound_psram_udp.bin` | DAE707FA5EC2C6408657C3492110646209FE7F7ED1205CC97EE51166C67FD289 |

应用 929776 B，小于 1536000 B 分区，NVS 未擦除。构建/回归日志在 `tests/output/tcp-pipeline-*.log`、`build/reports/tcp-pipeline-*.log`；原始板测证据使用本文列出的 `.jsonl/.device.json`，console 日志只在本地保留（可能含旧缓存启动信息）。修改前功能代码已保存为 `53b9a41`，用户历史日志删除及无关新报告不混入提交。
