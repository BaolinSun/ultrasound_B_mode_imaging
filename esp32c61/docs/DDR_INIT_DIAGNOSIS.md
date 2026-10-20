# DDR 帧缓存初始化分阶段诊断（2026-10-19）

这里的“DDR 初始化”指帧缓存控制接口初始化，不是 PS DDR PHY 校准。

## 已取得的板上证据

- 原运行应用的指令与下载前 workspace ELF 一致。
- 被动读取 `0x404C0200` 多次均为 `0x55424631`，当前 bit 提供新帧缓存 ABI。
- 原应用中 `frame_store.c` 的 `sequence=0`、`initialized=0`，PL 的 ACK、RESULT、FLAGS 均为 0。
- 原应用未走到 INIT 命令提交阶段；需要区分首次版本读取失败、SD1 清理失败及命令入口再次版本读取失败。不能仅凭后来读到正确版本就排除启动瞬间的版本读取问题。
- 后续 SD1 已枚举，当前 `0xE010102C=0x000E0207`、`0xE0101024=0x01FF0000`。这是枚举后的状态，不能反推启动时复位检查已成功。

## 本次源码改动

只增加诊断，不改变 DMA 复位策略、等待截止时间、初始化顺序、PL 或高压/扫描语义。两份 PS 源码分别增量修改，保留原有差异。

启动打印 `@DDR INIT` 与 `@DDR SD1`，RAM 中也保留 `frame_store_init_diagnostic()` 指向的结构体。

| stage | 含义 |
|---|---|
| 1 | 首次 ABI 版本检查失败 |
| 2 | SD1 CMD/DAT 复位或活动状态等待失败 |
| 3 | INIT 命令入口再次 ABI 检查失败 |
| 4 | INIT 已提交，但等待 PL ACK 超时 |
| 5 | 已收到 ACK，但 PL 返回非成功结果 |
| 6 | 全部初始化成功 |

`stage=0` 表示诊断尚未开始，不得将其他字段当作有效结果。SD1 快照记录复位前后 CLOCK/RESET、PRESENT、最终复位字节和耗时。命令快照记录请求序号、ACK、RESULT、FLAGS。

## 软件验证

- 本地 MMIO self-check 覆盖上述失败分支、成功、五档帧长、租约和超时，已通过。
- frame-store 合约测试 4 项通过。
- Vitis 2026.1 当前 workspace、独立 USB 和 SDIO 两种宏模式均构建通过；既有 USB/未使用参数警告仍存在。
- 当前诊断 workspace ELF SHA256：`05F1FDB4E0276EF757ECDCABD314688D05AF440C21EC0FE4A5C8C831DDA97F81`。

## 板测进度与限制

用户已授权仅下载诊断应用；执行了 CPU0 stop/dow/con，未复位 PS、未更新 bit 或 ESP32、未打包 BOOT、未启动扫描或高压。

直接热下载后 PC 为 `0x00110044`，对应本 ELF 的 `Xil_UndefinedExceptionHandler`，诊断结构体仍为未执行状态。这次尝试未取得有效初始化诊断，不能据此确定原始失败分支。需要另行确认仅复位 CPU0 后重新启动诊断应用。

随后用户另行授权仅复位 CPU0，按 `rst -processor / dow / con` 重新运行。应用已正常进入主循环，得到有效诊断：

```text
stage=4 version=55424631
sd_clock_before=000E0207 sd_clock_after=000E0207
sd_present_before=01FF0000 sd_present_after=01FF0000
sd_reset_after=0 dma_elapsed_ms=0
sequence=1 ack=0 result=0 flags=00000008
```

因此，本次 CPU0 复位复现中失败的是 **INIT 命令等待 PL ACK**，不是版本检查或 SD1 DMA 清理。`flags=8` 表明暂停位已置位，初始化位仍为 0；结合 RTL 命令处理逻辑，PL 已接受 INIT，但未完成维护/双域 FIFO 清理。旧日志第一次启动的现场未被保留，不能据此排除冷启动还可能存在另一失败分支。

