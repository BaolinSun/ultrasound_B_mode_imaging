# Zynq 超声 PI-THI、接收增益、TX7332 读回与启停状态迁移报告

## 1. 迁移范围与基线

- 目标仓库基线：`ultrasound_B_mode_imaging@93023ec`。
- 固定功能源：`ultrasound_focus_B_mode_imaging@f225a21f095718dbce4ada470ad85f1f9bbc4a14`。
- 迁移范围：旧仓库 `b2f2076..f225a21` 的 PI-THI 主线；本轮增量为 `6f7e0d7..f225a21`。
- 增量提交 `f225a21` 只修改旧仓库 6 个 PS 文件：`HV/hv.c`、`bram/bram.c`、`bram/bram.h`、`main.c`、`main.h`、`uart/uart.c`，没有新增 RTL、BD 或 XDC 差异。
- 排除项：未合并的 `THI@45cc58b` 旁支、旧 Vivado 2020.2 工程/bitstream/XSA/ILA XCI，以及删除 `tools/` 的仓库整理动作。
- 工具版本：Vivado/Vitis 2026.1，器件 `xc7z030sbg485-2`，顶层 `heart_top`。

源版本校验使用 `build/reports/source_commit_manifest.gitblob` 中固定提交的 Git blob ID，不依赖旧仓库当前分支或工作区状态。

## 2. 已完成的功能迁移

### PL 接收与成像链路

- AFE5832 输入改为 12-bit 有效数据，波束累加相关通路扩展为 21 bit。
- 新增 `gain_shift[3:0]`，寄存器地址 `0x11530`，有效范围 `0..8`，每级约 `+6.02 dB`。
- 增益跨时钟域采用两级同步并在连续采样一致后更新；同步器带 `ASYNC_REG`、`SHREG_EXTRACT=NO`、`DONT_TOUCH`，只有第一级被精确 false-path。
- `filter_iq`、`demod_iq`、PI 合成和 `log_compress` 增加有符号舍入、截断和饱和，避免强回波溢出反相或变暗；`gain_shift=0` 保持原定点行为与流水延迟。
- 四相 PI-THI 使用 A+、A−、B+、B− 共 256 shots；波束表索引为 `rtc_shot[7:1]`，图像行号为 `rtc_shot[7:2]`，输出仍为 64 行、8-word 头及每行 6144 个采样。
- `apt_add` 使用 FIFO 完成四相累加，第四相输出舍入除二后的 20-bit 饱和值；停止请求仅在 shot 255 后生效。

### TX7332 动态配置与读回

- 每次发射前等待九组 BRAM 动态配置及寄存器 `0x18` 写入完成；仅用 `PAT_INV` bit 6 改变极性。
- 四个器件的静态 `0x18` 基值分别捕获、保存并可刷新。
- 读回保持旧主线语义：组合 `tx_spi_0/1` 两个内部 die 的返回值，不扩展为四器件聚合。
- 读请求/完成采用 toggle 握手；地址和返回数据在握手期间保持稳定，多位捆绑数据使用定向 `set_max_delay -datapath_only`。
- 参数时钟域和 200 MHz 域的同步器均保留，未增加宽泛时钟域 false-path。

### PS、配置与命令

- AFE 初始化改为 12-bit ADC、PGA 27 dB、LNA 21 dB，并在初始化结束产生 TX_TRIG 脉冲。
- 新增 `Rx_SetGainShift()` 和实时命令 `GAIN SET <shift>`，超出范围钳位为 8，无需停止成像。
- 新增 `Tx7332_ReadRegister()` 和 `TX7332 RD <addr>`；地址限制为 `0x000..0x3FF`，映射保持 `0x404C0110/0x404C0114`，等待时间保持 10 ms。
- TX7332 读回在 beamforming 活动时返回 `BEAM_ACTIVE_USE_BEAM_STOP`，要求先执行 `BEAM STOP`。
- `PI_SHOTS_PER_LINE=4`、`PI_SHOTS_PER_FRAME=256`；USB/UART 协议、64 行帧大小和数据头格式未改变。
- 启动 TX7332 `0x120` 数据更新为 `0x0007B9BA`，AFE GUI `0xC7` 更新为 `0x2805`。
- 保留 PC/GUI 5 us 与启动 `delay_profile.c` 6 us 的既有差异及 16384-word 延时表。

### 启停生命周期与首次启动状态

