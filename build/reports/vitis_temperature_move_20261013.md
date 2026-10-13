# 温度模块移动记录（2026-10-13）

将规范源和当前 Vitis 工作区的 tmp/tmp.c、tmp/tmp.h 分别原样移动至 system_ctrl/tmp.c、system_ctrl/tmp.h；删除各自空 tmp 目录。只改路径、引用、编译列表和自检查，不调整温度算法或接口，不下载硬件。

- 规范源：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\src\ps`
- Vitis 应用：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\src`
- 移动前备份：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\workspace_backups\temperature_move_before_20261013`，8 个原文件，逐文件 SHA256 校验通过。

开始操作时发现用户已从工作区 tmp.c 中移除空 temp_init()，规范源仍保留该空函数。分别保留两份当前内容，不用其中一份覆盖另一份，不恢复或提交用户删除的其他 ESP32 历史文档/日志。头文件中的已有声明也不借本次移动扩展修改。

本次仍保留默认 SDIO 宏。移动后的相对层级相同，tmp.c 的 `#include "tmp.h"` 和两份温度文件的 `../iic/iic.h`、`../main.h` 无需修改。main.c 引用改为 `system_ctrl/tmp.h`，Vitis 编译源改为 `system_ctrl/tmp.c`，删除 tmp include 目录（system_ctrl 原已在 include 列表）。

实施后补充移动校验及 USB/SDIO 构建结果。BOOT.BIN、bit/XSA、ESP32 固件不更新。
