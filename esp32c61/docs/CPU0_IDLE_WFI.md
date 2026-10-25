# CPU0 安全空闲与 WFI

## 使用范围

本功能基于 `124d132`，分支 `feature/cpu0-idle-wfi`，仅修改 PS 应用及离线构建、测试。AFE、TX 都已稳定进入 `sleep` 后，CPU0 才可能执行 WFI。单独 AFE 待机、单独 TX 待机、正常扫描、配置或传输时均继续前台服务。

CPU0 的中断唤醒不会自动唤醒 AFE/TX，也不会自动恢复扫描、高压或上传。原有 AFE/TX 操作入口和恢复顺序继续有效。CPU1 仍保持既有复位与停钟策略。

本次无需更换 bit/XSA、BSP、FSBL 或 ESP32 固件，不覆盖 BOOT.BIN；不调整 DDR、PLL、SCU/L2、DCache 或共享时钟。工程此前的缓存策略保持原样。本轮未连接板卡，以下板测步骤尚未执行，功耗收益没有实测结论。

## 调度及唤醒

- 主循环统一服务温度、UART 命令、AFE/TX 状态机和 USB；长发送及配置等待中的必要服务保留，避免递归进入 WFI。
- LED 正常时每 500 ms 翻转；双待机期间保持电平，不为 LED 设置唤醒。
- 双待机且传输空闲时暂停 SDIO 状态轮询、重新枚举和源端统计同步；保留控制器时钟、会话和所有权。收到恢复命令后恢复正常前台调度。
- 原有 10 秒温度周期继续运行。CPU0 私有定时器使用 PPI 29、/256 分频、单次计时，按距下一次温度采样的剩余时间唤醒；命令提前唤醒时重新计算剩余期限，不补发采样、不增加周期 tick，不修改全局计时器。
- ESP32→UART0 是主要命令唤醒通路；UART1 和 USB 的既有中断入口保留。SDIO 是轮询驱动，不作为 WFI 唤醒来源。

进入前保存 CPSR、屏蔽 IRQ、检查有界软件队列和到期事件，再设置单次定时器并再次检查。检查涵盖 UART 完整命令/错误、硬件 RX/TX FIFO 和接收中断使能、USB 请求/回执/完成/会话事件、USB IN DMA、SD1 CMD/DAT 活动、配置、电源状态、扫描/高压、DDR 租约及当前有效 PAUSE 证明。

这些原子检查只读状态，不弹出队列、不重新开启 IRQ、不读取 GIC IAR、不发送 SDIO 命令或触发双域 FIFO 清理。采用 DSB/WFI/ISB，返回后恢复原中断状态。未完成的串口行可以等待后续字节中断；两个接收中断条件未同时使能则拒绝 WFI。

