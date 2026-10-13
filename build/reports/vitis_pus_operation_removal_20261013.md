# 删除 pus_operation 包装层（2026-10-13）

用户要求删除 pus_operation 及其中的文件，将 AFE5832_init() 直接放在 main.c 中调用。

## 范围与备份

规范源和当前 Vitis 源各删除 pus_operation/pus_oper.c、pus_oper.h（共 4 个文件），随后删除两处空目录。main.c 移除包装头文件和 pus_init()，直接调用既有 AFE5832_init()；更新 Vitis 编译源和 include 列表、自检查。AFE5832 驱动本体、PL SPI、时钟、USB/SDIO、温度与 UART 模块不改。

- 规范源：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\src\ps`
- Vitis 源：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\src`
- 删除前基线：`5c7e22b`。备份：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\workspace_backups\pus_operation_removal_before_20261013`，8 个原文件逐一校验 SHA256 相同。

原 pus_chip_init() 在 AFE5832_init() 后等待 1 秒，main 又等待 1 秒。直接调用后合并为 sleep(2)，不缩短原有稳定等待，不改初始化位置或 AFE 配置内容。旧包装中的调试提示随包装移除（TX7332 OK 并未对应实际 TX7332 初始化调用）；正常日志桥和应用启动日志不变。

工作区 main.c 中用户已有的注释/调试提示清理保留在工作区，不覆盖，不混入本次提交；历史 ESP32 日志/文档删除不恢复或提交。

两处 pus_operation 目录及 4 个文件已删除，删除前确认目标在项目内且有字节备份，目录确认空、非链接后非递归删除。main.c 现在直接调用 AFE5832_init()，并 sleep(2) 保留原总等待。Vitis include/源列表没有遗留 pus_operation 条目。可以从备份或删除前提交 6cec8eb 恢复。

## 验证结果

- 9 项自检查通过：本次新增检查 main 中 AFE5832_init 恰好调用一次，位于 GPIO/IIC/HV 初始化之后、para_down/USB/SDIO 初始化之前，等待 2 秒后再 led_init；两份源码均无包装调用或目录。
- 当前默认 SDIO 模式通过 Vitis 2026.1 IDE 的 empyro.bat build_app 后端构建；编译单元减少至 24。
- 隔离 USB 模式通过同一 Vitis 2026.1 CMake/ARM 工具链构建，不改变默认宏。
- 两个 ELF 均存在 AFE5832_init、__wrap_outbyte、sdio_bridge_init 和 read_temp，不含 pus_init、pus_chip_init。既有其他模块告警未扩展处理。

| 模式 | text | data | bss |
|---|---:|---:|---:|
| SDIO | 155063 B | 140588 B | 948816 B |
| USB | 150231 B | 140588 B | 160400 B |

| 生成 ELF 绝对路径 | SHA256 |
|---|---|
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf`（当前 Vitis 下载使用） | EAB8D764306C8132FFF6654543072A646C01D67CD6D26B517C55BE32B92A0AC5 |
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\tcp_pipeline_usb\B_mode_focus_imaging.elf`（USB 构建验证） | 6751A18CF29A8FBB90D3B8AB4CF773E41D931427386FB827F2D7B2F69AD017C5 |

本地日志：build/reports/vitis_pus_removed_sdio_20261013.log、vitis_pus_removed_usb_20261013.log。本次不下载硬件，不更新 BOOT.BIN、bit/XSA、ESP32 固件或 build/output 的旧 ELF。未进行实际 AFE 上电板测或 48 小时运行，也不以软件构建代替时序/CDC 验收。
