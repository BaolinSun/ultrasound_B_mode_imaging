# 温度监测：实现与验收说明

2026-10-13。本次只修改 Zynq/ESP32 软件，不修改 RTL、bit、XSA、SDIO 或 TCP/UDP 图像协议。不增加反向 UART 控制、温度历史、NVS 存储或自动停机。

## 运行方式

两端更新软件后，打开现有 PSRAM 数据上传网页即可。在“连接与目标地址”和“设备与传输状态”之间新增“温度监测”卡片，显示 Zynq 芯片、电源和 AFE 区域温度。无需启动上传，无需配置 PC 目标，也无需打开 monitor。

1. ESP32 使用下面的性能配置固件（保持原 TCP 配置），先启动 ESP32。
2. 在 Vitis 2026.1 编译并下载当前应用，或使用已构建的 SDIO ELF。复位/运行 Zynq。
3. 打开设备局域网页面，例如 http://192.168.3.27/；旧网页缓存时强制刷新。
4. 应看到首组温度，此后约每 10 秒更新一次，页面仍每秒刷新传输状态。启动期间及其他既有阻塞操作允许短时调度延迟。

**本次没有烧录、没有 JTAG 下载、没有板测、没有重新生成 BOOT.BIN。build/output 中原有 BOOT.BIN 不包含本次温度功能。板测和重新打包须另外确认。**

查看 API 不需要专用接收工具：
```powershell
python -c "import json,urllib.request;print(json.dumps(json.load(urllib.request.urlopen('http://192.168.3.27/api/status'))['temperatures'],ensure_ascii=False,indent=2))"
```

如需日志，确认当前 COM 端口后，在 ESP-IDF 控制台执行 `idf.py -p COM3 monitor`，再启动 Zynq。温度行带有 `[ZYNQ] @TEMP1` 前缀。不开 monitor 也会解析并刷新网页。

## 源码与通路

- 规范源：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\src\ps\system_ctrl\temp.c/.h`、`temp_hw.c/.h`。
- Vitis 源：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\src\system_ctrl\temp.c/.h`、`temp_hw.c/.h`。
- 公用格式和换算：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\common\temperature_protocol.h`。
- ESP32：`esp32c61/main/temperature_cache.c/.h`；原 UART bridge 在 USB 日志输出之前将原始 RX 字节交给缓存解析器；HTTP `GET /api/status` 输出缓存，页面无需新接口。
- 用户对 Vitis main.c 的注释/启动横幅清理保留；没有用规范 main.c 覆盖 Vitis main.c。
- 修改前备份：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\workspace_backups\temperature_feature_before_20261013`。其他已有日志删除和测试报告不混入功能提交。

Zynq 服务入口：`temperature_service_init()` 首次采集，`temperature_service_poll()` 在前台按单调时钟调度；跳过错过的周期，不补发。主循环、USB 等待和 USB 分包发送循环均调用服务。SDIO 按原有窗口返回主循环，不改变 SDIO 协议。

`temp_read_checked(address, &millidegrees)` 失败返回非零且不覆盖输出值；兼容入口 `read_temp(address)` 失败返回 NAN，不伪造 0°C。不得从 ISR 调用这些接口；IIC0 由前台温度服务独占。

## 采集与保护

