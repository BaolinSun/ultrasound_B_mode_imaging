# 扫描后下采样配置 PAUSE 排空修复

## 检查点与故障现场

修改前全部现有代码/文档已按用户要求提交为 `76d5aee`：
`chore: checkpoint DDR frame store, temperature charts and web decimation`。
本轮修复在该提交之后，不把检查点提交冒充修复已提交。

用户报告：上电未开始扫描时首次配置成功，扫描后配置返回 `DDR_DRAIN_OR_ABI`。
现场 VERSION=0x55424631、FLAGS=0xD、ACK=0x58，内部请求/待完成序号=0x59、命令=4，
诊断 AXI=0x2E101、解析/AXI 状态=0。解码为已初始化、已暂停、FIFO 空、无活动帧或在途
AXI 写，但输入 busy=1、maintenance=1，未进入双域 FIFO 清理。
输入快照0x201、年龄26周期，支持采样域在该次读取附近有活动，不支持直接认定停钟。
命令和请求的写入地址没有有效回读，读到0不代表内部命令/序号为0。

已确认卡点是 PL 输入忙状态；现场寄存器不能进一步区分原 capture_busy 的四个来源。
源码和自检查可确定旧 tail_pending 存在缺陷：它在 BF 窗口**每个高电平周期**置位，
且置位优先于 log_last。最后输出完成若发生在 BF 窗口结束前/同周期，会被覆盖或再次置位，
随后没有新 log_last 就永久保持忙。仿真已复现此机制，但不能替代用户板上根因复现和修复验收。

## 修改内容

新增 `capture_drain_tracker`，不改图像样本、线头、AFE/MMCM、扫描四次物理收发组成一条
逻辑线的规则，亦不修改 ila_bf 探针：

- BF 窗口上升沿登记一次物理事务，add_rf_head 的 trans_last 完成该事务。
- 在 PI 第3相位的首个线头登记一次逻辑线，log_last 完成该逻辑线。
- 四次物理事务对应一次逻辑输出，分别计数，不能把每次物理 BF 使能当成一次图像输出。
- 处理使能改为 `scan_active | capture_busy`：物理扫描可以结束，但已接受的 RF/PI 尾部
  继续处理到最后完成事件。原先直接用 scan_active 门控 apt_add 会提前复位 PI FIFO，
  可能丢失末尾输出和完成信号；仅改忙计数不能修复这个问题。capture_busy 为同域寄存输出，
  不形成组合反馈，不产生新的物理发射，也不改变运行中的采样或 PI 算术。
- 扫描活动、BF 窗口、物理在途、逻辑在途及寄存后的实际输出任一存在就保持忙。
  完成先于窗口结束不会重新产生事务；多个事务交叠及完成/新开始同周期不丢计数。
- 不按固定延时、输入静默时长或超时清除忙位。缺少完成事件仍拒绝配置。两路计数各4位，
  溢出或已开始工作后的无匹配完成事件锁存 fault 并保持忙；显式逻辑复位是故障恢复路径，
  不能通过重复 CFG 强行解锁。复位后第一笔工作前的旧管线完成脉冲不产生虚假在途数。
- DDR 写入器原 AXI 完成、FIFO 双域清理和槽位所有权检查保留，不通过伪造 busy=0 绕过。

## CDC 与工程同步

新模块内部只使用 clk_25，同域的 trans_last、线头边沿和 log_last；PI 相位取样与既有
apt_add 的首线头取样点一致，没有增加直接跨域读取 rtc_shot 的通路。scan_active 与
bf_window 分别沿用已有的两级同步输出。输出 busy 经 DDR 的 busy_sync1/2 跨到 AXI 域。

`frame_store_cdc.xdc` 仅对已识别同步器第一级做例外，并将新 tracker 层级排除在旧设计
遗留的跨域例外集合之外，不添加全域 false path。
规范源、Vivado 管理的 proc_top/bproc_top 同步；配置脚本登记新模块，干净重建也会包含它。

