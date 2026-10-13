# 删除软件时钟配置模块（2026-10-13）

用户明确确认板上时钟由硬件直接生成，要求删除 CLOCK 目录。

## 授权范围与删除前快照

仅删除以下两处应用源码目录下的 AD9512.c/.h、system_clk.c/.h（每处 4 个文件，共 8 个文件），以及删除后空目录；同步更新 main.c 的旧 include/调用与 Vitis 源列表。不修改时钟 RTL、XSA、PS7/FSBL 系统时钟初始化、SDIO 时钟或 BSP。

- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\src\ps\CLOCK`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\src\CLOCK`

删除前源码版本：`a08b4b0`。逐文件备份：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\workspace_backups\clock_removal_before_20261013`，包含 12 个目标/关联原文件，复制后逐一校验 SHA256 相同，可按对应相对路径恢复。

`hw_spi_init()` 仅供 CLOCK 中 AD9512 访问使用；全应用检索未见其他模块使用该函数或 xspips 实例。AFE5832 配置通过独立 PL AXI SPI 控制块，不依赖此 PS SPI 初始化。温度、HV 等仍使用 IIC，保留 IIC 初始化及原有启动等待。

## 执行与验证

实施后填写实际删除、构建和自检查结果。本次不下载或运行板卡，不修改 ESP32 固件，不更新 bit/XSA/BOOT.BIN。
