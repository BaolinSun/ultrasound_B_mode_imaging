# 删除软件时钟配置模块（2026-10-13）

用户明确确认板上时钟由硬件直接生成，要求删除 CLOCK 目录。

## 授权范围与删除前快照

仅删除以下两处应用源码目录下的 AD9512.c/.h、system_clk.c/.h（每处 4 个文件，共 8 个文件），以及删除后空目录；同步更新 main.c 的旧 include/调用与 Vitis 源列表。不修改时钟 RTL、XSA、PS7/FSBL 系统时钟初始化、SDIO 时钟或 BSP。

- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\src\ps\CLOCK`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\src\CLOCK`

删除前源码版本：`a08b4b0`。逐文件备份：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\workspace_backups\clock_removal_before_20261013`，包含 12 个目标/关联原文件，复制后逐一校验 SHA256 相同，可按对应相对路径恢复。

`hw_spi_init()` 仅供 CLOCK 中 AD9512 访问使用；全应用检索未见其他模块使用该函数或 xspips 实例。AFE5832 配置通过独立 PL AXI SPI 控制块，不依赖此 PS SPI 初始化。温度、HV 等仍使用 IIC，保留 IIC 初始化及原有启动等待。

## 执行与验证

两处 CLOCK 目录及各自 4 个文件均已删除。main.c 中删除 `CLOCK/system_clk.h` include、`hw_spi_init()` 和 `sys_clk_init()` 调用；保留原有启动等待、IIC/HV、SDIO 与 UART 初始化。UserConfig.cmake 删除 CLOCK include 目录及两个 C 编译条目。规范源/当前 Vitis 源同步，温度模块两份文件均未修改。

四个 C 文件为旧编码，已在严格解码检查和备份哈希确认后按精确绝对路径删除；四个头文件和关联源/配置通过补丁清理。目录删除前确认在项目内、非链接、内容为空，没有递归删除其他目录。

**实际 Vitis 2026.1 构建结果：**

- SDIO 默认：使用 IDE 的 `empyro.bat build_app` 后端在当前工作区构建成功，退出 0。
- USB：同一 Vitis 2026.1 CMake/ARM 工具链在隔离的 `build/tcp_pipeline_usb` 构建成功，仍使用 `-DDATA_OUTPUT_MODE=DATA_OUTPUT_USB`，不修改当前默认宏。
- 编译单元从 27 降至 25。两种 ELF 都不含 `hw_spi_init/sys_clk_init/clk_61e2_init/ad9512_init/xspips/tx7332_set_freq/afe5832_set_freq`，仍包含 IIC_Init、hv_init、read_temp、sdio_bridge_init、zynq_uart_log_init 和 __wrap_outbyte。

| 模式 | text | data | bss |
|---|---:|---:|---:|
| SDIO | 155191 B | 140588 B | 948816 B |
| USB | 150359 B | 140588 B | 160400 B |

相对上次已清理版本，SDIO text 减少 6840 B、data 减少 320 B、bss 减少 64 B。仍存在其他模块历史编译告警，本次没有宣称零警告。

| ELF 绝对路径 | SHA256 |
|---|---|
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf`（当前 Vitis 下载使用） | 025A0C3EFE78D7AD417D2794003E355A4D53CA1B5955B482EBC6BF22287902F7 |
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\tcp_pipeline_usb\B_mode_focus_imaging.elf`（USB 构建验证） | EF69F368459FD68169E86351C96BDE4D4ABF57C1E8387914C0A7B77DD94630A8 |

本地构建日志：`build/reports/vitis_clock_removed_sdio_20261013.log`、`build/reports/vitis_clock_removed_usb_20261013.log`。

自检查新增硬件时钟无需旧初始化场景，总计 7 项源码清理测试通过：删除集合（累计每份 22 个文件）、无悬空 include、温度和核心源同步、当前编译列表、重新生成排除空目录/缓存、保留 IIC/HV/SDIO 初始化、迁移检查不读取已删文件。测试显式使用 UTF-8 读取 UTF-8 源码，旧编码注释只按字节读取 ASCII include，避免依赖 Windows 当前代码页。

现有数据链路软件回归通过：102 项 Python、8 项 Node 网页及 C 数据路径/故障注入测试。结果另见本地 `esp32c61/tests/vitis_clock_removal_regression.log`。

## 未执行范围

本次不下载或运行板卡，不修改 ESP32 固件，不更新 bit/XSA/FSBL/BOOT.BIN。时钟由硬件提供是用户确认的板级条件，本次软件构建不验证实际时钟幅度/频率、器件上电时序或长期板级稳定性。没有重新运行 Vivado 时序/CDC 验收，也没有将既有风险标记为已解决。`build/output` 中旧 ELF/BOOT 输入未同步，正常 Vitis 下载请使用上表工作区的新 ELF。