## PS 失败诊断

不放宽原3秒 PAUSE 截止时间，不自动复位或重试。失败时在 Zynq 日志打印：

```text
@DDR PAUSE reason=... version=... seq=... ack=... result=... flags=...
@DDR PAUSE_DIAG axi=... parser=... input=... samples=... age=...
```

reason：1未初始化、2版本不匹配、3超时、4硬件返回失败；0成功。
失败回执分别为 DDR_NOT_READY、DDR_ABI、DDR_DRAIN_BUSY（超时且 FLAGS 的输入忙为1）、
DDR_DRAIN_TIMEOUT、DDR_DRAIN_RESULT。不是物理高压测量。超时后仍关闭软件帧缓存就绪状态，
不允许绕过失败继续扫描或改参数；后续未初始化提示不等同于原先的版本错误。

## 软件验证

- `build/scripts/test_frame_store.ps1`：旧粘住尾位复现，新模块的早/同时/晚完成、四合一 PI、
  并行事务、缺失完成、输出尾拍、计数故障和复位恢复。
- 同脚本的 DDR 联合测试：发送真实64线载荷，在最后一线尚未处理完时提交 PAUSE，
  随机 AXI AW/W 背压，断言确认不能早于输入处理、活动帧、AXI 写和 FIFO 清理完成。
  原五档点数/逐字节相等/短突发/租约/溢出/复位回归继续运行。
- 使用真实 apt_add 与 Standard FIFO 模型，向新旧使能路径输入相同的4相位RF；在末相位
  RF 尚未结束时撤销物理扫描。断言原路径丢失尾部，而新路径逐点输出正确且完成后才关闭。
- PS 原生测试覆盖故障快照、版本错误、未初始化、硬件结果、超时、成功及失败时不写下采样。
  其余温度、UART、Wi-Fi 控制、TCP/UDP、网页回归沿用完整主机测试。

2026-10-20 最终源码回归：原生 C 测试通过，Python 共173项（172通过，1项因缺少历史
样本跳过），网页25项及图像网页5项全部通过。五个 self-checking 仿真顶层通过：新增
tracker、真实 PI 尾部对照、DDR 联合测试，以及原 apt_add、scan_sync PI 回归。

验证日志集中在：
`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\capture_drain_fix_20261020_154122`。
其中 `rtl_with_pi_tail.log`、`pi_scan_regression.log`、`software_verified_source.log` 是最终回归记录。
未采集完整 RTL 代码覆盖率，也未执行门级仿真；功能测试通过不代表已达到覆盖率或板级验收目标。

## 离线构建与实现结果（2026-10-20）

Vitis 2026.1 的 USB/SDIO 两种输出宏构建，以及当前 workspace 应用构建均通过。
USB 模式仍只保留参数配置，不恢复 USB 图像输出。DDR 链接检查通过，应用最高 ALLOC
末地址为 `0x001785F0`，未占用 `0x20000000～0x202FFFFF` 三帧缓存区域。

| 模式 | text | data | bss |
|---|---:|---:|---:|
| USB | 180859 B | 140604 B | 162320 B |
| SDIO | 187099 B | 140604 B | 163344 B |

Vivado 2026.1 重新综合、增量布局布线并生成 bit/XSA；增量参考仅复用未改动布局布线，
逻辑和约束由本轮源码重新构建。`ila_bf_i` 原五探针宽度17/2/17/32/7检查通过。

- 最终全局 WNS **+0.797 ns**、TNS 0，WHS **+0.012 ns**、THS 0，脉宽最差余量 +0.264 ns。
  路由阶段显示的 +2.032 ns 不是最终全局时序汇总值。
- 新 tracker 寄存器端点 setup 最差 **+31.138 ns**、hold 最差 **+0.195 ns**；
  从 tracker 到处理逻辑的 setup 最差 **+29.674 ns**。
