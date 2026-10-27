# CPU1 固定复位与停钟

> 本文保留历史实现及其验证记录。当前已改为应用和 FSBL 启动仅观察，CPU1 关闭由网页显式触发；当前操作及更新要求见 [CPU1 网页控制](CPU1_WEB_CONTROL.md)。下述“启动即关闭”和旧产物表不再代表当前版本。

## 行为与边界

本功能基于 `b28d456`，分支 `feature/cpu1-clock-stop`。仅 CPU0 运行应用；CPU1 在应用启动和 FSBL 交接时被置于复位、停止时钟状态，没有运行时启用入口。AFE/TX 的待机与唤醒不会解除 CPU1 的限制。

前提是当前 CPU1 未承载 AMP/Linux 等其他任务；本函数不是让正在工作的第二核安全保存上下文、退出任务的通用下电接口。

这是 CPU1 数字控制的复位和停钟，不是单独断电；不承诺具体功耗下降比例。CPU0 仍执行原有前台轮询，未加入 WFI、不调整 PLL、DDR、PL 或模拟芯片配置。

用户选择的失败策略为：**告警后继续原有功能**，不因 CPU1 关闭未确认而新增扫描或高压门控。

## 寄存器及启动处理

- 仅允许 CPU0 操作。对 `0xF8000244` 读改写，先置 bit1 `A9_RST1`，读回确认后再置 bit5 `A9_CLKSTOP1`；不清除 CPU1 已有控制位，也不覆盖 CPU0、共享外设和其他位。
- 写入后执行 DSB/ISB 和读回；每阶段以运行中的全局计时器限制为 10 ms。另外设置有限轮询次数，防计时器失效造成永久卡死；计时器失效时不能保证真实墙钟时间仍为 10 ms。
- 通过 `SLCR_LOCKSTA` 保存锁定状态，只在必要时解锁，结束时恢复原状态。已禁用时不重复写寄存器，不先恢复 CPU1 时钟。
- CPU0 应用在 UART 日志初始化后、应用中断初始化前执行；Vitis 直接下载应用无需先运行定制 FSBL。
- FSBL 的 `FsblHookBeforeHandoff()` 调用相同算法。JTAG 初始化若在此钩子之前退出或使用 ps7_init，应用入口仍负责禁用。FSBL 失败不改变成功交接返回值。
- UART0/UART1 及 USB 中断映射使用 CPU 编号 0，而不是值为 1/2 的 CPU 位掩码。USB 移除 CPU1 目标、保留 CPU0 目标。