AFE RESET、PDN_FAST、PDN_GBL 的 GPIO 输出/输入均为 0。现有 `ila_bf_i` 的 UUID 与 LTX 相符，执行一次不涉及扫描/HV 的即时采样成功，状态 FULL。FIFO 输入与 ILA 时钟在源码、BD 和当前实现网表中均为 `data_clk/clkdiv2_mmcm`，因此**不能将本次持续等待直接归咎于当前采样时钟完全停止**。`STATIC.ILA_CLOCK_FREQ=0` 是静态元数据，不是测得的时钟停止证据。

新增 `build/src/sim/tb_ddr_init_clock.sv`，使用真实 XPM 模型验证：输入时钟未启动/中途停止时 INIT 安全等待，时钟恢复后握手完成；测试通过。这证明预期机制本身在该仿真场景可工作，但未复现板上“时钟已运行、INIT 仍未完成”的根因。

当前接口没有暴露 `maintenance`、`flush_request/ack_sync2`、`rd_rst_busy`、`ov_sync2` 等内部等待条件，不能仅凭 FLAGS=8 区分具体卡点。进一步精确定位需要带内部诊断状态的新 bit，或另加诊断 ILA；不得直接跳过清理或伪造 ACK。此轮未修改综合 RTL、未加载新 bit，亦未宣称已修复。

板上当前为已运行的诊断应用，扫描/HV 未启动。BOOT.BIN 和 bit 保持不变。

## 只读内部诊断扩展（用户已授权重建/加载，结果另记）

不改变 UBF1 功能 ABI、命令状态机、FIFO 清理或 ILA 原有探针。

| 相对 FS_BASE 偏移 | 绝对地址 | 只读内容 |
|---|---|---|
| 72 | 0x404C0248 | UBD1 = 0x55424431 |
| 76 | 0x404C024C | AXI 域等待状态 |
| 80 | 0x404C0250 | PL 内部已接收请求序号 |
| 84 | 0x404C0254 | 当前命令 |
| 88 | 0x404C0258 | 待完成维护命令序号 |
| 92 | 0x404C025C | 采样域一致性快照，低 16 位有效 |
| 96 | 0x404C0260 | AXI 域累计收到的快照数，允许回绕 |
| 100 | 0x404C0264 | 距离上次快照的 AXI 时钟周期，饱和 |
| 104 | 0x404C0268 | [4:3] AXI 状态、[2:0] 解析状态 |

AXI 等待状态位：`[2:0] maintenance, [3] flush_request, [4] 保留为0, [5] ack_sync2, [6] rd_busy, [7] ov_sync2, [8] busy_sync2, [9] issuing, [10] active, [11] abort_pending, [12] command_pending, [13] FIFO empty, [14] paused, [15] initialized, [16] maintenance_init, [17] maintenance_purge`。

采样域快照位：`[0] alive=1, [1] 保留为0, [2] req_sync2, [3] flush_seen, [4] flush_ack, [5] fifo_reset, [6] wr_busy, [7] full, [8] overflow_latch, [9] rst_in_n, [15:10] reset_count`。

多位快照通过独立 `xpm_cdc_handshake` 传递，XPM 自动约束数据 max-delay/bus-skew 及两级单 bit 同步器；不直接跨域采样，不将数据路径 false-path。每个输入快照内部一致，但与另一次读取的 AXI 状态/计数不构成原子快照；应对比多次稳定结果。计数停止且年龄增长表示没有新快照，不能把旧快照当作当前输入状态。

XPM 握手没有本地 reset。第一次复位回归检测出诊断请求被本地复位提前撤销的 XPM 断言，已阻止该版本继续构建。请求控制改用与 XPM 同属配置/GSR 初始化周期的 FDRE（INIT=0、R=0），不因功能复位丢弃在途诊断；未使用可综合 initial 块。此改动只属于新增诊断，原有功能逻辑未改变。回归脚本同时加强为遇到 Error/Fatal/XPM 握手断言即失败，不再只判断 PASS 文本。

修正后的停钟/恢复、诊断地址只读和完整五档数据路径回归均通过，未再出现 XPM 断言。