依据 [Arm Cortex-A9 MPCore TRM](https://documentation-service.arm.com/static/5f03371bdbdee951c1cd68f0)，WFI 可由到达处理器的 IRQ 唤醒，即使 CPSR.I 屏蔽了异常处理；GIC 使能、路由及优先级仍必须有效。代码不关闭 GIC，因此检查与 WFI 之间到达的 IRQ 可以唤醒，恢复 CPSR 后由原 ISR 处理。

## 初始化、故障回退及状态

应用完成现有启动流程后检查 CPU0、IRQ/GIC 状态、私有定时器占用、PPI 29 处理器与使能、BSP tick/睡眠定时器冲突、优先级屏蔽和频率。占用冲突时不抢占其他使用者。

初始化执行约 1 ms 的单次定时器真实 IRQ 自检查，前台有限等待，不使用 WFI 进行自检查。失败时恢复自己接管前的处理器、优先级和定时器寄存器，打印告警，保持前台轮询与通信。运行时检查设置读回、控制状态和超期未触发事件，发现异常后禁用 WFI。

边界：如果定时器在 CPU 已进入 WFI 后物理失效，软件需等其他有效 IRQ 唤醒后才能检查并回退，不能保证在没有任何有效唤醒源时自行恢复。本功能没有增加系统看门狗或整机复位。

启动及退出稳定双待机时输出累计汇总，不逐次打印：

```text
@CPU0_IDLE enabled=1 entries=0 wait_ms_hi=0 wait_ms_lo=0 timer_irqs=1 reason=NONE
```

`entries` 为 WFI 调用次数；等待毫秒累计值为 `(wait_ms_hi << 32) | wait_ms_lo`，包含唤醒到 ISR 返回的开销，不是精确硬件低功耗驻留时间。初始化失败或运行异常时 `enabled=0`，`reason` 给出阶段；宏关闭显示 `MACRO_OFF`。

`STATUS` 末尾增加 `wfi=0|1`，表示功能启用且初始化成功，不表示查询时 CPU 正在睡眠。最长合法状态组合验证为 124 字节，小于原 128 字节内容缓冲上限，无需修改 ESP32。

```powershell
# 在 esp32c61 目录；仅查询，不自动恢复成像
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "STATUS"
```

## 宏与构建

修改 `build/src/ps/system_ctrl/cpu0_idle_config.h` 和当前 Vitis 对应头文件的默认值：

```c
#define CPU0_IDLE_WFI_ENABLE 1
```

`1` 默认允许安全 WFI；`0` 完全关闭本功能中的 WFI 指令，保留统一前台调度。其他数值编译报错。重新生成 Vitis 工程时自动收集该源文件和头文件。

以下在 `ultrasound_B_mode_imaging` 根目录离线执行，独立目录同时生成 USB/SDIO 应用，不下载、不打包 BOOT.BIN：

```powershell
.\build\scripts\build_frame_store_apps.ps1 -Wfi 1
.\build\scripts\build_frame_store_apps.ps1 -Wfi 0
.\build\scripts\test_cpu0.ps1
.\esp32c61\tests\run_tests.ps1
python -m unittest discover -s build\scripts\tests -v
```

不指定 `-Wfi` 时以头文件为准，平时 Vitis IDE 构建同样以头文件为准。测试目录 `build/vitis_frame_store_{usb,sdio}_wfi{0,1}` 不改变头文件默认值。

## 板测及 48 小时验证（待单独授权）

1. 先用 WFI=0 应用记录 UART0、UART1、USB、JTAG 和 CPU1 状态基线，确认正常温度、通信、上传及安全停止。
2. 换 WFI=1，只查询状态，再分别测试 AFE/TX 单待机：不应产生 WFI。双待机、排空后保留观察至少 30 秒，应持续约 10 秒一组温度；LED 保持不变。
3. 分别通过 UART0、UART1、USB 查询/恢复；测试短包、分段命令、重复查询和 AFE/TX 任意顺序唤醒。只唤醒 CPU0 不得自动恢复模拟芯片、扫描、高压或上传；单颗恢复后应退出稳定双待机调度。
4. 验证 USB 活跃 DMA、DDR 租约、配置事务、故障或不确定状态时不会入睡；正常成像和动态帧长 TCP/UDP 上传保持原快速路径。
5. 完成单独授权的成像恢复与吞吐回归，在相同 AFE/TX 状态及负载下比较 WFI 开关的 PS 电源轨电流、命令唤醒延迟和有效吞吐，保留原始测量条件。
6. 48 小时循环双待机/定时采温/命令唤醒，记录状态、温度间隔、命令超时、WFI 汇总、CPU1 控制位、异常复位及通信错误；检查退出待机后可恢复数据传输。不要用持续高频查询代替定时唤醒验证。

既有 Cortex APB 调试访问异常与温度相关时钟问题继续单独追踪；本次离线测试未验证或解决这些问题。不得将 CPU0 无法通过 JTAG 访问作为 WFI 的正常验收结果，也不自动通过暂停、复位或下载消除该异常。

## 离线验证与产物

2026-10-25 离线验证通过：

- 新增核心：行覆盖 100%、分支方向覆盖 100%；真实 PS 适配层在主机模拟服务下：行覆盖 100%、分支方向覆盖 93.75%。覆盖队列竞态、检查前后中断、短 UART 包、温度期限与回绕、定时器占用/失败、中断被屏蔽、USB 会话/DMA、SD1 硬件活动、DDR 租约/排空证明、四种 AFE/TX 组合及故障回退。
- 原有原始 UDP、帧 TCP/UDP、动态长度、USB、温度、UART 桥、Wi-Fi 命令及 AFE/TX 状态机离线回归通过。Python 213 项通过、1 项历史 TXT 样本测试跳过；网页测试 33 项通过；构建脚本及源码集成测试 32 项通过。
- USB/SDIO × WFI 开关四种应用，以及当前 Vitis 工作区的头文件默认构建通过。反汇编确认 WFI=1 的 `wait_irq` 含 DSB/WFI/ISB，WFI=0 无该指令。
- 编译仍有原 AFE/USB 等代码的未使用参数、符号及有符号比较警告，未在本任务中宣称清零；新增 CPU0 源无编译警告。

内存（单位 B，GNU size；不包括未分配的文件调试信息）：

|应用|text|data|bss|
|---|---:|---:|---:|
|基线 `124d132` SDIO|214635|140604|165872|
|SDIO WFI=1 / 默认|221819|140604|166000|
|SDIO WFI=0|220519|140604|166000|
|USB WFI=1|215483|140604|164944|
|USB WFI=0|214235|140604|164944|

默认 SDIO 增加 text 7184 B、bss 128 B；已分配区域结束于 `0x00181050`，不与 USB 区 `0x1C000000` 或三帧区 `0x20000000～0x202FFFFF` 重叠。

常用应用已更新为头文件默认 SDIO/WFI=1，两处 SHA256 均为 `F0981B9C49B23F09AA1517644CA5440E5BF74B956521568FB0C5E46D3551DA6B`：

- `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/B_mode_focus_imaging.elf`
- `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/build/B_mode_focus_imaging.elf`

四档对照产物位于 `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/cpu0_idle_20261025/`：

|文件|SHA256|
|---|---|
|`B_mode_focus_imaging_sdio_wfi1.elf`|`77F4A9D4683CAB6B9906165671C1710F20DE1B815C6DF083D7DB1075E837AAA6`|
|`B_mode_focus_imaging_sdio_wfi0.elf`|`C915CA40DAEE80AE9D1FA2BB5678EBF00AEB163A0EC235231D5838C29D763374`|
|`B_mode_focus_imaging_usb_wfi1.elf`|`4021EC19396457F4A3ECD8E34CA1C396E451511B5DE9B6AE02EC051AA6B1AA63`|
|`B_mode_focus_imaging_usb_wfi0.elf`|`E6401D5BD523EDABF804E76E9CD39B56D87F640E0B9641C3D31D1A7B83A7A5AB`|

`B_mode_focus_imaging_sdio_header.elf` 是默认应用的额外副本。显式 WFI=1 与默认构建的 ELF 因调试/构建信息和链接 build-id 不同而哈希不同；核对发现加载区仅 GNU build-id 的 20 字节不同，应用指令和其余加载数据一致。

基线应用及未修改产物备份在 `build/backups/cpu0_124d132/`。FSBL、bit、XSA、BOOT.BIN 与备份逐项哈希比较一致；ESP32 源及固件未改。完整绝对路径、哈希和测试摘要见 `esp32c61/logs/cpu0_idle_build_manifest_20261025.json`，日志集中在 `esp32c61/logs/cpu0_idle_20261025/`。日志和构建产物依照现有 Git 忽略规则保留本地，不纳入源码提交。

本轮软件自检查和编译结果不能替代实际唤醒、电流、JTAG 及长期稳定性验收。
