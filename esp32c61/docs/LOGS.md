# 运行日志与测试报告

[esp32c61/logs](../logs/) 集中保存接收日志（`.jsonl`）、设备状态（`.device.json`）和控制台日志（`.log`）。历史报告只作历史记录，不代表当前固件或真实成像数据的测试结果。

PC 工具的 `--log image_one.jsonl` 自动保存到 `esp32c61/logs/`，不依赖启动工具的工作目录。显式指定绝对路径或其他子目录时尊重指定位置；缺失的父目录自动创建。未指定 `--log` 的接收工具仍只打印结果，不自动生成日志。

`sdio_benchmark.py` 的接收结果、`.device.json`、`.stdout.log`、`.stderr.log` 保存在同一目录。图像十六进制 TXT 仍由 `--output-dir` 指定，不放在这里。固件、构建和编辑器所需的配置 JSON 不迁入此目录。

## Git 管理规则

本工程的 `logs/`、运行报告 `*.json` / `*.jsonl` / `*.log`、构建目录、Python 缓存/虚拟环境和临时编译产物均不纳入 Git；文件仍保存在本地。
已有 `.vscode/settings.json` 作为 IDE 配置例外保留；源码、显示 LUT、依赖锁文件及 SDK 默认配置继续跟踪。
此前已跟踪的运行报告已解除跟踪，可从历史提交恢复。需要版本管理的新配置 JSON 应明确加入忽略例外，不要将真实配置混入运行日志目录。

## 2026-10-21 临时生成物清理

按用户要求删除18个闲置生成目录，共20182个文件（含202个临时日志/覆盖率文件），删除数据逻辑体积约2.186 GiB。

- ESP32只保留`build`和日常默认的`build-throughput-fast-socket-exp-combined`；移除其余9个实验/对照构建目录。
- 删除`tests/output`、项目内Python缓存、两份空配置验证目录及已结束的帧缓存/流水线仿真输出。
- 清理`build`顶层过期`.log/.jou`、ESP32构建/烧录/测试控制台日志、重复的历史`.stdout/.stderr.log`，以及可再生成的验证日志/覆盖率结果。
- 保留全部66份JSON/JSONL记录（测速逐帧、设备状态、窗口统计、控制与网页日志）、图像采集TXT、Markdown报告；最新`throughput_*`控制台日志及只读板级诊断日志保留。
- Vivado/Vitis当时仍在运行，因此未删除`.Xil`、Vivado管理工程/缓存、当前Vitis/BSP/FSBL及下载产物，也未停止服务、烧录或操作板卡。
- `build/output`中的bit/XSA/BOOT/ELF等交付物、当前两份ESP32固件、根sdkconfig和`220869c`回退镜像保持原样；共26个保留文件经删除前后SHA256对照未变化。

删除闲置ESP32构建之前，已保留9组配置和可烧录文件（共45个文件）：

`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\backups\cleanup_20261021_esp32\<原构建目录名>`

每组包含`sdkconfig`、`flash_args`、应用bin、bootloader及分区表，目录关系保持不变且复制哈希一致。
如需重新测试其它档位，可按`build.ps1 -Experiment ...`重建；如需精确恢复历史构建配置，优先使用这些快照，不假定当前默认值等于历史值。

被清理的临时日志、ELF调试符号、目标文件、覆盖率和缓存未全部备份，**不能从Git直接恢复**；可重新构建/运行生成，但时间戳及历史控制台输出不一定相同。
历史报告中的部分临时日志路径因此不再存在，正式测速的JSON/JSONL及统计数据仍位于原`logs`目录，未移动或覆盖。
忽略规则继续有效，但忽略规则不等于自动删除；后续构建/测试会再次生成对应缓存和日志。

清理后`.\build.ps1 -Action size`成功，默认combined仍可构建：静态DIRAM使用135863 B，与清理前一致。
本次Git提交同时保存此前已验证的combined默认配置及本清理记录；被忽略的生成文件本身不会表现为Git删除项。
