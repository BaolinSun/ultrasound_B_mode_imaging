# SDIO 直接读取成像 DDR（第一阶段，仅 PS 修改）

默认仍为 `DATA_OUTPUT_MODE=DATA_OUTPUT_SDIO`。发送源已从软件测试数组切换为 PL 成像窗口：

- 起始地址：`DDR_RING_BASE_ADDR = 0x20000000`。
- 每次发送：`BF_FRAME_BYTES = BR_FRAME_BYTES = 787456` 字节。
- 读取地址范围：`0x20000000` 至 `0x200C03FF`，包含末字节。
- 整个既有 DDR 环形窗口为 1024000 字节；本次只读取窗口起点的上述固定长度，不重组环形跨界帧。
- 不初始化、不清零、不填充该区域，不再分配 PS `test_frame[]`。
- 每次提交 SDIO BEGIN 元数据之前失效该区域的 CPU 缓存并重新计算 CRC32，然后按原有 512 字节块/窗口发送。
- SDIO、TCP/UDP 协议和 ESP32 固件不变。USB 编译模式及原有 START/STOP 控制不变。

## 明确暂不解决的问题

当前没有“完整帧已写完、缓冲区不可覆盖”握手。本次不是一致性快照：

1. CRC 计算或 SDIO DMA 期间，PL 写入可能改变数据，ESP32 因 CRC 错误丢弃该次传输。
2. 即使 CRC 通过，固定 DDR 片段也不保证从逻辑线 0 开始，不保证属于同一次完整扫描；可能混合多个采集时刻。
3. 未进行实际成像时，可能读到旧数据或未定义内容；CRC 通过并不等于成像已启动或图像正确。
4. 保留原有 CRC 错误处理和会话恢复，不跳过校验、不补零冒充完整图像，也不自动使能高压或暂停/启动成像。

后续需要 PL 帧完成/缓冲区所有权机制，本阶段不修改 RTL、BD、XDC、bit 或 XSA。

## 操作

1. 使用修改后的 PS 应用 ELF（或重新打包的 BOOT.BIN）启动 Zynq。原有配套 bit 保持不变。
2. 按原有上位机配置流程配置并 START 成像；网页的 SDIO 按钮只请求传输，不负责启动成像或高压。
3. 在 ESP32 网页配置好接收 PC 的 IP 和端口。先运行 PC 接收工具，再点击网页 SDIO 单帧/连续按钮。

TCP 固件接收示例（在 `esp32c61` 目录执行）：

```powershell
python .\pc_tools\sdio_tcp_receiver.py --port 50001 --source-ip 192.168.3.27 --frames 1 --output-dir .\image_capture --log .\logs\image_one.jsonl
```

若 ESP32 编译选择 UDP，使用同样参数的 `sdio_udp_receiver.py`。不要添加 `--test-pattern`；真实成像内容不是固定测试码。旧版 `sdio_benchmark.py` 自动启用测试码校验，不适用于本次真实 DDR 接入；本次按“仅 PS 修改”范围不改上位机生产工具。

启动串口应显示 `@SDIO DDR_IMAGE addr=20000000 bytes=787456 compiled=SDIO live_ddr=1`。

上位机接收工具现在将传输内容保存为 `.txt`：每个 16 位小端字对应一行 4 位大写十六进制数，包含原有行头，不再生成 BIN。这不是已经保证行对齐、帧同步的完整 B-mode 图像。此阶段可验证读源和数据通路，但不能代替后续完整成像验收。

## 本次软件验证（2026-10-15）

- 已同步规范源和当前 Vitis 应用源；实际运行代码只修改两份 `sdio_bridge.c`，另同步相关测试和 PS 构建检查。
- SDIO 和 USB 模式均通过 Vitis 2026.1 ARM 工具链编译。Vitis IDE 工作区正在使用，未关闭 IDE；采用其原有 CMake 构建配置和同一 SDK 工具链完成 clean build。
- 主机自检查将内存映射到真实地址 `0x20000000`，验证初始化不改写 DDR、发送直接读取该地址、逐帧数据变化重新计算 CRC、末窗口、会话复位、故障、超时及人为覆盖后的 CRC 错误；USB 模式不访问该 DDR 源。
- 源码/编译模式检查 3 项、SDIO 初始化契约检查 4 项、源码清理检查 9 项、网页检查 9 项通过。
- 全套 Python 回归为 107/108 通过。唯一失败是原有 `test_source_and_bd_copies_match`：上次 Vivado 编译自动补充派生 BD 参数，导致两份 BD 文本不完全相同。本次按“仅修改 PS”范围不改 BD，不将整套回归宣称为全通过。
- ELF 中 `frame_data` 的实际值已反汇编核对为 `0x20000000`，不再包含 `test_frame`；SDIO 应用 BSS 从 948848 B 减至 161392 B，减少 787456 B。
- 真实 DDR 版应用 ELF 和 BOOT.BIN 已重新生成；bit/XSA/FSBL 哈希保持上一轮构建不变。未执行烧录、JTAG 下载或高压/成像板测。

交付文件位于 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output`：

| 文件 | SHA256 |
|---|---|
| B_mode_focus_imaging.elf | EDB0AF8A0577E32D39A89DCFCA888FC6A8AD0EACF66ED3125C613859A75EC855 |
| BOOT.BIN | 30BE9B06D8E058C6D26B249EA960DC460CB467EE3EF7D57B3812F2DDDB256360 |
| ultrasound_B_mode_imaging.bit（沿用） | 6D96934CF78A6FB0E9970ACF0DEE70A16239A1A50B7C937B65FEB8DCC64FF2EE |

Vitis 下载 ELF 位于 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf`，与交付 ELF 完全一致。

完整输入绝对路径、时间和哈希见 `build/output/live_ddr_ps_manifest.json`、`boot_inputs.sha256` 和 `SHA256SUMS.txt`；回归记录见 `build/reports/live_ddr_regression.log`。原 `REBUILD_507DAFFC_20261015.md` 是此前固定测试数据版本的历史构建记录，不代表本次真实 DDR 版。
