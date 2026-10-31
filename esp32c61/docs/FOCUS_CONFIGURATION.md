# 网页焦点配置：DDR 预存表快速切换

## 当前实现（2026-10-31）

20～170 mm、步进 10 mm 的 16 张表由 MATLAB 直接导出 C 数组，每张 16384 个 uint32（64 KiB），共 1 MiB。表随 Zynq ELF 装入 DDR 只读段，断电后由下次启动或 Vitis 下载重新装入；不依赖运行时的 PC、MATLAB 或 USB。PL、bit、XSA、图像协议与帧槽位 ABI 未修改，不增加 DMA。

PS 默认初始化 80 mm。网页初始选择的 80 mm 仍只是编辑值，不代表对硬件当前焦点的测量。CRC/ACK 表示软件资料和 MMIO 写入一致，不代表已验证实际声场。

## 使用与升级

1. 更新配套 Zynq 应用及 ESP32 固件，PC 图像页使用本次更新后的后端。无需更换 bit/XSA；本次没有覆盖 BOOT.BIN。
2. 高级参数页或实时图像页选择 20～170 mm；选档不会发送命令。
3. 点击高级页“停扫并应用焦点”或实时图像页右上角下发图标，确认停机。
4. 程序排空上传、关闭高压并停扫，通过配置事务发送单条 `FOCUS SET`，核对 ACK、配置结束状态和焦点状态。
5. 成功后保持停止。按需手动开启扫描、高压或在实时图像页点击“开始接收”。不会自动恢复。

预设切换不再发送 2048 条写命令。状态显示切换阶段及 1/1 进度；总耗时仍包含停流、停扫和硬件排空，不能把 MMIO 写入耗时当成整个切换耗时。尚未上板测量具体切换秒数。

高级页“载入草稿”、JSON 导入及自定义延时表仍使用原始表编辑与分批 BRAM 写入；这条通用路径不会自动变成预设切换。自定义写入、配置中止或失败后，PS 预设焦点记录标为未知，不假装回滚已经写入的数据。

## 命令与版本匹配

```text
FOCUS STATUS
FOCUS SET <深度mm> <CRC32>
```

`FOCUS SET` 仅允许当前 Wi-Fi 配置事务所有者执行；深度必须为 20:10:170，CRC 必须与 DDR 中对应表的 CRC 一致。还需芯片就绪、扫描和高压关闭、DDR 排空证明有效。错误时 NACK，不回退到慢速发送。

只读回执：`@FOCUS v=1 ready=1 depth=80 crc=0x27cdd731 words=16384`。depth=0 表示没有可确认的预设记录。写入回执：`@ACK <事务编号> FOCUS_SET 80 0x27cdd731`。原 STATUS 不追加长字段，避免超过既有回执长度。

查询：

```powershell
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "FOCUS STATUS"
```

当前表库 80 mm 的显式事务示例（每个更新版本应以新清单 CRC 为准）：

```powershell
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "CFG BEGIN 100" -c "FOCUS SET 80 0x27cdd731" -c "CFG END" -c "STATUS" -c "FOCUS STATUS"
```

旧 PS/ESP32 不支持命令、表库未就绪、CRC 不匹配或 ACK 不匹配均明确失败；不重放未知结果，不自动启用高压或扫描。PC 后端仍校验本地 configs 是否被修改；修改后重启服务，并更新匹配的 PS 表库与 ESP32 焦点资料。

## 单独运行 MATLAB 更新表库

在 MATLAB 命令窗口执行：

```matlab
addpath('D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/scripts');
export_focus_bank;
```

默认只读调用 `D:/MyProjects/py_prj/ultrasound_dev_kit/tools/para_cal` 的计算、通道映射与 C 导出函数；不修改参考工程。Python 校验器使用 conda esp32c61，可通过导出函数第二个参数传入其他明确的 python.exe。

MATLAB 先输出到当前工程 build 下的唯一暂存目录。校验器核对每档 C/raw/当前 JSON 逐字一致、长度和重复表；失败不替换正式表库。安装错误会恢复已替换的文件。生成结果为两份 PS 源中的 `bram/focus_bank_data.c` 与 `focus_bank_manifest.json`，记录 CRC、SHA256 及参考脚本/通道映射来源。它不会自动覆盖 JSON；若算法改变导致不一致，应先明确更新并确认对应的 JSON，再重新导出。

日常 Vitis 编译只使用已生成 C 表，不运行 MATLAB、不依赖参考工程路径。之后正常编译 USB/SDIO 应用和 ESP32 固件；若只更新 C 表而 ESP32/PC 仍用旧目录，CRC 门控会拒绝不匹配的档位。

## 离线验证与板测边界

已实际运行 MATLAB 生成 16 档，结果与当前 JSON 全部逐字一致。主机测试覆盖表库 CRC/损坏、索引、完整写入顺序与末字、写入失败、自定义记录失效、事务所有权、未排空、芯片状态及短命令的 ACK/最终状态。

ELF 检查读取实际文件字节：16 张表均在只读应用 DDR 内，合计 1048576 字节，实际 CRC/SHA 与清单一致。应用未进入 USB 保留区或图像槽位。参考工程不变，未自动烧录、下载、使能高压、启动扫描或打包 BOOT.BIN。

板测另行确认后，按正常基线、16 档切换、重复切换、停流失败、错误 CRC 和更新不匹配场景验收。分别统计排空、配置和端到端耗时，并验证成像效果；离线通过不能替代这些结果。

## 本次构建产物与离线结果

| 配置 | 绝对路径 | SHA256 |
|---|---|---|
| build | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf` | `d73c938298e19bd96b73b6fcd34a116e59522d3de2d483ec6b80d4c0b2aaca78` |
| vitis_frame_store_usb | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\vitis_frame_store_usb\B_mode_focus_imaging.elf` | `d88b68efc3c3320779ede80f1da229b05861b87abba01b1ae0025e803f9ffef9` |
| vitis_frame_store_sdio | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\vitis_frame_store_sdio\B_mode_focus_imaging.elf` | `52f3fc50fead1035cb9cdc44bc3654389ad017a527b358870e5fd8f2539ac152` |
| build | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build\ultrasound_psram_udp.bin` | `815d3f5f4bce54c8cdf189a41862df3b7d06ad775e42b58e70c16af834bd905a` |
| build-throughput-fast-socket-exp-combined | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket-exp-combined\ultrasound_psram_udp.bin` | `18faf839ce95ed5add2ed67707aca4207f35bb0c656462832271a6e656b06752` |

Vitis IDE 默认目录及独立 USB/SDIO 构建均成功，应用分配区末端为 `0x002850D0`。16 张表均已通过 ELF 实际字节 CRC/SHA 与只读 DDR 地址检查。两份 ESP32 应用均为 1252976 B，应用分区剩余 283024 B。

完整主机回归：Python 回归共 274 项：273 项通过、1 项历史样本测试跳过；新增导出器专项 3 项另行通过。网页完整回归 83 项通过后，新增快速事务专项通过（总计 84 项）。焦点核心模块行覆盖率 100%、分支取向覆盖率 92.11%。PS 实际解析和 MMIO 桩覆盖事务归属、CRC拒绝、排空/状态门控、16384字顺序和末字、自定义失效；同时回归 USB/SDIO、CPU/温度、电源及传输功能。

测试脚本另修复一处既有检查顺序：UART 桥输出断言原来在本轮测试执行前检查上一套测试的输出，现放到对应测试执行后，断言保留。

这些均为离线结果，不代表实测焦点切换时间或实际成像质量。本轮没有下载、烧录、操作高压/扫描或覆盖 BOOT.BIN。