- 上电装载默认参数后保持确定性停止状态：HV、`beamform_enable` 和 `spi_start` 均为低；启动提示为 `Beamform ready; STOP.`，不再自动开始成像。
- `Beamform_Resume()` 强制执行 `{0,0}`、等待 100 us、`{0,1}`、等待 5 ms、`{1,1}`，确保 TX7332 配置状态机每次都看到真实的 `spi_start` 上升沿。
- `Beamform_Pause()` 写入 `{0,1}`，只暂停扫描并保留 TX 配置和 HV 状态；`Beamform_Halt()` 写入 `{0,0}`，为下一次启动重新准备上升沿；`Beamform_Stop()` 关闭 HV、执行 Halt，并保留 1 秒帧结束等待。
- UART 与 USB 共用命令解析器：`BEAM START` 调用 Resume 且不自动打开 HV，`BEAM STOP` 调用 Pause 且不关闭 HV；`HV ENABLE` 读取 GPIO 输出状态确认使能，`HV DISABLE` 关闭 HV 并执行 Halt。
- 配置事务语义保持不变：`CFG BEGIN` 完整停止，`CFG END` 调用 `Beamform_Start()` 重新配置 TX、打开 HV 并启动扫描，`CFG ABORT` 完整停止。
- HV 初始化明确将 HV_EN 和 LED1 拉低；LED1 使用 EMIO 55 并随 HV 同步变化，LED2 调整为 EMIO 54。`hv_is_enabled()` 统一声明在 `hv.h`。
- 按确认范围没有修改 PC/GUI 工具的 HV 控件或命令封装，因此单独使用旧工具的 `BEAM START/STOP` 不会改变 HV 状态。

### 既有时序修复保护

以下已有设计保持不变：`beamform_active` 两级同步器、显式 `scan_enable`、AFE 复位同步链、W_DDR 第二级同步修复及其精确时序例外。XDC 已将旧 `rtc_line_reg` 端点更新为实际 `rtc_shot_reg`，并为 `tx_sdout[1]` 添加 E7 管脚。

## 3. Vivado 2026.1 验收结果

实现策略固定为 `Performance_ExplorePostRoutePhysOpt`。该策略只执行物理优化，没有删除同步器或放宽时序例外。

| 项目 | 最终结果 | 门限 |
|---|---:|---:|
| WNS | 0.566 ns | ≥ 0.5 ns |
| TNS | 0.000 ns | 0 |
| WHS | 0.017 ns | > 0 |
| THS | 0.000 ns | 0 |
| Slice LUT | 28,038 / 78,600，35.67% | < 80% |
| Slice Registers | 58,797 / 157,200，37.40% | < 80% |
| Block RAM Tile | 170.5 / 265，64.34% | < 80% |
| DSP | 293 / 400，73.25% | < 80% |
| Routed DRC | 0 Error，0 Critical Warning | 0 Error |
| 未约束内部端点 | 0 | 0 |

完整报告位于 `build/output/`：`timing_summary.rpt`、`utilization.rpt`、`route_drc.rpt`、`cdc.rpt`、`clock_interaction.rpt`、`check_timing.rpt`。

CDC 报告对新增 TX 读回的 32-bit 返回数据识别为带 `Max Delay Datapath Only` 的 clock-enable controlled bundled-data crossing，完成 toggle 被识别为两级 `ASYNC_REG` 同步。报告中的其他 Critical CDC 项主要是原工程已有的波束参数/shot 总线和 W_DDR/扫描通路；新增 gain 总线进入第一同步级的路径已被精确 false-path，并通过稳定值确认后使用。没有新增无约束的关键 CDC。

本轮 `f225a21` 仅修改 PS 软件，不改变任何 PL 时钟域、同步器或时序例外；10 个 RTL 回归测试和完整实现结果与上轮保持一致。`check_timing` 报告中无时钟寄存器、未约束内部端点、组合环和部分 I/O delay 均为 0；报告仍列出板级异步/源同步接口的 19 个无 input delay 和 19 个无 output delay 端口，这是既有接口约束状态，并非本轮新增。

说明：Vivado 在 bitstream 阶段仍报告 3 条既有 `Memdata 28-*` Critical Warning，原因是旧 BD 中处理器 BRAM 的 BMM reference 名称无法匹配；本设计由 FSBL 和应用 ELF 作为独立 BOOT.BIN 分区启动，不依赖该 BRAM 初始化。Bitstream 生成成功，正式 routed DRC 为 0 Error/0 Critical Warning。BD validation 另有旧自定义 AFE AXIS 缺少关联时钟元数据的提示，板级接口语义未在本次迁移中擅自更改。

