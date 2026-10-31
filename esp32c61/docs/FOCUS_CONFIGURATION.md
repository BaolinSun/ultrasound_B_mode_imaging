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
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "CFG BEGIN 100 FOCUS" -c "FOCUS SET 80 0x27cdd731" -c "CFG END" -c "STATUS" -c "FOCUS STATUS"
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
| build | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf` | `e994175c844316f59ef83bb921e8b27997b04b4af1d3b4d07943b40c621aeef6` |
| vitis_frame_store_usb | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\vitis_frame_store_usb\B_mode_focus_imaging.elf` | `f9c62e7fe62ef79813e953123cc6641965b593e5724ac8afcfdfd154db734c15` |
| vitis_frame_store_sdio | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\vitis_frame_store_sdio\B_mode_focus_imaging.elf` | `9d061c646282739e5695941759dab086280ff5b1ef4dfcfd282c5210773494d6` |
| build | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build\ultrasound_psram_udp.bin` | `5142ddbe9bfdda43a3ad46a80f2b8b7449cf57699193bb75ba40c97384ce6e47` |
| build-throughput-fast-socket-exp-combined | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket-exp-combined\ultrasound_psram_udp.bin` | `c91aa15dc36176850e85aa59cd1349e7878b80f08455860482a8de4e8a15d0af` |

Vitis IDE 默认目录及独立 USB/SDIO 构建均成功，应用分配区末端为 `0x002850D0`。16 张表均已通过 ELF 实际字节 CRC/SHA 与只读 DDR 地址检查。两份 ESP32 应用均为 1252976 B，应用分区剩余 283024 B。

完整主机回归：Python 回归共 274 项：273 项通过、1 项历史样本测试跳过；新增导出器专项 3 项另行通过。网页完整回归 83 项通过后，新增快速事务专项通过（总计 84 项）。焦点核心模块行覆盖率 100%、分支取向覆盖率 92.11%。PS 实际解析和 MMIO 桩覆盖事务归属、CRC拒绝、排空/状态门控、16384字顺序和末字、自定义失效；同时回归 USB/SDIO、CPU/温度、电源及传输功能。

测试脚本另修复一处既有检查顺序：UART 桥输出断言原来在本轮测试执行前检查上一套测试的输出，现放到对应测试执行后，断言保留。

这些均为离线结果，不代表实测焦点切换时间或实际成像质量。本轮没有下载、烧录、操作高压/扫描或覆盖 BOOT.BIN。

## 2026-10-31：前一版本实机配置与耗时验证（尚有固定 1 秒等待）

用户更新两端固件后，经网络测试；未刷写、复位或开启高压、扫描。开始时设备与本机显示服务均空闲，AFE/TX ready，PS 实时查询为 80 mm，抽取参数 down=3。

| 测试路径 | 次数 | 平均 | 最小 | 最大 |
|---|---:|---:|---:|---:|
| 直接短命令事务（含前置查询、CFG BEGIN、SET、END、最终查询） | 16（20～170 mm 各一次） | 2.342 s | 2.229 s | 2.489 s |
| 运行中的图像后端 `/api/viewer/focus`（20、170、80 mm） | 3 | 3.001 s | 2.954 s | 3.081 s |

每一档均检查 FOCUS_SET ACK 的事务号、深度和 CRC，以及最终 FOCUS STATUS、cfg=0/run=0/hv=0；全部通过。81 mm 非法档位返回 HTTP 400；错误 CRC 返回 FOCUS_CATALOG_MISMATCH，并确认事务清理和高压/扫描关闭，没有自动重放。

直接事务平均阶段耗时：前置查询 0.180 s，停扫/排空及 CFG BEGIN 1.237 s，FOCUS SET 0.559 s，CFG END+STATUS 0.185 s，最终焦点查询 0.182 s。FOCUS SET 耗时包含 CRC、MMIO 写入、UART/HTTP 及任务查询等待，不是单独的 BRAM 总线写入时间。当前测试开始时无活跃图像上传，不能用该耗时保证传输活跃或网络较差时也完全相同。

结束时恢复原 80 mm（CRC 27cdd731），保持 cfg=0/run=0/hv=0，down=3 不变，AFE/TX ready，SDIO 空闲槽位 2。SDIO session 与开始一致，reset_count 保持 2，CRC 错误/错误仍为 0，未增加链路复位。配置会清理旧图像帧，重新成像需用户手动开始。

结论：DDR 预存表选择、写入命令和回执核对在本轮实机测试中正常；实时图像页一次切换约 3 秒。没有进行高压发射/体模成像，因此本记录不等于实际声场或图像焦点效果验收，也不代替长时间稳定性测试。

详细原始记录：`esp32c61/logs/focus_verified_20261031-164927.jsonl`（Git 忽略的本地日志）。

## 焦点专用停扫优化：取消无条件 1 秒等待

两个预设焦点入口现使用 `CFG BEGIN <事务号> FOCUS`，ACK 标签仍为 CFG_BEGIN，后续 FOCUS SET/END/STATUS 不变。普通 CFG BEGIN、USB/UART1 配置、普通 STOP 与异常中止仍保留原行为；不修改通用 Beamform_Stop() 的 sleep(1)。

专用流程先完成 ESP32 上传排空和 PS USB 配置门控，然后关闭高压、仅清 RUN 请求，保留 SPI 配置控制。若本次 PS 启动中的有效 PAUSE 证明仍成立，则复用；否则执行新的 frame_store_pause()，以硬件输入/处理/AXI 及 FIFO 清理回执确认。失败拒绝配置，不强行放行。

随后复用现有 TX 硬件窗口发送一个新的 control=0 请求，不置 HOLD/STDBY、不复位、不改时钟。现有 PL 仅在 TX 请求、四路 SPI、读请求及触发流水级连续空闲八个 TX 周期后确认新请求。PS 必须收到匹配本次序号的 ACK，且 live IDLE、CLOCK_READY 正常、无 BUSY/ERROR，才能继续。旧 ACK 不放行；TX 确认最多等 1 秒，并有计数防护处理计时源故障。超时或 ABI 丢失进入安全故障，保持高压关闭；不在 TX 未确认时清 SPI。

确认完成后再调用 Beamform_Halt()。配置事务仅允许 FOCUS SET 和查询/结束/中止及安全关闭命令，不能借此向 AFE、TX 寄存器、增益或其他 BRAM 参数写入。写焦点前再次检查 TX 确认证明与 DDR 排空证明；任何证明被复位或新操作破坏均拒绝。写入 CRC 校验继续保留。

必须配套更新 Zynq 应用、ESP32 固件和 PC 后端，刷新网页；旧固件明确拒绝新语法，不静默回退。不需要更新 bit/XSA，不改变 AFE/TX 电源模式、CPU 策略或图像协议。本轮没有连接、下载、烧录或操作板卡，没有覆盖 BOOT.BIN。

离线验证：277 项 Python 回归中 276 项通过、1 项既有历史样本跳过；网页、原生 C、USB/SDIO 和电源控制回归通过。新增测试覆盖复用/失效 PAUSE、扫描中仅清RUN、TX旧ACK/错误序号/停钟/超时/计时源冻结/ABI异常、焦点事务写入限制及普通配置仍走旧停止路径。现有 TX 硬件、SPI和读回 CDC 自检查仿真通过，未修改 RTL。Vitis USB/SDIO 及 IDE 默认目录、ESP-IDF 默认/combined 构建成功，焦点表 ELF 校验通过。

上方约 3 秒的数据属于优化前版本；本版本尚未上板测速，不能直接视为已经达到 1～2 秒。板测需分别覆盖已停机连续切换，以及成像/上传活跃时的排空，检查耗时、错误CRC/超时拒绝、最终高压和扫描关闭、手动恢复后成像正常。
