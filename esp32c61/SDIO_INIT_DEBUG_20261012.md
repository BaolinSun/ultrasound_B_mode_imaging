# SDIO 灰色按钮与 CMD0 停滞：修复及板测记录

后续已实施 UDP/SDIO 性能优化；最新固件、BOOT 输入哈希和对比板测见 [性能优化记录](UDP_PERFORMANCE_20261012.md)。以下保留初始化修复阶段的历史结果。

日期：2026-10-12。修复前 Git 工作区干净，基线为 `0c0f8b4`。用户本次确认 COM3 后，已更新 ESP32 固件，并通过当前 Vitis/JTAG 下载 Zynq 应用，测试范围仅为固定 DDR 测试数据，没有启动成像或高压。

## 结论和根因

灰色按钮不是网页失灵。修复前 `/api/status` 中 Wi-Fi、目标地址、ESP32 SDIO 驱动均正常，两个槽位空闲，但 `host_sdio=false`。这表示 Zynq 尚未完成 SDIO 初始化/协议握手，不代表一定编译成了 USB。

实际定位并修复四个问题：

1. **SD1 控制器状态使能被复位清零。** SDK `CfgInitialize` 原先设置了轮询事件，但随后 `XSdPs_ResetConfig()` 再次清掉状态使能。板上 `E0101034` 为 `00000000`，命令完成事件无法被软件观察。复位后补调 `XSdPs_ConfigInterrupt()` 并恢复 DMA 模式；状态使能现在是 `33FF06FF`，信号中断保持禁用，仍使用轮询。
2. **DMA 准备路径误发 CMD16。** SDK `XSdPs_SetupTransfer()` 会通过 `SetBlkSize` 发 SD 存储卡的 CMD16，ESP SDIO-only 从机不支持。握手修复后的第一轮真实单帧因此失败。现在只设置主机块长寄存器并建立 DMA 描述符；从机块长由现有 CMD52/FBR 配置，不再发 CMD16。
3. **SDK 计时宏缺少括号。** 本地 `COUNTS_PER_SECOND` 展开为 `XPAR_CPU_CORE_CLOCK_FREQ_HZ/2`。直接参与除法/取余会改变运算顺序，可能产生错误超时。所有相关转换均修正，并用商/余数计算微秒，避免长期运行乘法溢出。
4. **打印格式错误。** 本地 Vitis 2026.1 `xil_printf` 在启用 64 位打印时，`%l` 会按 64 位读取；应用却传入 32 位 `unsigned long`。用户日志中超长地址、长度、CRC 是参数错位显示，不是 DDR 长度真的有数千万亿。SDIO 日志改用 `%u/%x`，并直接编译实际 SDK formatter 做回归。

另外，枚举前增加 CCCR I/O reset（CMD52 FN0 地址 0x06、bit3），再发 CMD0；不再伪造 RCA。命令等待改为严格完成/错误检测及有限超时，不把控制器空闲当作成功，不调用 SDK 的长时间等待助手。I/O reset 本身在未选中/复位期间可能超时或 CRC 异常，后续 CMD5/枚举成功才算恢复；不能把其他命令错误一概视为正常。

网页保留安全启动门槛，新增具体的不可启动原因；没有强制启用按钮，没有改变 SDIO/UDP 帧协议或添加重传。

## 修复后的板上日志

```text
@SDIO DDR_TEST addr=00151940 bytes=787456 crc32=3AA5AEBF compiled=SDIO
poll events enabled norm=0x06FF err=0x33FF
CMD52 I/O reset before enumeration
CMD0 complete; CMD5 OCR probe
CMD5 ready; CMD3 assign RCA
CMD7 selected; enable function1
@SDIO ONLINE SD1 4bit; waiting webpage start
sdio_bridge: SDIO frame=1 bytes=787456 crc=3aa5aebf elapsed_ms=290
@SDIO FRAME id=1 bytes=787456 elapsed_ms=292 (ESP receive, not PC ACK)
```

链路为 SD1（`0xE0101000`）四线、25 MHz；未操作 SD0。串口桥保持单向 RX，没有向 Zynq 发送控制命令。测试时 USB 控制台未读取期间出现过日志丢失提示，这是日志通路的有限等待行为，不是 DDR/UDP 校验失败。

## 实际板测结果

### 单帧：通过

- PC 收到 787456 字节、547 个数据报。
- CRC32 `3AA5AEBF`，固定测试模式逐字节校验通过，无丢帧/非法数据报。
- ESP 接收 SDIO 帧耗时 290 ms，Zynq 报告 292 ms。
- PC 首包到完整帧为 0.3306 s，折算约 19.06 Mbit/s。该时间不是全链路端到端计时。
- 日志：`build/reports/sdio-board-single-v2.jsonl`。

单帧日志 summary 的 33.864 s 包含接收程序启动后等待人工触发网页的时间，不能当作这帧的发送耗时。

### 连续：20 帧完整校验通过，2 帧不完整被丢弃

先启动接收程序，再用网页等效接口启动连续模式（`frames=0`）。PC 得到 20 帧完整 CRC/测试模式校验通过，2 帧部分数据被丢弃；无冲突包、非法数据报或一致重复包。

- 完整有效字节 15749120，接收到的载荷字节 17302432，数据报 12019。
- 接收窗口 11.513 s，有效帧率约 1.737 fps，有效载荷吞吐约 10.94 Mbit/s。
- 其中 frame 11 收到 537/547 包，等待超时丢弃；frame 28 收到 542/547 包，接收程序停止时丢弃。
- 观察到的不完整帧占比 2/(20+2)=9.09%；**不是总链路丢帧率**，零包到达的帧不在该统计分母中。
- 日志：`build/reports/sdio-board-continuous20.jsonl`。

