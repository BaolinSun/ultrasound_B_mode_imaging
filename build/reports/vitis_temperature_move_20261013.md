# 温度模块移动记录（2026-10-13）

将规范源和当前 Vitis 工作区的 tmp/tmp.c、tmp/tmp.h 分别原样移动至 system_ctrl/tmp.c、system_ctrl/tmp.h；删除各自空 tmp 目录。只改路径、引用、编译列表和自检查，不调整温度算法或接口，不下载硬件。

- 规范源：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\src\ps`
- Vitis 应用：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\src`
- 移动前备份：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\workspace_backups\temperature_move_before_20261013`，8 个原文件，逐文件 SHA256 校验通过。

开始操作时发现用户已从工作区 tmp.c 中移除空 temp_init()，规范源仍保留该空函数。分别保留两份当前内容，不用其中一份覆盖另一份，不恢复或提交用户删除的其他 ESP32 历史文档/日志。头文件中的已有声明也不借本次移动扩展修改。

本次仍保留默认 SDIO 宏。移动后的相对层级相同，tmp.c 的 `#include "tmp.h"` 和两份温度文件的 `../iic/iic.h`、`../main.h` 无需修改。main.c 引用改为 `system_ctrl/tmp.h`，Vitis 编译源改为 `system_ctrl/tmp.c`，删除 tmp include 目录（system_ctrl 原已在 include 列表）。

移动前检查目标不存在、绝对路径均在工程内；分别原生移动文件，移动后四个文件与各自备份 SHA256 完全相同。旧 tmp 目录确认无剩余内容、不是链接后非递归删除。可以从上述备份或移动前提交 `e2116d6` 恢复。

执行期间工作区 main.c 又出现用户对注释/DEBUG_PRINT 的清理，完整保留在工作区；提交时只暂存本任务改动的 include 行，不混入这些编辑。Vitis 对新文件加入了一条重复的编译项，已去重，当前 system_ctrl/tmp.c 只编译一次。

## 验证结果

- 8 项重定位/源码清理自检查通过，确认新路径存在、旧目录不存在、include 和编译列表无失效路径、温度 read_temp/IIC 接口保留、再次生成配置幂等且不纳入空 tmp 目录。
- 通过 Vitis 2026.1 IDE 的 empyro.bat build_app 后端构建默认 SDIO 模式成功。
- 使用同一 Vitis 2026.1 CMake/ARM 工具链在 build/tcp_pipeline_usb 构建 USB 宏模式成功，不修改当前默认 SDIO 宏。
- 两种 ELF 均存在 read_temp、__wrap_outbyte 和 sdio_bridge_init。工作区用户删除的空 temp_init 未恢复；本次没有统一该既有差异，也没有修改头文件中的既有声明。

| 原样移动后的文件（各自根目录 + system_ctrl） | SHA256 |
|---|---|
| 规范源 system_ctrl/tmp.c | D5026964A728579AAEA2903BE47726EDF32E9B310C7A6221835C5F1DAB0FDDDB |
| 工作区 system_ctrl/tmp.c | F7F292DC4D6B2E273841FAA187F1A2AAF9FBF5C7ECBC35E57AEF59BBAFD70395 |
| 两份 system_ctrl/tmp.h | 1F8108228C8E5B34F35F2862D101450A676E75513E39669B5247D1319D8A7C3B |

| 当前构建 ELF 绝对路径 | SHA256 |
|---|---|
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf` | 2A822BA81350E702844EA4DD9FBA4EAFE0E0620419ABBC2175E24AD5C82540C4 |
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\tcp_pipeline_usb\B_mode_focus_imaging.elf`（USB 构建验证） | 0AFD41905AEC914DB613014C21235EC32525CF6A89318BDD9B4D344A758E7EC4 |

本地构建日志：build/reports/vitis_temperature_move_sdio_20261013.log、vitis_temperature_move_usb_20261013.log。编译通过不代表零历史告警。此次不执行板测、不修改 RTL/CDC/时序约束，BOOT.BIN、bit/XSA、ESP32 固件以及 build/output 中旧 ELF 均不更新。
