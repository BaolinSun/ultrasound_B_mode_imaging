# ultrasound_dev_kit 参数下发功能盘点

分析日期：2026-10-29。参考目录：`D:/MyProjects/py_prj/ultrasound_dev_kit`。
本次仅阅读源码及 JSON，不启动 GUI、MATLAB 或发送工具，不接触硬件，不修改参考仓库。

## 下发入口

| 入口 | 实现 | 范围 |
|---|---|---|
| UART 命令行 | `tools/ultrasound_config_uart.py` | 10 类配置目标、全部配置、TX 回读、扫描开始/停止/循环、dry-run |
| USB 命令行 | `tools/ultrasound_config_usb.py` | 同一套 JSON 打包规则，另有 `--cmd` 原始命令、高压开关、组合成像启停 |
| 独立参数 GUI | `tools/ultrasound_config_gui.py` | UART 下发，JSON 导入/保存、校验、命令预览、曲线/热图、AFE 回读 |
| 成像桌面 GUI | `ultrasound_gui.py` | 调用上述工具下发选定目标，USB 高压/扫描协调；额外实现焦点更新 |
| 参数生成 | `tools/para_cal/*.m`、`tools/calc_dtgc_registers.py` | 生成可下发的 JSON/寄存器值，生成操作本身不表示已下发 |

目标选择为 `afe5832`、`tx7332`、8 类 BRAM 参数，以及 `all`。`all` 是组合操作，不是第 11 种参数。
独立参数 GUI 当前通过 `serial.Serial` 工作，不应因主桌面 GUI 同时有 USB 入口就把它描述成 USB 参数 GUI。

## AFE5832 配置

来源：`tools/ultrasound_config_uart.py:158` 起的各 `build_*_commands()`。

- VCA：LNA 增益 15/18/21 dB，PGA 增益 21/24/27 dB，LPF 10/15/20/25 MHz，以及 LNA HPF 档位。
- 低噪声/低功耗工作模式：`low_noise`、`low_power`；工具拒绝 low_power 与 25 MHz LPF 的组合。这不是当前工程的 PDN_GBL 全局掉电待机功能。
- ADC 数字高通 DHPF：四组（0～3）独立使能、截止档位代码、舍入使能。
- DTGC Profile：四组起止增益、正/负步长及步进频率、起止索引、开始时间、保持时间；支持原始 DTGC 寄存器补充。
- DTGC 内部非均匀模式：按 ADC 时钟、声速、LNA/PGA、起始深度、最大增益深度、下降深度、起止增益码、正负步进码、斜率因子、存储银行计算寄存器和最多 160 个存储字。
- 原始寄存器：GLOBAL/ADC/VCA/DTGC 四种映射，8 位地址、16 位数据，生成 `AFE WR <map> <addr> <data>`。
- GUI 支持增删原始寄存器行、编辑数值、DTGC 参数编辑、按行 `AFE RD` 回读。DTGC 中 LNA/PGA 修改会同步到 VCA 配置。

注意：`input_impedance_ohm` 在当前工具中**仅记录、不下发**，源码明确提示没有对应的直接寄存器接口。
`build_afe_commands()` 先生成参数化配置，最后追加 `raw_registers`；同地址的后续原始写入可能覆盖前面的值。

## TX7332 与 BRAM 配置

所有表以十六进制 32 位传输字打包，命令偏移为“字”，不是字节；这不意味着每种算法系数本身都有 32 位精度。
`BRAM WR`、`TX7332 WR` 单条最多 8 个字，较大表分块下发。

| 配置目标 | 下发内容 | 固定传输字数 | 命令 |
|---|---|---:|---|
| `tx7332` | TX 静态寄存器地址/数据表；GUI 64 行地址/数据对 | 128 | `TX7332 WR tx7332_config_data ...` |
| `delay_profile` | 发射延时及逐线 TX 控制块 | 16384 | `BRAM WR delay_profile ...` |
| `demod_coeffs` | I/Q 解调正弦、余弦表 | 各 8192 | `BRAM WR fdemod_sin/fdemod_cos ...` |
| `log_table` | 对数变换查找表 | 1024 | `BRAM WR log_table ...` |
| `x_element` | 64 阵元坐标量化表 | 64 | `BRAM WR x_element ...` |
| `hamming` | 64 通道 Hamming 加窗/孔径权重 | 64 | `BRAM WR hamming ...` |
| `dfilter` | FIR 滤波器系数 | 64 | `BRAM WR dfilter ...` |
| `t_timing` | 扫描时序、点数、接收窗口及抽取参数 | 10 | `BRAM WR t_timing ...` |
| `sin_beta` | 64 条扫描线角度正弦量化表 | 64 | `BRAM WR sin_beta ...` |