- tracker 占 **18 LUT、12 FF、0 BRAM、0 DSP**。全设计 LUT 31063/78600（39.52%）、
  FF 62559/157200（39.80%）、BRAM 188.5/265（71.13%）、DSP 293/400（73.25%）。
- 新忙位到 DDR、BF 窗口到 tracker、扫描活动到处理域均识别为两级 ASYNC_REG（CDC-3 Info）；
  新排空路径未出现 CDC-1/4/10/13 Critical。FIFO/快照 bus-skew 报告无负余量。
- **全工程 CDC 不是 clean**：CDC-1/4/10/13 Critical 数量仍为32/4/7/11，与既有记录相同；
  19个无输入延迟及19个无输出延迟端口、旧 RAM 异步控制/流水线等告警仍在。
  这些既有 CDC/I/O 风险未在本次修复中解决，不能将“已约束路径时序满足”等同整板签核。

报告位于 `build/reports/sdio_*.rpt` 和 `build/reports/capture_drain_*.rpt`；本次没有连接硬件，
不把离线测试、资源和时序结果作为板测通过，也不宣称48小时稳定性已验证。

## 本轮配套产物

以下均为本轮重新生成的文件；XSA 内嵌 bit 已与独立 bit 做 SHA256 一致性校验。

| 文件（绝对路径） | SHA256 |
|---|---|
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.bit` | `72B3AA6D49E5B94CAB50505596D8881C221F06534BA36C5804088F8F29375D23` |
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.xsa` | `6BAEEEF8C18D1CC762AE74C9FEDBAB30AB644E8FEF6DA3142AD6B0C428DED5DC` |
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.ltx` | `14C3551092C8CE1BC4DE3249568A9235D6020986ED08B74237C43DB9331FB559` |
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging.elf` | `5D3D6E11240A88F01EA995D0DD192E0B1D5F0264A97F056D2CEBAB43094ECC28` |
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging_USB_frame_store.elf` | `BB58DEB754A13658EFA134ADC8DCD17A7FEDA2967F44CCB93D7FB806944239B6` |

已同步并验证以下 Vitis 默认/平台副本：

- bit/LTX：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\_ide\bitstream\`。
- SDIO ELF：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf`。
- XSA：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\ubmi_p\hw\ultrasound_B_mode_imaging.xsa`
  及 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\ubmi_p\export\ubmi_p\hw\ultrasound_B_mode_imaging.xsa`。
- 平台 bit：上述平台 `hw` 导出目录、其 `sdt` 子目录和 `workspace\ubmi_p\hw\sdt` 的同名 bit。

覆盖前的对应旧产物保存在本轮 `build\verification\capture_drain_fix_20261020_154122` 下，
按原目录结构保留。`build\output\BOOT.BIN` **未重新生成或覆盖**，其 SHA256 仍为
`30BE9B06D8E058C6D26B249EA960DC460CB467EE3EF7D57B3812F2DDDB256360`；从旧 SD 卡镜像
启动不会因此自动获得本修复。不要混用旧 bit 和新 ELF 进行修复验收。

## 更新与板测

修复包括 **PL bit 和 PS ELF**，两者均需更新；ESP32 和 PC 工具无需因本修复重刷/升级。
bit/XSA 的 PS 外设地址和架构不变，不因本项修改重建外设驱动。BOOT.BIN 不自动覆盖，
只更新 ELF 不能替换板上的旧 tail_pending 逻辑。重新启动故障现场前先保存诊断快照。

授权板测后，在高压关闭条件下先测试：未扫描时配置 → 手动开始扫描 → 配置停扫 →
核对 run=hv=0 且 down 正确 → 手动开始扫描并接收新档位完整帧。逐一测试五档并重复切换，
检查CRC、点数/帧长、槽位及错误计数。不自动使能高压。之后才在另行确认安全条件下验收
真实成像、活跃上传期间配置和长期稳定性。温度相关时钟问题、旧 CDC/I/O 风险独立保留。