板上本轮计数增量：SDIO 接收 23 帧、UDP 本地完整发送 22 帧、SDIO CRC 错误 0、错误 1、丢弃 2。停止连续测试时正在发送的一帧被取消，最终发送结果为 `cancelled`；现有统计将非成功完成（包括取消）计入错误。不能把这个停止取消当作一次 SDIO CRC 错误，也不能宣称所有错误计数均为零。

测试结束只读核对：`ready=true`、`host_sdio=true`、`running=false`、`raw_busy=false`、`free_slots=2`、`busy=false`，Wi-Fi/上传服务就绪。网页启动门槛已全部满足；需要刷新网页以加载新提示。

保留两份失败阶段证据，勿与最终通过结果混淆：

- `sdio-board-single.jsonl`：握手已恢复，但 CMD16 路径未修复时的单帧失败。
- `sdio-board-20frames.jsonl`：测试启动请求错误地传了 `frames=20`，API 拒绝，仅支持单帧 1/连续 0，未启动数据传输；不是链路丢包结论。

本次只是单帧与短时连续板测，不是 48 小时稳定性验证，也未完成真实采集、USB 数据板测、断网及两端所有复位组合验证。

## 软件与构建验证

- C 自检查通过：严格命令完成/状态清除/错误/忙碌/超时/冻结计时、宏展开转换、真实 SDK 打印；原始 UDP、带头 UDP、SDIO 窗口/缓冲所有权/取消、UART 单向桥等回归保持通过。
- 新命令引擎覆盖率：行 100%、分支 taken 95%；并非整个工程覆盖率。
- 76 项 Python、7 项网页测试通过。测试日志中的负例失败提示来自预期失败用例，不代表测试套件失败。
- ESP-IDF 6.1 默认/throughput 两种配置构建成功；实际 COM3 烧录的是 throughput 配置，NVS 配网/目标保留。
- 当前 Vitis 2026.1 工作区 USB/SDIO 两种宏模式均构建成功。最终恢复默认 SDIO；没有换临时工作区。USB 验证 ELF 不包含 test_frame，最终 SDIO ELF 包含 test_frame，地址范围 `0x151940..0x211d40`，64 字节对齐。
- 最终 SDIO ELF：text 175931 B、data 141004 B、bss 953040 B。USB 验证 ELF：text 172155 B、data 140996 B、bss 164624 B。现有旧编译警告没有全部清零。
- 源副本、XSA SD0/SD1/SPI1/UART0/UART1、BSP stdout/stdin、DDR 测试区、BOOT 顺序和 bit 哈希检查通过。
- 本次未修改 RTL、bit、XSA 或 FSBL，因此没有重新实施硬件；原时序/CDC 风险不因软件板测通过而视为解决。

详细构建/串口日志位于 `build/reports/sdio-init-debug-*.log`、`build/reports/sdio-frame-board-uart-v2.log` 及 `esp32c61/tests/output/sdio-init-debug-*.log`。它们是本地生成的诊断文件。

## 用户运行

Vitis 继续打开 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace`，对 `B_mode_focus_imaging` 执行 Build 并使用原 Download/Run 配置。当前应用源码已同步，默认宏仍为 SDIO。

在 ESP32 工程目录先启动：

```powershell
python .\pc_tools\sdio_udp_receiver.py --source-ip 192.168.3.27 --frames 1 --test-pattern --output-dir captures
```

再打开 `http://192.168.3.27/`，按 Ctrl+F5 刷新，确认“SDIO 已握手 / 可启动”，点击“SDIO 单帧”。不能用无包头的 `raw_udp_receiver.py` 接收该 SDIO 帧。

连续测试：

```powershell
python .\pc_tools\sdio_udp_receiver.py --source-ip 192.168.3.27 --frames 20 --seconds 30 --test-pattern --log sdio_20frames.jsonl
```

再点击“SDIO 连续测试”；接收工具退出后点击“停止全部传输”。PC 的 `--frames 20` 是接收完成数量，与设备 API 的 `frames=0/1` 不同。

## 最终交付文件和 SHA256

BOOT.BIN 已重新生成。打包顺序及输入绝对路径如下；本次板上实测是 JTAG 应用下载，没有将 BOOT.BIN 写入启动卡。

1. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\zynq_fsbl.elf`
   - `A71159C76FA6ACBBF5EBC9EDB4D58AABE21B3F4A1A81E67C98C69D4AED0C74C4`
2. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.bit`
   - `29B2A863B48FD3C284AA89CD69B90D42372C7AD9E0A85CE74274FADAE5C3E939`
3. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging.elf`
   - `7C62528373B811F74D5949B210FFDFA4F7CBF1EB09B5036007E74FA72E2FFE54`
   - 与当前 Vitis 应用 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf` 完全一致。

产物：

- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\BOOT.BIN`
  - `E22B370ECE3ED0A97E0B928283888BA107D299940C228B00116D1E0F57BB70A7`
- 实际 COM3 烧录：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput\ultrasound_psram_udp.bin`
  - `8ED9E2077FDFC79A04BCF80BF15AF4861F10E01B2A7E4FE015AEF993A202BB5B`
- 默认配置构建（未烧录）：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build\ultrasound_psram_udp.bin`
  - `C765C86C2CCD02AA1A3ABA063F8430C17AA72E22BE1D93F9351E05DD6BB5EC03`
- USB 编译验证（未下载）：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging_USB_verify.elf`。

历史 UART 修复文档中的旧哈希属于当时记录；SDIO 修复后的当前产物以本文件及 `build/output/boot_inputs.sha256` 为准。
