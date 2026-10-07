# 软件验证记录

验证日期：2026-09-30（Asia/Shanghai）。修改由本次任务创建在 `esp32c61/` 内；现有及并发产生的 Vivado/迁移报告变更保持未处理。本记录不代表板级验收。

## 构建

- ESP-IDF：6.1.0；源码 `D:\Program Files\esp\v6.1\esp-idf`。
- 环境：安装器 PowerShell profile，Python 3.13.5，RISC-V GCC 15.2.0（esp-15.2.0_20251204）。
- 目标：ESP32-C61，Flash 4 MiB，1500 KiB factory app，Quad PSRAM 40 MHz。
- `build.ps1` 最终构建、链接及分区大小检查退出 0。
- 最终构建日志：`build/log/idf_py_stdout_output_37776`、`build/log/idf_py_stderr_output_37776`。
- 固件：`build/ultrasound_psram_udp.bin`，866000 字节（0xD36D0）。
- SHA256：`2EE01266C7C1383D54642EE865DF4D8FBB08FCCDA330638ADA7AA4F43BF3B80C`。

IDF 首次 CMake 配置会输出上游 Wi-Fi/WPA private include 依赖警告和 NimBLE/FATFS Kconfig bool default 提示。没有修改 SDK；最终新增 C 源码编译和链接通过。

## 资源

| 项目 | 已使用 | 余量 / 说明 |
| --- | ---: | --- |
| 应用分区（1536000 字节） | 866000 字节，56.38% | 670000 字节，43.62% |
| DIRAM 链接期区域（256624 字节） | 113635 字节，44.28% | 142989 字节，55.72% |
| DIRAM 指令 | 72908 字节 | 包含在 DIRAM 总量中 |
| DIRAM `.bss` + `.data` | 40727 字节 | 包含在 DIRAM 总量中 |
| PSRAM 自测缓冲区 | 787456 字节，运行时分配 | 真实容量/可用块需板上检测 |
| 内部 SRAM staging | 1440 字节，运行时分配 | FreeRTOS/Wi-Fi/HTTP 动态分配需板测 |

静态 DIRAM 余量不能当作运行时空闲堆，任务栈和网络缓冲区需启动后由 `/api/status` 的内部堆低水位观察。未测量真实无线吞吐和功耗。本次无 RTL 增量，LUT/BRAM/DSP、FPGA 时序和 CDC 报告不适用。

## 自检查测试

最终命令：`tests/run_tests.ps1`，退出 0。

- C：GCC 主机编译启用 `-Wall -Wextra -Werror -Wpedantic`，测试通过。
- `raw_transfer.c`：51 个可执行行，56 个分支；行执行、分支执行及分支取值均 100%。覆盖范围检查、请求拒绝、单次完成、原始字节、尾包、突发让步、临时失败/重试耗尽、取消、断网、期限到达。
- `config_model.c`：32 个可执行行，74 个分支；行执行、分支执行及分支取值均 100%。覆盖 IPv4/端口/字符串界限、密码格式、保存失败保留快照、保存后从模拟存储恢复。
- Python：12 项 unittest 全部通过，包括真实回环 UDP、接收 CLI binary/partial 保存与退出码、异常包、HTTP+UDP 模拟稳定性工具、内容损坏时停止和非法时长拒绝。
- 网页：6 项 Node 测试全部通过，运行的是固件内嵌 HTML 的实际脚本，使用 DOM/API test double，覆盖首次配置、目标保存、上传/停止、保存失败、密码省略/清空、断线恢复与状态显示。
- 最终覆盖率输出：`tests/output/20260930-220503713/`。

上述覆盖率只对应两份可移植核心代码。NVS 保存/恢复的测试使用模拟持久化后端；不代表对真实 Flash 掉电原子性、FreeRTOS 调度、Wi-Fi 驱动或完整固件达到同等覆盖率。

## 未完成的实机验证

- 没有自动烧录板卡，也没有在真实 ESP32-C61 上读取 PSRAM 或传输无线数据。
- 浏览器控制环境因 Windows sandbox `setup refresh` 错误连续两次无法启动；页面通过脚本交互模拟验证，尚未完成真实浏览器视觉和实际设备网页测试。
- 首次配网、热点恢复、15 秒关闭热点、两台接收主机在线切换、真实 NVS 复位/掉电恢复和 48 小时连续运行需要板级验证。
- `pc_tools/soak_test.py` 及 README 中的步骤已交付；本机只运行短时模拟回归，没有运行 48 小时板测。
