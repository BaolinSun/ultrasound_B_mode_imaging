# Vitis 2026.1 源码清理结果（2026-10-13）

## 完成范围

按用户确认，保留温度模块 `tmp/tmp.c`、`tmp/tmp.h`，删除其余已审阅候选。规范源 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\src\ps` 与 Vitis 应用 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\src` 各删除 18 个文件，共 **36 个文件路径**；同时精确移除 12 个已空目录，没有递归删除其他目录。

每份源码的删除清单：

| 模块 | 文件 |
|---|---|
| 旧 AXI DMA | data_transfer/axidma.c、axidma.h |
| 压缩算法 | data_process/compress.c、compress.h |
| 旧 Linux Wi-Fi | linux_wifi/linux_wifi.c、linux_wifi.h |
| EEPROM 占位 | eeprom/eeprom.c、eeprom.h |
| 已全部注释的旧 TX7332 驱动 | TX7332/tx7332.c、tx7332.h、tx7332_spi.c、tx7332_spi.h |
| 旧按键 | key/key.c、key.h |
| 旧 TTC 延时 | timer/timer.c、timer.h |
| 空接收接口 | data_transfer/data_recv.c、data_recv.h |

同步移除 main.c、usb.c、pus_oper.c 中失效 include/空调用和旧注释调用，移除 main.c 中已不再相关的四个未使用变量；更新 Vitis UserConfig.cmake 的源码与 include 列表。温度、AFE5832、HV、GPIO、IIC、时钟、BRAM 参数、USB 配置/数据、SDIO、UART 日志功能保留。

生成脚本 `build/scripts/build_vitis_2026_1.py` 不再保留已删除 AXI DMA 的排除特例，且忽略隐藏编辑器缓存和不含源/头文件的空目录。迁移检查 `build/scripts/check_migration.ps1` 改为验证旧接收/DMA 文件已删除，不再读取已删除的按键、TTC、接收文件；仍保留 UART/USB SDT 中断检查。

## 备份与恢复

删除前已确认 Git 只有用户删除的两个 ESP32 历史日志，没有其他未提交源码修改；历史日志删除未混入本次提交。

- 删除前候选及批准范围快照提交：`e03c93f`。
- 逐文件字节备份目录：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\workspace_backups\source_cleanup_before_20261013`，共 47 个原文件，复制后逐一检查 SHA256 相同。
- 备份按原工程相对路径分层；需要恢复时可从对应位置复制回原路径。备份为本地忽略目录，不上传 Git；删除前提交/父版本也保留源码。
- 四个旧编码的 compress 文件不能被 UTF-8 补丁工具读取，已在验证精确路径和字节备份后用原生文件操作删除，未转码其他源码。

温度模块两份源码均未修改，清理前后 SHA256：

| 文件 | SHA256 |
|---|---|
| tmp/tmp.c | D5026964A728579AAEA2903BE47726EDF32E9B310C7A6221835C5F1DAB0FDDDB |
| tmp/tmp.h | 1F8108228C8E5B34F35F2862D101450A676E75513E39669B5247D1319D8A7C3B |

## 构建验证

使用实际 `D:\AMDDesignTools\2026.1\Vitis` 工具，不是仅语法检查：

- **SDIO 默认模式**：通过 Vitis IDE 使用的 `empyro.bat build_app` 后端构建现有工作区，并再次执行增量构建，均退出 0。
- **USB 模式**：使用同一 Vitis 2026.1 CMake/ARM 编译器在隔离的 `build/tcp_pipeline_usb` 中编译，C_FLAGS/CXX_FLAGS 为 `-DDATA_OUTPUT_MODE=DATA_OUTPUT_USB`，不修改源码中默认的 SDIO 宏。
- 当前构建编译单元由 35 减至 27；两份剩余源码的 55 个 C/头文件内容逐一归一化换行后比对一致。
- ELF 符号确认两种模式均保留 `__wrap_outbyte`（日志包装）、SDIO 接口、`read_temp`，SDIO 还保留 `zynq_uart_log_init`；编译期模式差异及独立测试帧存储未被破坏。

| 模式 | text | data | bss |
|---|---:|---:|---:|
| SDIO 清理前（已有 build.log） | 176987 B | 141004 B | 953072 B |
| SDIO 清理后 | 162031 B | 140908 B | 948880 B |
| USB 清理后 | 157199 B | 140900 B | 160464 B |

SDIO 减少 text 14956 B、data 96 B、bss 4192 B。不代表 ESP32 SRAM 释放或 Wi-Fi 吞吐提高，两个处理器的内存不同。

| 生成的 ELF（绝对路径） | SHA256 |
|---|---|
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf`（正常 Vitis 下载使用） | AD7DF5E35ED22960A10E71D139074A750C8250BC84D675018CE6583ED4C7D004 |
| `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\tcp_pipeline_usb\B_mode_focus_imaging.elf`（仅 USB 构建验证） | 4896B5E26BD0661EEDBEBB7551ABF8107D24599375B82203BB60EFB8B3C9A06C |

构建日志为本地忽略文件：`build/reports/vitis_cleanup_sdio_20261013.log`、`vitis_cleanup_sdio_rebuild_20261013.log`、`vitis_cleanup_usb_20261013.log`。

## 软件自检查

- 新增 `build/scripts/tests/test_vitis_source_cleanup.py`，6 项通过：确认删除集合、温度接口保留/源码同步、无失效 include/空调用、当前 CMake 源列表完整、重新生成配置的幂等性及缓存/空目录排除、迁移检查不读取已删源码。
- 现有 ESP32/Zynq 软件回归：102 项 Python、8 项 Node 网页测试及 C 数据路径/故障注入测试通过，包含 USB/SDIO 宏模式、TCP/UDP、UART 桥与日志包装。
- 修改后的迁移检查 PowerShell 语法解析通过。没有执行它的全量历史迁移/时序验收，也没有用本次软件结果宣称既有时序/CDC 风险已解决。

## 注意事项

本次没有下载/运行板卡、修改 ESP32 固件或 RTL，没有重新生成 bit/XSA/FSBL/BOOT.BIN，也没有替换 `build/output` 中旧打包输入。正常 Vitis 下载指向上表工作区的新 ELF；若要让 SD 卡启动镜像也包含本次清理，应单独重新打包 BOOT.BIN，不能把现有 BOOT.BIN 当作本次已更新。

编译通过不等于零警告：保留的温度模块 `temp_init()` 原有缺少返回值警告仍存在，USB/系统控制等历史告警未扩展修改。没有实测硬件，也没有进行 48 小时稳定性验收。

复查命令（在工程根目录）：

```powershell
python -m unittest discover -s .\build\scripts\tests -p test_vitis_source_cleanup.py -v
```