依据 [AMD CPU 复位/停钟寄存器](https://docs.amd.com/r/en-US/ug585-zynq-7000-SoC-TRM/Register-A9_CPU_RST_CTRL-Details)、[SLCR 解锁](https://docs.amd.com/r/en-US/ug585-zynq-7000-SoC-TRM/Register-SLCR_UNLOCK-Details)与[锁定](https://docs.amd.com/r/en-US/ug585-zynq-7000-SoC-TRM/Register-SLCR_LOCK-Details)。

## 日志与状态

应用日志示例（数值以真实寄存器为准）：

```text
@CPU1 disabled=1 before=00000000 after=00000022 error=NONE lock_restored=1
```

FSBL 对应前缀为 `@CPU1_FSBL`。`disabled` 指读取的两个控制位均已置位；错误和锁定恢复结果应同时检查，不是电流测量。
失败时另外输出 `WARNING ... continuing ...`，错误可为非 CPU0、解锁失败、复位/停钟超时、其他位变化或锁定恢复失败。不自动重试、不复位整个 PS。

原 `STATUS` 回执末尾新增 `cpu1_disabled=0|1`，每次查询实时读取控制位。原 ESP32 可在其 PS 原始状态中显示，不需更新固件：

```powershell
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "STATUS"
```

该命令在 `esp32c61` 目录执行，不自动启动扫描或高压。`mrd 0xF8000244` 也可只读核对：`value & 0x22` 应为 `0x22`，但读回不代表独立断电。

CPU1 因复位、停钟而无法继续运行/单步是预期结果；CPU0 和 JTAG 主调试通路仍应可用。若 CPU0 也无法访问，不应当作本功能的正常现象，需另行排查。

## 构建与回退

规范 PS 源与当前 Vitis 源同步修改，不整体覆盖用户 main.c 的已有差异。现有 Vitis CMake 已加入 CPU1 源文件，可继续在 IDE 构建。
FSBL 钩子通过工程本地 `build/scripts/cpu1_fsbl.py` 维护，不修改全局 SDK；平台生成后显式重编、检查 ELF 诊断字符串并核对导出文件，避免交付旧 FSBL。

在成像工程根目录可运行：

```powershell
.\build\scripts\test_cpu1.ps1
.\build\scripts\build_cpu1.ps1
```

后者强制使用全新、工程 build 下的隔离工作区，构建平台、FSBL 和 USB/SDIO 两种应用；拒绝删除或覆盖已有工作区。它不生成 BOOT.BIN、不连接硬件。

本次不更换 bit/XSA 或 ESP32 固件，不覆盖 BOOT.BIN。新软件产物与哈希见本次交付记录；旧产物备份在 `build/backups/cpu1_b28d456/`。Git 切换不能自动恢复生成的 ELF，应配套恢复应用及 FSBL。

下载新应用后可在当次运行中生效；若重新上电仍从旧 BOOT.BIN 启动，旧应用不含本功能。电池独立启动的持久化更新需后续单独确认并重新打包启动镜像，本次没有执行。

## 验证与待板测项

核心自检查覆盖有序读改写、重复调用、部分置位、非 CPU0 拒绝、SLCR 锁恢复、失败与计时器停止、计数回绕；行、分支方向及调用覆盖率均为 100%。
集成检查覆盖 FSBL 重生成/幂等、保留用户钩子、旧源拒绝、入口顺序、源同步及 IRQ 核编号；原 PS 命令测试模拟 CPU1 未确认关闭时仍允许原有操作。

尚需单独授权板测：上电和 Vitis 下载两种启动方式、CPU0/JTAG 正常、通信与成像、AFE/TX 循环后 CPU1 位保持不变，以及相同条件下的 PS 电源轨电流比较。离线测试不代表上述项目已通过。

## 2026-10-25 离线交付结果

- CPU1 核心：35 个可执行行、28 个分支方向和 17 处调用均覆盖 100%。
- 构建脚本集成回归 28 项通过；完整主机回归 214 项（213 通过，1 项历史 TXT 缺失跳过），网页 28 项、图像网页 5 项通过。原生 C 回归通过，包括 CPU1 未禁用时仍能按原规则执行命令的模拟测试；测试没有向板卡发送高压或扫描命令。
- Vitis 2026.1 新平台/FSBL、SDIO 和 USB 应用均构建成功。新 FSBL 的 CPU1 符号、诊断字符串及导出文件一致性已核对，当前工作区 FSBL 也完成重编。
- SDIO text/data/bss：214635/140604/165872 B；USB：208363/140604/164816 B。相对基线应用 text 各增加 2004 B，data/bss 不变。
- SDIO/USB 应用 end 分别为 `0x00180FD0` / `0x00180BB0`，与 USB `0x1C000000` 及图像槽位 `0x20000000..0x202FFFFF` 无重叠。
- 未修改 RTL、约束或 Vivado 工程，因此未重新运行综合、时序、CDC；不宣称解决既有报告风险。未操作板卡或实测功耗。

交付目录绝对路径：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/cpu1_20261025/`。下表文件名均位于此目录：

|文件|SHA256|
|---|---|
|`B_mode_focus_imaging_sdio.elf`|`B23FDF7850F83A3A0C968DBC9EFA7FAA3E15D0607AFE0E6C5BEA4541A9668E01`|
|`B_mode_focus_imaging_usb.elf`|`4BDEA243C3BF3D4FBC529D6DFE8E4CA783FCD427858C7476E95CD5D534746DBA`|
|`zynq_fsbl.elf`|`86457EA0AB1A2F8E42F2F7F6FD25E2E6EB35D5E5E330CA7E9C01C10EF39E644B`|
|`zynq_fsbl_vitis_workspace.elf`|`F80F05CCD56ADA1ECEAE23A71389DCE47F07815C19DA4321CC5B239569E30137`|

两个 FSBL 分别使用隔离平台 BSP 和当前 IDE 工作区 BSP 构建，因此 ELF 哈希不同；它们的 CPU1 共用源逐字节一致，核心源码 SHA256 为 `1d53229204661e7ae07acd5f802829c235290af22aab05eb082372c7ab59059a`。

常用文件同步如下：

- 应用：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/B_mode_focus_imaging.elf`（SDIO）。
- Vitis 默认应用：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/build/B_mode_focus_imaging.elf`（同一 SDIO ELF）。
- FSBL：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/zynq_fsbl.elf`（隔离平台构建）。
- 当前 IDE FSBL：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/ubmi_p/export/ubmi_p/sw/boot/fsbl.elf`（当前工作区重编）。
- 新 BSP：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/cpu1_platform_20261025/ubmi_p/export/ubmi_p/sw/standalone_ps7_cortexa9_0/`。

完整清单为 `esp32c61/logs/cpu1_build_manifest_20261025.json`。bit/XSA 哈希仍分别为 `E17F6E53FDD8C594243566DFD708E61AF2B5B41F8D94159D6867FA03FBE63239` / `7A7B1223E515C772E7600B158CBDE768F122377CB7F662018F98B0F5529D3754`，BOOT.BIN 仍为 `30BE9B06D8E058C6D26B249EA960DC460CB467EE3EF7D57B3812F2DDDB256360`。
