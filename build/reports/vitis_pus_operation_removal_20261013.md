# 删除 pus_operation 包装层（2026-10-13）

用户要求删除 pus_operation 及其中的文件，将 AFE5832_init() 直接放在 main.c 中调用。

## 范围与备份

规范源和当前 Vitis 源各删除 pus_operation/pus_oper.c、pus_oper.h（共 4 个文件），随后删除两处空目录。main.c 移除包装头文件和 pus_init()，直接调用既有 AFE5832_init()；更新 Vitis 编译源和 include 列表、自检查。AFE5832 驱动本体、PL SPI、时钟、USB/SDIO、温度与 UART 模块不改。

- 规范源：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\src\ps`
- Vitis 源：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\src`
- 删除前基线：`5c7e22b`。备份：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\workspace_backups\pus_operation_removal_before_20261013`，8 个原文件逐一校验 SHA256 相同。

原 pus_chip_init() 在 AFE5832_init() 后等待 1 秒，main 又等待 1 秒。直接调用后合并为 sleep(2)，不缩短原有稳定等待，不改初始化位置或 AFE 配置内容。旧包装中的调试提示随包装移除（TX7332 OK 并未对应实际 TX7332 初始化调用）；正常日志桥和应用启动日志不变。

工作区 main.c 中用户已有的注释/调试提示清理保留在工作区，不覆盖，不混入本次提交；历史 ESP32 日志/文档删除不恢复或提交。

实施后补充 Vitis 2026.1 USB/SDIO 构建与自检查结果。本次不下载硬件，不更新 BOOT.BIN、bit/XSA、ESP32 固件。