第一轮物理实现的 CDC 检查发现，观察一级同步寄存器会增加其扇出，使两个原同步链被判为 CDC-1。该 bit 未加载到板上。已将这两个诊断位保留为 0，仅观察二级同步输出，增加源码防回归检查，并从 RTL 重新综合/实现；不通过豁免或 false-path 掩盖问题。第一次通过综合/布线并不等同于允许上板，必须通过独立 CDC 检查。

本轮备份目录：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\ddr_bit_diag_20261019_211032597`。

原应用 ELF 和修改前源码已保存在：

`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\ddr_init_diagnosis_20261019_205145278`

原应用 SHA256：`07FF419CB02583E01B484F7C58C10C3459D52281100A99D39335D5E8498348A5`。

修改前 bit（已备份）：`8A7C599DF34F511A8E1C66C75B7650F767F03309907169F9090E8C03579E5E8B`。

BOOT.BIN 未改动：`30BE9B06D8E058C6D26B249EA960DC460CB467EE3EF7D57B3812F2DDDB256360`。

## 最终构建与停机状态 A/B 板测结果

以下为本轮最终状态，取代上文各阶段“尚未加载/bit 未更新”的历史进度描述。

用户分别授权诊断 bit 下载、仅 CPU0 复位，以及临时加载备份原 bit 对照后恢复诊断版。本轮未启动扫描、未使能高压、未烧录 ESP32、未覆盖 BOOT.BIN，未进行 PS 整机复位。每次下载前检查帧缓存未活动、高压 GPIO62 的 bank2 bit8 为 0；下载后运行请求仍为 0，该高压控制位仍为 0。这是控制引脚读回，不代替高压电源的实际测量。

| 对照 | INIT 结果 | 请求序号 / ACK | FLAGS |
|---|---|---|---|
| 原 PL 保留状态，仅 CPU0 复位（此前复现） | stage=4，等待 ACK 超时 | 1 / 0 | 0x8 |
| 新诊断 bit 重新配置，同一诊断应用 | stage=6，成功 | 1 / 1 | 0x1 |
| 保留诊断 PL，仅 CPU0 复位再运行 | stage=6，成功 | 2 / 2 | 0x1 |
| 备份原 bit 重新配置，同一诊断应用 | stage=6，成功 | 1 / 1 | 0x1 |
| 对照后恢复诊断 bit，同一诊断应用 | stage=6，成功 | 1 / 1 | 0x1 |

恢复后的三次只读快照均为：`maintenance=0, initialized=1, paused=0, fifo_empty=1, rd_busy=0, overflow=0, wr_busy=0, fifo_reset=0, errors=0`。`flush_request=ack_sync2=flush_seen=flush_ack=1`，双域清理已完成。输入快照计数从 50407333 推进到 53126005，年龄为 4/28/12 个 AXI 周期，说明本次确有新的输入域快照，而不是复用一个旧状态。

### 可以确认与不能确认的结论

- 已确认此前复现的卡点是 PL 帧缓存 INIT 维护/清理等待，不是 DDR PHY 校准，也不是该次 SD1 清理。
- 原 bit 重新配置也通过，不能归结为“原 bit 文件一定损坏/不匹配”或“只有新诊断 RTL 才能工作”。
- 当前证据更符合此前 PL 的残留状态或启动时条件相关，但**具体哪个等待条件、由什么事件触发仍未定位**。重新配置清除了旧现场；原 bit 没有内部寄存器，不能追溯当时的 FIFO busy、握手或溢出状态。
- 本轮只增加只读诊断，没有修正原功能状态机。源码级隔离检查通过，不等同于形式等价证明。不能把初始化暂时恢复写成根因已修复。
- 再出现 `@DDR ERROR` 时，应先保留当前 PL、不重新加载 bit，通过 `@DDR PL_DIAG` / `@DDR PL_SOURCE` 与只读寄存器记录多次现场，再决定恢复操作。快照计数停止/年龄增长与握手不匹配可以区分不同卡点；需结合 AXI 状态及 FIFO busy 一同判断，不能仅凭计数停止认定外部时钟故障。
- 本次未验证连续图像链路、温度相关时钟故障、全部复位组合或 48 小时稳定性，未以停机初始化成功替代这些验收。

### 软件、时序和 CDC 验证

- 完整 RTL self-check：五档长度、逐字节比对、AXI 背压、帧尾短突发、租约、淘汰、溢出和复位通过；停钟/恢复及诊断只读测试通过；合约检查共 5 项通过。
- Vitis 2026.1 当前应用及 USB/SDIO 宏模式构建通过。新增只读寄存器未改变 PS 硬件接口，本轮没有重新生成 BSP/FSBL，也没有更改 ESP32 固件。
- 最终实现已检查时序：setup 最差余量 **+0.807 ns**，hold 最差余量 **+0.014 ns**；hold 余量较小，不能据此保证所有温度条件板级稳定。
- 新诊断数据握手路径受到 **20 ns** 约束，最差余量 **+19.052 ns**，未被 false-path；四条同步链一级寄存器仅驱动各自二级 D，未增加观察扇出。
- 帧缓存内未新增 Critical CDC；全工程既有 Critical CDC-1/4/10/13 分别为 **32/4/7/11**，以及既有 I/O 约束/方法学警告仍存在，**全工程不是 CDC clean**。
- 最终全设计资源：LUT **31047**、FF **62548**、BRAM **188.5**、DSP **293**。相对本轮备份基线增加 **167 LUT、118 FF**，BRAM/DSP 不变。使用率分别为 **39.50%、39.79%、71.13%、73.25%**。`frame_store_i` 本身为 1775 LUT、1264 FF、5 BRAM、0 DSP。
- 原 `ila_bf` 探针与深度保持不变。

### 当前加载产物及 SHA256

| 产物 | 绝对路径 | SHA256 |
|---|---|---|
| 诊断 bit（当前板上） | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.bit` | `187F7923F57FCC52BB81A152254519437D4EA3131CCB8E1ECBFF9AC4A6F29CD7` |
| 匹配 XSA | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.xsa` | `4D187A3FEDBF988CC94E2C5F0B0FAA0D336384EFDD819B431A5835538346E0D2` |
| 匹配 LTX | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.ltx` | `14C3551092C8CE1BC4DE3249568A9235D6020986ED08B74237C43DB9331FB559` |
| 诊断 ELF（当前板上） | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf` | `A3E4D692F4761CB2ECD51FFAABF2B731F542478C3BBD9F966365521ED83F3AD1` |

2026-10-19 板测的 JTAG 明确使用上表 bit，当时未使用或更新 Vitis `_ide/bitstream` 的旧副本。2026-10-20 已按用户要求同步，见下节。BOOT.BIN 仍为本轮之前的版本，重新上电启动不等同于保留本次 JTAG 诊断程序。

对照记录均位于 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\ddr_bit_diag_20261019_211032597`：