## 4. RTL 与工具验证

Vivado XSim 的 10 个 self-checking testbench 全部 PASS：

1. RX 32 路相邻通道重排及 valid 透传。
2. 固定 8-word 头保持和 PI 数据路径。
3. 四相 FIFO 累加、舍入、饱和、6144 点对齐与 64 行输出。
4. demod/up-data 的完整头部透传。
5. 256-shot 顺序、64 行映射和 shot 255 后停止。
6. 九组动态写入及 `PAT_INV` bit 6 行为。
7. 四个静态 `0x18` 基值捕获、屏蔽、刷新及相互独立。
8. gain CDC、unity 行为和正负饱和。
9. log compression 饱和防回绕。
10. 异步 TX 读请求/完成 toggle 及捆绑地址/数据保持。

Python 通道映射与延时模型检查通过：TX 双射、打包、启动 6 us 表和保留的 GUI 5 us 表均一致。当前主机未安装 MATLAB/Octave，因此 MATLAB 脚本未在本机执行；其等价 Python 模型和生成表已完成自动核验。

## 5. Vitis 与启动镜像

- canonical `workspace/` 已由 Vitis 2026.1 从本次 XSA 重新创建并同步当前 PS 源。
- Platform、standalone 应用及 FSBL 均构建成功，零编译错误。
- BOOT.BIN 已由 Bootgen 2026.1 生成并回读确认分区顺序：
  1. `zynq_fsbl.elf`
  2. `heart_top.bit`
  3. `B_mode_focus_imaging.elf`
- 旧 USB/调试代码仍有 unused/sign-compare 等编译 warning，本次没有为消除无关文本差异而改写这些模块。

## 6. 最终产物 SHA-256

```text
cffd9df2c39dc2aad495f7fde8ed22c0d8f60316dadad0dcb658bf977542b5c2  heart_top.bit
ccdd759e22932860a119cf5af6e194f54fa252ed953b2cf9ae6e13696f026b32  ultrasound_B_mode_imaging.xsa
703d00bddeb210c62629578e13db6f9f6b5166dbf45f3df0db188a11349c2eaf  B_mode_focus_imaging.elf
89123546f759317cfe1a7378463fe179662e30f3786e18e292c402ea7e3bb077  zynq_fsbl.elf
052b4eed59fb9ffab9a451e02bdaa59938d8c5898fd373762bbb3422a12df7ec  BOOT.BIN
```

权威清单位于 `build/reports/artifact_manifest.sha256`。

## 7. 板级验证项

以下内容不能由离线构建替代，需在目标硬件上验证：

- 上电后 HV 和扫描均保持关闭，LED1 熄灭。
- 执行 `HV ENABLE` 后 LED1 点亮，再执行 `BEAM START` 可产生 TX7332 配置和 PI-THI 发射。
- 多轮 `BEAM STOP/START` 能暂停和恢复扫描且 HV 状态保持不变；`HV DISABLE` 关闭高压、熄灭 LED1 并拉低 `spi_start`，再次 ENABLE/START 后可重新启动。
- `CFG BEGIN/END/ABORT` 分别满足完整停止、重新配置并自动启动、完整停止的语义。
- AFE5832 12-bit 数据位序、LNA/PGA 实际增益与强回波饱和边界。
- E7 管脚电气连接及两路 TX7332 SDOUT SPI 读回。
- A+/A−/B+/B− 四相发射波形、`PAT_INV` 极性和每 shot 配置完成时序。
- 64 行成像数据顺序、PI-THI 抑制效果、实时 gain 调节效果。
- 连续 48 小时运行稳定性和 USB 帧完整性。

## 8. 可重复构建

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build\scripts\build_all.ps1 -Stage hardware
powershell -NoProfile -ExecutionPolicy Bypass -File .\build\scripts\run_vitis_2026_1.ps1 -Stage all
powershell -NoProfile -ExecutionPolicy Bypass -File .\build\scripts\create_boot_image.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\build\scripts\check_migration.ps1
```

若 canonical workspace 正被 Vitis IDE 打开，可设置 `ULTRASOUND_VITIS_WORKSPACE` 指向隔离目录完成验证；正式交付前仍应在 IDE 关闭后重建默认 `workspace/`。