打包入口：`build_bram_commands()`、`build_tx7332_commands()`。工具检查表深度、32 位字范围和未知区域名。
`t_timing` 的顺序为 `[rtc_dot, rtc_period, t1, t2, t3, t4, t5, t6, t7, rtc_down]`。

MATLAB 默认生成逻辑还表明：

- 解调表按 25 MHz 采样、4 MHz 中心频率生成；改变解调频率需重新生成表，不是发送一个 `FREQ` 数值命令。
- FIR 默认 64 抽头、1.5 MHz 低通，按 25 MHz 采样计算；数值为 16 位定点、11 位小数。
- 对数表由 `log10(1:1024)` 量化到 16 位、14 位小数生成。
- 阵元坐标默认 64 阵元、0.3 mm 间距；Hamming 权重量化到 8 位；角度表默认 ±45°、64 线。
- TX 延时生成包含物理阵元到硬件通道映射、两次合成孔径接收的 T/R 控制及 profile 装载，不只是一个简单延时数组。

这些是参考工程生成器的默认值，不是当前成像硬件的自动回读结果。独立参数 GUI 的主要直接编辑对象是 AFE、DTGC 和 TX 寄存器表；其他表主要通过加载/保存 JSON、MATLAB 生成和预览管理，不能把它们都描述成现成的交互式物理参数滑块。

## 焦点深度更新

`ultrasound_gui.py` 的 `_focus_commit_requested()`、`_start_focus_matlab_generation()`、`_start_focus_usb_config()` 实现：

1. 用户提交新焦点，限制在 20 mm～当前显示深度，过滤变化太小的请求。
2. 若正在采集，停止 USB 采集并发 `BEAM STOP`；不在采集时直接进入参数生成阶段。
3. 调用 MATLAB `para_transmit_delay(output_dir, true, focus_m)`；同焦点的生成结果可复用缓存。
4. 通过 USB 的 `--target delay_profile --delay-profile-config ...` 下发新表。
5. 若操作前正在采集，会尝试恢复 `BEAM START` 及采集，并恢复原显示/冻结状态。

这里的“焦点下发”最终是生成并写入 `delay_profile`，不是当前 PS 已有一条 `FOCUS SET` 命令。参考恢复流程与当前 Wi-Fi“配置后保持停机”语义不同，不能直接移植。

## 运行控制及协议

- 配置事务：`CFG BEGIN <seq>`、`CFG END`、失败时尝试 `CFG ABORT`；逐条等待 ACK/NACK。
- 扫描控制：`BEAM START/STOP`，支持循环测试及指定最终运行/停止状态。
- USB 高压控制：`HV ENABLE/DISABLE`；组合成像 START 为先高压后扫描，STOP 为先扫描停止再关闭高压。
- USB 原始命令：可重复 `--cmd`，例如 `GAIN SET 1`；走配置事务之外的运行命令通路。
- TX 回读：UART/USB 都提供 `--tx7332-rd`；AFE 按行回读入口在独立参数 GUI，也可使用 USB 原始命令通路提交支持的命令。
- 传输设置：串口、波特率、超时、分块字数；USB VID/PID、端点和最大回执长度。这些设置是 PC 通信参数，不是成像参数。

没有发现专门的“设置高压电压值”命令入口，现成高压控制是使能/关闭。
寄存器表下发可以改变相应芯片寄存器，但不等于软件已经实现所有寄存器位的物理意义编辑器。

## 只在 PC 生效、不下发硬件的功能

`ultrasound_gui.py:2333` 起的显示设置最终调用 `ultrasound_pipeline_adapter.py`：

- 动态范围、亮度、对比度、噪声底。
- 灰度映射 LUT。
- 软件 TGC 曲线、启用/关闭、保存/加载。
- 显示深度/裁剪和图像叠加信息。

软件 TGC 与 AFE DTGC 是两条不同链路；PC 显示深度与上述提交后实际下发 TX 延时表的焦点功能也不同。

## 与当前 Wi-Fi 工程的兼容性边界

- 参考默认 `t_timing_default.json` 的点数是 **2048**；当前工程原始点数固定 **6144**，仅允许通过最后一项抽取改变输出长度，不能直接下发参考默认整套时序。
- 当前 Wi-Fi `CFG END` 保持高压/扫描关闭；不能照搬参考说明或 GUI 中配置后自动恢复成像的行为。
- 通道映射、TX 延时布局、定点格式、表深度及 AFE 原始配置需逐项核对，不能只因命令名称一致就认定可直接套用。
- 当前工程已有 Wi-Fi JSON 配置工具，不表示这些物理参数都已有网页编辑面板；本次仅盘点，没有新增下发功能。