- `board_internal_diagnostic_hwmap.log`：初次诊断版成功。
- `board_cpu0_warm_restart.log`：保留 PL，仅 CPU0 复位成功。
- `board_original_bit_comparison.log`：备份原 bit 重新配置成功。
- `board_diagnostic_bit_restored.log`：诊断 bit 已恢复，三次快照及安全状态读回。
- `final_safe_verification.log`：最终时序及同步链检查。
- `full_rtl_safe.log`、`ps_build.log`：RTL 回归与应用构建记录。

## 2026-10-20：Vivado/Vitis 硬件副本同步

以本页上表 SHA256 对应的 `build/output` 诊断产物为唯一来源，已同步下列位置，并逐一验证完整 SHA256 一致。新 XSA 内嵌 bit 与独立 bit 一致；新旧 XSA 内六份 `ps7_init*` 文件完全一致，因此本次仅同步硬件产物，没有重新生成 BSP、FSBL、SDT 或应用，也没有修改调试启动配置。

- Vitis 应用调试 bit/LTX：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\_ide\bitstream\ultrasound_B_mode_imaging.bit`、同目录 `ultrasound_B_mode_imaging.ltx`。
- 平台输入 XSA：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\ubmi_p\hw\ultrasound_B_mode_imaging.xsa`。
- 平台导出 bit/XSA/LTX：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\ubmi_p\export\ubmi_p\hw\` 下对应同名文件。
- 平台 SDT 内 bit：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\ubmi_p\hw\sdt\ultrasound_B_mode_imaging.bit` 及 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\ubmi_p\export\ubmi_p\hw\sdt\ultrasound_B_mode_imaging.bit`。
- Vivado 实现目录：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\vivado\ultrasound_B_mode_imaging.runs\impl_1\heart_top.bit`、`heart_top.ltx`、`debug_nets.ltx`。