- PS XADC 按当前 BSP 基地址查找配置，使用有限等待的 PS FIFO/DRP 读取。采用内置安全监测序列，保留 CFR1 的告警控制位和现有阈值，不禁用过温保护，不写报警阈值寄存器。
- 换算：12 位 ADC 码 × 503.975 / 4096 − 273.15°C；读取 16 位寄存器的高 12 位。依据 [AMD UG480 温度传感器说明](https://www.amd.com/content/dam/xilinx/support/documents/user_guides/ug480_7Series_XADC.pdf) 和 [状态寄存器定义](https://docs.amd.com/r/en-US/ug480_7Series_XADC/Status-Registers)。页面两位小数是显示格式，不是传感器精度承诺。
- 电源 0x48、AFE 0x49：IIC0，寄存器指针 0，12 位有符号左对齐，0.0625°C/码。不额外配置传感器；负数正确符号扩展，转换为毫摄氏度时半毫度向远离零方向取整。
- 每路采集共用自己的 20 ms 截止时间，包含寄存器指针写入及接收，XADC 重初始化也在该次读取截止时间内。
- IIC 异常清除本次自有事务 FIFO/HOLD/状态并恢复时钟分频和中断掩码；开始前总线已经忙时不抢占别的事务，20 ms 后返回失败。只访问 IIC0，HV 的 IIC1 不变。不添加 GPIO 强制总线恢复。
- 三路失败相互独立；每次最坏读取等待约 60 ms，另有最长 20 ms UART 入队等待及前台软件开销。实际板级极限还需验证。
- UART0/MIO27 → ESP32 GPIO10，115200/8N1，只发送；20 ms 限时，失败不立即重发。上一次发送若留下半行，下个周期先补换行重新定界。

## UART 与 HTTP 格式

UART 一行：
```text
@TEMP1 <seq> <valid_mask> <zynq_mC> <power_mC> <afe_mC>*<crc32>\r\n
```

CRC32 IEEE 覆盖 * 前全部 ASCII 字节；CRC 8 位十六进制。有效位 0/1/2 对应 Zynq/电源/AFE，无效通道占位值不参与显示。采样序号从 0 开始。固定 96 字节解析缓冲；支持分段接收、CRLF/LF、普通日志混入；超长行丢到下一换行，错误格式/CRC 不更新缓存，重复序号不延长旧数据的有效期。

状态对象包含 `period_ms=10000`、`seq`、`age_ms` 以及 `zynq/power/afe` 三个通道，每个通道有 `celsius` 和 `status`：

- waiting：还未收到有效报告；温度为 null。
- ok：当前报告该通道读取成功，包括真实 0°C。
- read_error：当前报告该通道读取失败；温度为 null，其他有效通道继续显示。
- stale：距上次有效新报告超过 30 秒，或 UART 丢失/溢出；温度为 null。

Zynq `@UART_LOG` 启动标记将缓存改回 waiting；ESP32 重启也从 waiting 开始。UART 溢出和接收错误立即清除有效状态。坏 CRC 本身不刷新时间，旧数据最终过期。网页与 ESP32 断开时显示“网页连接已断开”，不保留貌似当前的温度。

## 温度历史折线图（2026-10-20）

ESP32 配置页的“温度监测”现提供三路历史折线：Zynq 为青色实线、电源为橙色虚线、AFE 为紫色点线，并继续显示最新温度、采样序号和接收年龄。仅改变网页显示，不修改温度采集、UART 协议、HTTP 状态接口、10 秒周期或现有状态轮询频率。

- 无外部绘图库或 CDN，直接使用浏览器 SVG；设备无需访问互联网。
- 深色绘图区、细折线和清晰坐标轴；首个有效点或断点后的孤立点显示为小圆点，连续采样连线后不再显示末端大圆点。
- 最多保留 180 组、最近 30 分钟内浏览器实际见到的采样。首个采样从左侧开始，新数据向右追加，右侧预留一个采样周期的空间；横轴先随记录时长展开（至少两分钟），满 30 分钟后滚动。纵轴根据有效温度自动缩放。
- 按采样序号去重，不把每次网页轮询当成新采样。支持真实 0°C、负温度及 uint32 序号正常回绕。
- 每路读取失败以空值断线，不补成 0°C；报告过期、网页断连或跳过采样时不跨缺口连线，历史仍可保留在窗口内。恢复后仅新采样继续记录。
- 状态报告未包含采样或发现序号回退重启时清空旧会话曲线；不将前后两次启动画成连续历史。UART 丢失引起无采样状态时也清空。
- 历史仅在当前页面内存中，不写 NVS、设备 PSRAM 或浏览器持久存储。刷新页面、关闭标签后丢失；浏览器后台漏掉的历史不会从设备补取。
- 横轴以浏览器本地 `HH:MM:SS` 显示，时间由左向右增加，跨午夜时正常回到 `00:00:00`。记录时刻基于浏览器当前时间减去状态报告中的接收年龄，是显示估算，不是 Zynq/PC 校时后的绝对采样时刻。

2026-10-20 显示样式调整：用户已自行更新 Zynq 应用并反馈周期修复正常。本次仅修改网页、离线测试及说明；硬件已断电，不连接设备、不烧录、不更新构建产物。下方固件哈希为此前构建的历史记录，不含本次样式调整；使用新样式需之后自行重新编译并更新 ESP32 固件。

本次离线网页测试共 25 项通过，含左至右追加、午夜时间刻度、30 分钟滚动及原有网页功能回归；这不等同于真实浏览器布局或实机显示验收。

更新方法：编译并烧录 ESP32 新固件后，浏览器用 Ctrl+F5 强制刷新。无需更换 Zynq ELF、bit 或 BOOT.BIN。此项修改不附带烧录授权，不自动操作板卡。

网页自检查：`node --test tests/test_web.mjs tests/test_bmode_web.mjs`，2026-10-20 共 22 项通过，含原有上传/配网/高压确认/图像页入口回归与新增曲线测试。真实浏览器布局及实机长时间曲线观察仍需更新固件后验收。

本次 ESP-IDF 6.1 默认/throughput-fast 两档均构建成功，应用分区均剩余约 38%；网页增加 4929 B，无新增设备端温度历史缓存或采样任务。默认固件位于 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build\ultrasound_psram_udp.bin`，SHA256 为 `BDEDDE32717C6591844A26671A225552DF6C3D56ED85B47AF492CC21E7B74EE9`；性能固件位于 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket\ultrasound_psram_udp.bin`，SHA256 为 `DC22FE8FB883E1023EFC18C0E45267D6C9FFE625D1C23BE4CCEB6D5322BB500D`。本次未烧录；下方原温度功能的旧构建哈希为历史记录，不是此曲线版。

## 软件验证结果

### 2026-10-20：曲线仅显示孤立圆点的采样周期修复

用户提供运行数分钟后仍只有三种颜色圆点的截图。只读观察实际设备 `/api/status`：开始观测时采样序号 38、接收年龄 15205 ms；约 24.82 秒后序号 39、接收年龄 108 ms，推算相邻报告间隔约 **39.92 秒**。现有前端对超过 20 秒的样本间隔断线、报告超过 30 秒标过期，因此每个新报告都是孤立点。这不是“再等第二个点”能解决的问题。

根因在 PS `system_ctrl/temp_hw.c` 的 `temp_hw_now_ms()`：BSP 将 COUNTS_PER_SECOND 展开为未加括号的 `CPU_FREQ/2`，原除法、取余表达式分别作用于展开后的局部项，产生非单调且平均约慢四倍的毫秒值。温度服务逻辑仍按该错误时间等待 10000 ms，实际接近 40 秒。过去主机测试仅用字面常量频率，未覆盖真实宏形态。

规范源与当前 Vitis 源均已改为先求完整频率 `const uint64_t frequency=(uint64_t)(COUNTS_PER_SECOND)`，再用该变量做商和余数换算。目标仍为实际每 10 秒一次，不改变传感器配置或协议，不放宽前端断点策略，也不补零或虚构线段。**已安装上一节折线固件的 ESP32 不需重刷；需要更新 Zynq ELF。**

新增测试覆盖 1/2/4/10/40 秒附近的计时与 uint32 毫秒回绕，并以实际 `CPU_FREQ/2` 宏形态重复执行 IIC/XADC/UART 有限等待测试，全部通过。网页使用 1.2 秒轮询仿真：正常 10 秒源在三分钟内生成 19 组数据和 18 段连续折线；40 秒源复现只出现孤立点。当前网页测试共 23 项通过，温度合约 6 项通过；Vitis USB/SDIO 两种宏及默认 workspace 构建通过。

最新 ELF：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf`，与 `build/output/B_mode_focus_imaging.elf` 同步，SHA256 为 `8780C3754873546E8EF9560F4935F7D2F7A163697353D14609E595DF48F85759`。本轮备份、测试、构建与只读现场记录在 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\temperature_period_fix_20261020_114335286`。

用户已授权停机后下载应用，但前置安全检查两次拒绝，**尚未执行 CPU halt/reset/dow**。用户手动安全停止后，三次只读快照均为 run=spi=0、高压控制 bit8=0、active=issuing=0、FIFO empty=1，最新帧号 23243 不再变化；但 busy_sync=1、FLAGS=5 残留，errors=1。该 PL 处理尾部忙状态与温度周期问题分开记录；未绕过安全保护，未自行重新加载 bit。修复后真实 10 秒周期及实机曲线仍待具备安全加载条件后验收。BOOT.BIN、bit、ESP32 固件均未改。

- 主机 C 自检查通过：正/负/0°C、范围边界、XADC 黄金换算、严格 CRC 和字段校验、分段、超长行、启动/溢出、过期、独立通道失败、周期回绕、不补发、重入拒绝、IIC 忙/NACK/仲裁丢失/收发超时及恢复、XADC FIFO 超时/恢复、告警位保持、UART 限时/半行恢复。
- 原 UART bridge 的实际接收任务测试覆盖 monitor 未连接仍更新缓存、初始化失败、RX-only 引脚和无反向发送。
- 108 项 Python 回归、9 项实际网页脚本测试，以及 9 项 Vitis 源码清理/生成器检查通过；原 raw UDP、帧 UDP/TCP、SDIO、配置及日志桥回归通过。回归日志内故障注入产生的 “Soak FAILED” 是预期测试案例，以最终 unittest OK 和进程退出码为准，不是板测失败。
- 温度服务/硬件适配/缓存代码行覆盖率均 100%；分支 taken-at-least-once 分别 95.00% / 96.25% / 95.83%。不是整工程覆盖率。共享协议头也执行了畸形输入测试；不宣称全工程 FSM/CDC/48 小时验证完成。
- Vitis 2026.1 当前 SDIO 应用和隔离 USB 构建均通过；存在既有未使用参数/函数、符号比较、USB fall-through 等警告，本次没有将这些既有警告标记为已修复。
- ESP-IDF 6.1 esp32c61 默认/throughput-fast 两配置构建通过，应用分区均约剩余 39%。

软件测试命令（ESP32 目录）：
```powershell
.\tests\run_tests.ps1 -Python 'C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe'
.\build.ps1 -Action build
.\build.ps1 -Profile throughput -Action build
```

构建内存：ESP32 默认 DIRAM 118223 B，性能 DIRAM 130185 B；两者 .bss 25688 B、.data 16187 B。新增缓存静态符号合计 131 B（未计对齐），不新建任务或整帧缓冲。HTTP JSON 和调用栈仍有暂态开销；这些静态余量不是运行时堆低水位。Zynq SDIO text/data/bss=167355/140604/948848 B，USB=162523/140604/160432 B。

## 构建产物绝对路径与 SHA256

SDIO Zynq ELF（当前 Vitis 应用，保持默认 SDIO 宏）：
```text
D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf
84BD1879CA6EA7D3D230A2DFDF75BE6B0513AC2630462B40D4BFB6C073D121E0
```

USB 验证 ELF（隔离构建，不改变当前宏）：
```text
D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\tcp_pipeline_usb\B_mode_focus_imaging.elf
0078409B7560DF19EA76FABEE2D217A844E44479F702A633918EEE9E92755DB6
```

ESP32 默认配置：
```text
D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build\ultrasound_psram_udp.bin
39DDFCCBC7CCF37B37CAE3F5E991EE9927F1420E6C3A482CA8BAE8E70A532F2F
```

ESP32 性能配置（建议沿用该配置）：
```text
D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket\ultrasound_psram_udp.bin
8C8BB96EE47B5274670EBFBBE1EB423EC34E5EBE23A91E7B54B07E8B4CAE0C59
```

构建与测试原始记录在 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\reports\temperature_*.log`，这些是忽略的本机记录，不纳入功能提交。

## 待板测验收

确认设备/COM 端口及 Zynq JTAG 可用并另行授权后：

1. 不启动任何数据上传，观察三个温度每约 10 秒更新，序号递增；关闭 monitor 再观察至少 40 秒。
2. 在断电条件下断开一个探头，重新上电；该通道 read_error，其他通道更新，传输不中断。避免带电拔插。
3. 连续 SDIO→TCP 上传与原 raw UDP 自测分别检查 CRC/完整帧及有效吞吐，与同条件旧固件比较；记录内部堆低水位。
4. 暂停 Zynq 超过 30 秒，页面显示 stale；恢复报告后回到 ok/read_error。两端分别复位，应先清空旧值，再恢复。
5. USB 模式验证等待主机数据时温度继续更新；原 USB 参数和数据功能需实机回归。SDIO/USB 选择仍由 C 宏决定。
6. 长时间上传观察温度、错误和堆余量；短测不代替 48 小时稳定性测试。无 RTL 改动，本次不重新运行综合、时序或 CDC，不宣称解决已有板级/CDC 风险。
