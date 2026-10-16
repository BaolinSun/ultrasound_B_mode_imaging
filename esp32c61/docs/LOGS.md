# 运行日志与测试报告

[esp32c61/logs](../logs/) 集中保存接收日志（`.jsonl`）、设备状态（`.device.json`）和控制台日志（`.log`）。历史报告只作历史记录，不代表当前固件或真实成像数据的测试结果。

PC 工具的 `--log image_one.jsonl` 自动保存到 `esp32c61/logs/`，不依赖启动工具的工作目录。显式指定绝对路径或其他子目录时尊重指定位置；缺失的父目录自动创建。未指定 `--log` 的接收工具仍只打印结果，不自动生成日志。

`sdio_benchmark.py` 的接收结果、`.device.json`、`.stdout.log`、`.stderr.log` 保存在同一目录。图像十六进制 TXT 仍由 `--output-dir` 指定，不放在这里。固件、构建和编辑器所需的配置 JSON 不迁入此目录。