Vitis 现有 `launch.json` 已引用上述应用调试 bit，无须改路径。若 IDE 仍显示旧信息，可刷新工程或重新打开工作区。不要运行会删除重建整个 workspace 的脚本来完成单纯副本同步。

同步前的已有副本已按原目录结构备份到 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\hardware_sync_20261020_091119696`。本次未操作板卡，未下载/烧录，也未修改 BOOT.BIN；本页先前的“根因尚未定位”结论不变。

## 2026-10-20：修复 SD1 未启内部时钟时的初始化失败及假成功回执

### 新现场及边界

用户随后提供不同于前一天的失败现场：`stage=2, version=55424631, seq=0, clock_before=0, clock_after=06000000, reset=06, elapsed_ms=5`。这表明版本检查成功，但停在 PS 的 SD1 CMD/DAT 清理，尚未提交 PL INIT。`0xE010102C` 的低 16 位是时钟控制，高字节是软件复位；`06000000` 不是时钟已开启，而是复位位仍为 6。PL 快照显示未接收 INIT，不能将本次与此前 stage=4 的维护等待混为一个已经确认的根因。

此前 `stop_inherited_dma()` 在内部时钟未启用时直接请求 CMD/DAT 复位，并有 100000 次轮询保护。当时将日志的 5 ms 判断为迭代先耗尽；后续已确认耗时公式存在 BSP 宏展开问题，**不能再依据这个 5 ms 断定实际只等待了 5 ms**。以下是第一版软件修复的历史记录，第二版及板测结果见末节。没有跳过清理或伪造 PL ACK。

### 修改范围

- `frame_store.c`：保留 SD1 当前分频与外部卡时钟状态，只用字节访问使能内部时钟 bit0，确认内部时钟稳定 bit1 后才写 CMD/DAT 复位 6；等待复位位清零且 PRESENT 的 CMD/DAT inhibit/data-active 均为 0，才允许 PL INIT。既不使能原本关闭的外部 SD 时钟，也不发送卡命令，不触碰 SD0 或 SLCR。
- 清理共用 20 ms 单调时钟截止，原 100000 次限额提升为仅防计时器故障的 10000000 次保护；不因正常 CPU 轮询过快在约 5 ms 时提前失败。计时器本身失效仍会由迭代保护退出，不无限等待。
- 诊断结构保留原前 12 个字不变，追加 `sd_cleanup_phase` 和 `sd_clock_ready`；`@DDR SD1` 新增 `cleanup`、`clock_ready`。cleanup 为 1=等时钟、2=等复位、3=等总线空闲、4=清理完成；总的 stage 语义不变。
- `Beamform_Resume/Start()` 返回操作结果。失败时关闭高压控制、清零扫描/SPI 启动请求；`BEAM START` 和旧 USB/UART 的自动启动路径收到 `@NACK ... DDR_RESUME_OR_ABI`，不再在实际 run=0 时返回成功 ACK。正常 Wi-Fi BEAM START 仍不自动开启高压，正常旧接口行为保持。
- SDIO 枚举成功后还检查本地帧缓存初始化、当前 ABI 和 PL initialized 位。不满足时发布 SOURCE_FLAGS=8、HOST_MODE=0，并打印 `@SDIO SOURCE_UNAVAILABLE ...`，由现有 ESP32 启动条件拒绝上传，不再把“仅总线枚举成功”当成图像源就绪。
- 不在 SDIO 轮询里自动重做 INIT，以免运行期异常时撤销一个仍在被 DMA 使用的租约。中途故障仍遵循原排空/租约规则。
- 规范源和 Vitis 源分别增量修改；`main.c` 原有注释及调试输出差异保留，未整文件覆盖。未修改 RTL、bit、ESP32 固件、AFE 或 TCP/UDP 图像协议。

### 验证结果

- 新增 C 模型覆盖：内部时钟关闭的冷启动、保留分频/卡时钟的热重启、12 ms 延迟复位（超过原轮询限额）、时钟不稳定、复位不完成、PRESENT 持续 busy、失败不提交 PL INIT，以及本地/PL 就绪检查。
- SDIO 主机桩覆盖源未就绪时 HOST_MODE 不发布为 1、无租约申请/DDR 发送。扫描控制和 UART 测试覆盖实际启动失败不再 ACK、run/HV 均关闭，正常扫描和旧接口回归通过。
- 完整 C 自检查通过；Python 共 167 项，166 通过、1 项因历史采集样本缺失跳过；网页测试 12+5 项通过。回归输出中故意注入的 `Soak FAILED`、CRC 错误属于负例，其父测试均通过，不是板测失败或真实压测结果。
- Vitis 2026.1 的 USB、SDIO 宏模式以及日常 workspace 应用均构建通过；ELF 区间检查通过。SDIO/workspace 为 text=186587、data=140604、bss=163312 字节，相比上一诊断应用增加 text=864、bss=32 字节。既有未使用参数/USB 编译警告仍保留。
- **本轮尚未下载、运行或板测**，未启动扫描/高压、未重置设备，也未覆盖 BOOT.BIN。前一天 stage=4 的残留 PL 状态问题仍独立保留，不能以本次软件测试宣布已解决。

### 更新的应用与复测

日常 Vitis launch 仍使用：

`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf`

该文件与 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging.elf` 已同步，SHA256 均为 `A699EC994A3CA7D12C5B6F380FC2B52A21515274BC9ACCC923EFDFEB7C421A9F`。USB 宏构建为 `build/output/B_mode_focus_imaging_USB_frame_store.elf`，SHA256 为 `AF8C7EA70464B482634413DB710F8CBB50C642DBC4BA2FE9396C780829EA2EB0`。

bit 沿用上表 `187F7923...` 诊断版，无需重新生成或更新 ESP32。BOOT.BIN 仍为旧启动镜像，本次修复不会通过它自动启动。旧应用及此次相关源文件保存在 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\sd1_init_fix_20261020_102948887`，同目录含 `software_tests.log`、`vitis_modes_build.log`、`vitis_workspace_build.log`。

在高压关闭条件下通过 Vitis 运行新应用，先确认 `@DDR INIT rc=0 stage=6`，`@DDR SD1 ... reset=00 ... cleanup=4` 以及 `@SDIO ONLINE ...`。冷启动时 `clock_ready` 低两位应为 3（不强制高位相同）；若仍失败，保留包含 cleanup 和 clock_ready 的完整日志，不通过重复点击 SDIO 或重新加载 bit 掩盖现场。

初始化成功不代表已经产生完整图像。之后需由用户按原有流程产生完整帧，停扫后可上传保留帧；SDIO 单帧按钮本身不会启动扫描或高压。

## 2026-10-20：第二版修正与已完成的初始化板测

用户再次复现 `stage=2, cleanup=1, clock_before=0, clock_after=0, reset=0, elapsed_ms=5`。表明上一版并未成功使能内部时钟，不是 PL INIT 阶段异常。

### 查明的两个软件问题

1. Clock Control 是 16 位寄存器，本地 AMD `XSdPs_EnableClock()` 使用 `XSdPs_WriteReg16()`。上一版自定义清理却使用 `Xil_In8/Out8(0xE010102C)`。现改为 `Xil_In16/Out16`，保留完整分频和卡时钟字段；0x2F 的复位寄存器仍按官方方式使用 8 位。没有采用对整个 32 位组合寄存器的盲目覆盖。测试桩之前允许字节访问 Clock Control，未暴露该差异；现已严格拒绝该错误宽度。
2. BSP 的 `COUNTS_PER_SECOND` 展开为未加括号的 `CPU_FREQ/2`。原 `(delta*1000U/COUNTS_PER_SECOND)` 实际为 `(delta*1000U/CPU_FREQ)/2`，相对正确值少四倍，20 ms 可显示成 5 ms。现显式除以 `(u64)(COUNTS_PER_SECOND)`，截止时间表达式也采用完整括号及 64 位计算。第一版“已修复约 5 ms 提前退出”的推断不充分，不能作为已实测的原因。

这是 PS 启动软件耦合问题，不是 DDR 写入逻辑改变了 SDIO 电气接口。三缓冲新增租约后，在 PL 回收槽位前必须停止旧 CPU 可能遗留的 SD1 ADMA（它从 DDR 读数据送到 ESP32）；该保护因此成为 INIT 的前置条件。它不是 PL 写 DDR 的 DMA，也没有改变 SDIO 引脚、时钟指标或线上的图像协议。前置清理实现有误，会使缓存不可用，再由缓存就绪门控拒绝扫描/上传。不能直接删除保护来让旧 DMA 继续读已复用槽位。

### 验证与当前板上状态

在用户本轮明确授权下，仅复位 CPU0 并下载应用，保留现有诊断 bit；未烧录 ESP32、未覆盖 BOOT.BIN、未启动扫描、高压或图像上传。

| 板测 | 初始化前 SD1 时钟组合寄存器 | 清理后 | 结果 |
|---|---|---|---|
| 已开启时钟的应用重启 | 000E0207 | 000E0207 | stage=6，seq=ack=1，cleanup=4 |
| 停机且确认 SD1 无 CMD/DAT/DMA 活动后，关闭 Clock Control 再重启应用 | 000E0000 | 000E0003 | stage=6，seq=ack=2，cleanup=4 |

第二项保留 Timeout 字段 0x0E，只将低 16 位时钟控制清零；这是受控复现“内部时钟关闭”的启动条件，不是整板断电冷启动。16 位写入后时钟低两位确实变成 3，PL ACK 也推进，支持访问宽度修正解决本次可复现路径；仍不宣称已覆盖所有冷启动、复位和温度条件。

两次均读回扫描请求为 0，高压 GPIO62 的输出/输入 bit8 为 0，PL errors=0、maintenance=0、initialized=1、FIFO empty=1；高压 GPIO 不等同于实际电压测量。只读查询 ESP32 `/api/status` 得到 `sdio.ready=true, host_sdio=true, source_flags=1, running=false, errors=0, free_slots=2`。`source_latest=source_published=0`，因为没有启动采集，并非验证了零字节图像传输；完整图像链路仍需用户后续采集验证。此前 stage=4 的保留 PL 状态异常没有在本轮复现，不将它合并宣称已解决。

完整 C 回归（包括严格访问宽度、普通频率宏与真实未加括号频率宏两种构建）通过；Python 167 项中 166 通过、1 个历史样本缺失跳过；网页 17 项通过。Vitis USB/SDIO 两种宏与当前 workspace 均构建通过；最新 SDIO 应用 text=186779、data=140604、bss=163312 字节。新增硬件资源为零，未改 RTL/约束，不重新宣称之前的 STA/CDC 风险已解决。

当前板上及 Vitis 默认路径 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf` 的 SHA256 为 `B98565C6CB0214A77DB4F9F5120DAE711E534565CC36F0AF8B48FE0227B19027`，与 `build/output/B_mode_focus_imaging.elf` 一致。USB 交付 ELF 的 SHA256 为 `D2F2818A9A0C9C014A01486DE922468B9EBCD748E0EC1E06C5CC31B50146CAA3`。bit 仍为 `187F7923...`，BOOT.BIN 仍为 `30BE9B06...`；断电后从旧 BOOT 启动不包含本轮软件修复。

本轮旧源码/ELF 备份及结果在 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\sd1_access_fix_20261020_104602987`：`board_init_test.log`、`board_clock_off_test.log`、`device_sdio_status.json`、`software_tests.log` 和 `vitis_build.log`。板测脚本 `build/verification/verify_sd1_access_fix.tcl` 仅为本轮验证，RAM 符号地址已针对本 ELF 核对；换 ELF 后不得未经核对继续使用固定地址。
