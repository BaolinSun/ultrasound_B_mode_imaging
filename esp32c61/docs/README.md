# ESP32-C61 PSRAM UDP 自测与 SDIO TCP/UDP 帧上传

## 文档位置与索引

工程说明统一放在 `esp32c61/docs/`。除非另有说明，本文及同目录文档中的 PC 接收、ESP32 构建命令仍在 `esp32c61` 目录执行，不是在 `docs` 目录执行；明确要求在成像工程根目录执行的命令保持原要求。代码、脚本、固件和图像文件均未移动。

- [DDR 完整帧三缓冲与动态长度](DDR_FRAME_STORE.md)
- [DDR 初始化诊断与停机对照结果](DDR_INIT_DIAGNOSIS.md)
- [扫描后下采样配置 PAUSE 排空修复](DDR_PAUSE_FIX.md)
- [build.ps1 命令使用说明](BUILD_COMMANDS.md)
- [Wi-Fi 超声图像实时网页显示](WIFI_IMAGE_VIEWER.md)
- [Wi-Fi 下发全部 Zynq 命令](WIFI_COMMANDS.md)
- [扫描 run 状态读回修复与板测](SCAN_RUN_STATUS_FIX.md)
- [从 release/ubmi-pcb-v2 到当前版本的 PL/PS 修改清单](PL_PS_CHANGELOG_FROM_RELEASE_V2.md)
- [原始 ila_bf 探针与波束合成调试](ILA_BF_DEBUG.md)
- [Vivado 启动写入 C 报错修复](VIVADO_STARTUP_FIX.md)
- [运行日志与报告目录](LOGS.md)
- [SDIO 链路说明](SDIO_BRIDGE.md)
- [TCP/UDP 宏选择与协议](TCP_TRANSPORT.md)
- [Zynq 串口日志与 monitor](ZYNQ_UART_MONITOR.md)
- [温度采集与显示](TEMPERATURE_MONITOR.md)
- [USB/SDIO 输出模式验证](OUTPUT_MODE_VERIFICATION.md)
- [SDIO 验证记录](SDIO_VERIFICATION.md)
- [SDIO 初始化调试记录](SDIO_INIT_DEBUG_20261012.md)
- [TCP socket 调优历史](TCP_SOCKET_TUNING.md)
- [UDP 性能历史](UDP_PERFORMANCE_20261012.md)
- [UART 日志桥验证历史](UART_BRIDGE_VERIFICATION.md)
- [Vitis IDE UART 修复记录](VITIS_IDE_UART_FIX.md)
- [软件验证历史](VERIFICATION.md)

Wi-Fi 命令控制现已移除 USB 控制口令，电池供电时可直接通过网页或 Python 操作。接口不鉴权，仅限可信局域网；高压开启确认及配置安全约束仍保留，详见 [控制说明](WIFI_COMMANDS.md)。

历史文档中的测试结果不代表当前版本验收结果。此前已删除的 `TCP_OPTIMIZATION.md`、`TCP_PIPELINE_OPTIMIZATION.md` 和 `TCP_LARGE_BUFFER_TEST.md` 未恢复，历史引用仍保留。

## 当前 SDIO 接收保存格式

`sdio_tcp_receiver.py` 和 `sdio_udp_receiver.py` 的 `--output-dir` 现在保存 `frame_<会话>_<帧号>.txt`，不再生成 BIN。默认按 16 位小端数据解码，每行一个 4 位大写十六进制数（无 `0x`、无文件头）；原有成像行头也原样保留。一份 787456 字节数据对应 393728 行。需要其他位宽时，可使用 `--word-bytes 1` 或 `--word-bytes 4`；不补零、不截断末尾字节，不覆盖同名文件。TCP 的回执仍表示内存数据校验通过，不代表 TXT 已落盘。

```powershell
python .\pc_tools\sdio_tcp_receiver.py --port 50001 --source-ip 192.168.3.27 --frames 1 --idle-timeout 120 --output-dir .\image_capture --log .\logs\image_txt.jsonl
```

看到 Listening 后点击网页“SDIO 单帧”。真实 DDR 图像不要使用 `--test-pattern`。原始 PSRAM UDP 接收工具及板上固件不受此 TXT 保存格式变化影响。

运行日志及 JSON/JSONL 报告集中放在 [logs](../logs/)；只写 `--log image_txt.jsonl` 也会自动保存到本工程的 `logs`。压测附带的设备状态 JSON、标准输出和错误日志跟随主日志保存。显式指定其他目录或绝对路径时保留该位置，构建配置 JSON 不移动。

本目录是独立 ESP-IDF 6.1 工程，读取 ESP32-C61 外部 PSRAM 缓冲区，经 Wi-Fi STA 向上位机发送原始 UDP 数据报。网页完成首次配网、目标 IP/端口在线配置、单次自测上传及停止操作。

保留原始 PSRAM UDP 自测，Zynq DDR → SDIO → PSRAM → Wi-Fi 帧链路支持 C 宏选择 TCP/带头 UDP，默认 TCP。当前 PS 读取真实成像 DDR 固定片段，尚无帧完成/防覆盖保证；未修改成像 RTL。TCP 完成等待 PC 收齐并校验 CRC 后的回执，不证明采集帧一致性，也不保证 TCP 更快。接线见 [SDIO 链路说明](SDIO_BRIDGE.md)，宏与协议见 [TCP/UDP 模式说明](TCP_TRANSPORT.md)，控制成像及配置见 [Wi-Fi 下发命令](WIFI_COMMANDS.md)。三种接收工具不能混用。

当前 23040 B TCP 性能档加入 CRC 切片、帧头/首段合并、发送内存背压及两端停帧诊断；120/180 秒板测有效吞吐约 11.1..11.4 Mbit/s，未证明显著提速或停帧已根治。benchmark 默认在 armed 且两槽位空闲/发送空闲持续 5 秒时判失败，不再把该停帧当正常完成。最新测试、固件和 BOOT 输入见 [TCP 数据路径优化](TCP_PIPELINE_OPTIMIZATION.md)。

## 构建与烧录

环境激活、日常性能配置编译/烧录/monitor、完整参数及构建目录选择见 [build.ps1 命令使用说明](BUILD_COMMANDS.md)。脚本扩展名为 `.ps1`，不是 `.ps`。

本机 IDF 源码：`D:\Program Files\esp\v6.1\esp-idf`。安装器实际工具环境：`C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1`。直接运行 IDF 的 `export.ps1` 可能错误地寻找用户目录下的 Python 环境，`build.ps1` 已使用安装器配置。

```powershell
Set-Location 'D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61'
.\build.ps1
.\build.ps1 -Action size
.\build.ps1 -Action flash -Port COM7
.\build.ps1 -Action monitor -Port COM7
```

`COM7` 是示例，替换为实际 USB Serial/JTAG 端口。脚本默认仅构建，不自动烧录或擦除 NVS。其他电脑可传入 `-ActivationScript`、`-IdfPath`，或在已激活的 IDF 6.1 shell 中运行。

只激活当前 PowerShell 环境，不编译、不烧录：

```powershell
.\build.ps1 -Action activate
idf.py --version
```

直接在当前 PowerShell 中执行脚本即可，不需要点源；环境和 `idf.py` 命令在脚本返回后仍有效。不要通过另启的 `powershell -File`/`pwsh -File` 子进程激活来期望改变父终端。激活不切换目录，运行工程相关 IDF 命令时仍应位于 `esp32c61`。

打开当前性能固件对应的 monitor（`COM3` 替换为实际端口）：

```powershell
.\build.ps1 -Profile throughput -Action monitor -Port COM3
```

这会激活环境并调用对应构建目录的 `idf.py monitor`，不会执行 flash；按 `Ctrl+]` 退出。若烧录的是默认配置，改用 `.\build.ps1 -Action monitor -Port COM3`。monitor 可能按 IDF 默认行为复位设备，不应在成像或重要传输进行中随意打开。后续直接运行 `idf.py` 不会自动继承脚本的 `-Profile throughput` 选择；例如性能档 monitor 对应 `idf.py -B .\build-throughput-fast-socket -p COM3 monitor`，裸 `idf.py build` 仍使用默认 `build` 目录。

可选 Wi-Fi 发送性能配置（需要烧录后才影响板上速率）：

```powershell
.\build.ps1 -Profile throughput
.\build.ps1 -Profile throughput -Action size
.\build.ps1 -Profile throughput -Action flash -Port COM7
```

`throughput` 按用户速度优先选择，默认使用 TCP 高档：独立 `build-throughput-fast-socket/` 目录及 sdkconfig，仅上传 socket 发送缓冲 23040 B，其他 TCP/网页默认 5760 B，staging 5760 B，保留原 RX 窗口。合并基础、Wi-Fi 性能和 TCP 分档 defaults，不覆盖原有 `sdkconfig`、`build/` 或旧性能构建目录。TX BA 窗口仍为 32，保留 lwIP IRAM 优化、看门狗和 PHY 设置。配置生成后保留，可用相同参数的 `-Action menuconfig` 调整。

指定 `-TcpTuning baseline/mid/fast/faster/large/xlarge` 选择 5760/11520/23040/28800/46080/57600 B 上传专用 TCP 发送缓冲，各自独立 `*-socket` 目录。默认仍为 fast（23040 B）；large/xlarge 为显式实验档，保持 16384 B 内部堆发送背压、5760 B staging 和普通 TCP 默认容量。大缓冲对比见 [大容量 TCP 实验](TCP_LARGE_BUFFER_TEST.md)。28800 B 的历史板测发生既有上游停帧，不应作为持续提速证据。短测不代表长期稳定。原始 UDP 与 SDIO TCP/UDP 宏选择保持不变。历史实现及结果见 [上传连接专用缓冲](TCP_SOCKET_TUNING.md)和[历史 TCP 优化板测](TCP_OPTIMIZATION.md)。

```powershell
# 如需更大内部 heap 余量，显式回退中档
.\build.ps1 -Profile throughput -TcpTuning mid -Action flash -Port COM7
# 实验性更大上传容量，不保证更快，需板测内存及持续吞吐
.\build.ps1 -Profile throughput -TcpTuning faster -Action flash -Port COM7
```

默认：Flash 4 MiB，IDF 内置 1500 KiB 单应用分区，Quad PSRAM 40 MHz，USB Serial/JTAG 控制台，FreeRTOS 1000 Hz。PSRAM 容量由启动检测确定。PSRAM 缺失或自测分配失败时仍可访问配置页，但禁止自测上传，不使用 SRAM 替代测试数据。

```powershell
.\build.ps1 -Action menuconfig
```

`Ultrasound PSRAM raw UDP` 菜单支持最大提交长度（默认 1048576）、自测长度（默认 787456）、总超时（默认 30000 ms）。接入 SDIO 后自测借用固定帧池，自测长度不得超过 787456，最大提交长度不得小于 787456；否则帧池初始化明确报错。IDF 6.x 的 JSON 已迁移到 `espressif/cjson`，本工程锁定 `1.7.19~2`，本机安装器缓存可用于构建。

## 第一次使用

1. 烧录并打开 USB 控制台，读取 `Ultrasound-C61-xxxxxx` 配置热点及随机密码。密码保存到 NVS，重启保持。
2. 手机或电脑连接该热点，访问 `http://192.168.4.1/`。
3. 填写路由器 SSID/密码、上位机 IPv4 和 TCP/UDP 共用端口。端口默认 50001，目标 IP 默认未配置。上位机填写运行接收程序的电脑局域网地址，不能填写 ESP32 自己的地址。
4. 保存后等待 STA 获得 IP，网页和控制台显示 `http://设备局域网IP/`。连接成功 15 秒后关闭配置热点，电脑连接同一路由器后使用该地址访问配置页。路由器须允许客户端互访。
5. 先运行接收程序，再在网页点击“上传一次 PSRAM 自测数据”。每次接收使用一个新输出文件，避免覆盖既有结果。

```powershell
python 'D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\pc_tools\raw_udp_receiver.py' --port 50001 --bytes 787456 --output capture_001.bin
```

工具默认等待首次数据 60 秒、数据中断超时 5 秒、单次接收总超时 30 秒。正常且内容校验通过退出 0；缺失、异常长度、顺序/内容异常或额外包退出 1，已有数据保存为 `.partial`。使用 `--source-ip 设备局域网IP` 可过滤其他发送者。

接收期间仅检查来源、数据报长度和超时并保留原始顺序；收包结束后再校验测试内容，避免 Python 逐字节校验拖慢收包。`passed: true` 仍要求长度、数据报检查和已启用的内容校验全部通过；部分接收的数据也会校验。没有收到数据时 `pattern_checked` 为 `false`。

期望自测帧在接收前生成并缓存，最多缓存一种长度（CLI 最大 16 MiB）；完整帧用原生字节比较快速校验，仅在不匹配或部分接收时按块统计错误数及首错位置。自动测试在开始循环前预热缓存，不在收包过程中生成期望字节。`validation_mode=cached_selftest` 标识该路径，一次性期望数据生成不计入 `validation_elapsed_s`；该优化不改变模式、顺序或错误判定。

测速字段：`elapsed_s` 为首个到最后一个接受的数据报在用户态 `recvfrom()` 返回后的时间差；`mbit_s` 按该时间计算有效负载速率。末尾额外包观察窗口（默认 0.25 秒）、内容校验和文件写入不计入其中。`validation_elapsed_s` 单独记录内容校验耗时；`timing_basis` 为 `first_to_last_datagram_dequeue`。只有一个包时没有有效时间跨度，`mbit_s` 为 `null`。这些时间戳不是网卡/内核到包时间，不能当作 PHY 速率或完整上传流程耗时，也不能直接等同于官方持续 iperf 测试。

业务数据可使用 `--pattern none` 仅按长度接收保存；此时通过只表示长度满足要求，不能证明业务内容正确。若修改固件自测长度，需要同步修改 `--bytes`。

## 数据格式与内存所有权

发送内容严格为 PSRAM 字节：每包最多 1440 字节，末包为剩余长度，无应用层包头、包序号、帧号或结束标记。默认 787456 字节产生 547 包：546 个 1440 字节包和 1 个 1216 字节尾包。自测第 `i` 个 32 位小端字为 `i XOR 0xA5C30000`；奇数字节长度也按该模式截断。

原始 UDP 不支持可靠帧重组、端到端确认或丢包重传；上位机必须预先知道本次长度，每次仅触发一个上传。自测可通过已知内容检测异常，但不能据此恢复丢失数据。一般业务流在丢失或乱序后无法保证对齐。`sent_local` 仅代表本地 `sendto()` 成功。

原始 UDP 数据路径为：登记的 PSRAM 缓冲区 → 内部 SRAM staging 的 1440 字节前缀 → UDP socket → 上位机。共用 staging 默认分配为 5760 字节，TCP 使用整块。发送任务优先级 5，UDP 每 32 包让出 CPU 至少 1 ms；`ENOMEM/ENOBUFS/EAGAIN/EWOULDBLOCK` 最多重试 100 次。socket 发送超时 100 ms，总期限从接受提交起计时。断网、IP 丢失、重新配网、停止或超时终止当前上传，不自动重放。

公共接口位于 `main/psram_udp.h`。为了检查完整缓冲区范围，提交仅接受已登记的 PSRAM 内存，可提交登记缓冲区的子范围；最多登记 4 个缓冲区。

```c
#include "psram_udp.h"

static void uploaded(raw_result_t result, size_t sent, void *context)
{
    // result/sent 供业务记录；回调在 UDP 任务中，必须短且不阻塞。
    (void)result;
    (void)sent;
    psram_udp_free(context);
}

// psram_udp_init() 已由 app_main 调用。
uint8_t *buffer = psram_udp_alloc(length);
if (buffer != NULL) {
    // 在此填充业务数据，上传期间不得修改。
    esp_err_t err = psram_udp_submit(buffer, length, uploaded, buffer);
    if (err != ESP_OK) psram_udp_free(buffer); // 拒绝提交不会触发回调
}
```

业务若自行用 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` 分配，需要先调用 `psram_udp_register_buffer(base, capacity)`。调用方保证登记的是仍有效的真实分配范围，完成后先 unregister 再释放。模块自行分配的缓冲区使用 `psram_udp_free()`。正在发送的范围不能释放或注销。

提交返回：`ESP_OK` 已接受；`ESP_ERR_INVALID_ARG` 越界/无效长度/未登记/空回调；`ESP_ERR_TIMEOUT` 已忙；`ESP_ERR_INVALID_STATE` 未连接或目标未配置。完成回调不可为空，只有成功接受的提交会产生一次完成回调，成功、失败、取消都覆盖。接受后直到回调前不得释放或修改内容。

FreeRTOS mutex 保护配置快照、登记范围、上传状态及任务间访问；任务通知唤醒单一发送任务。同次上传固定目标 IP/端口，在线修改只影响下一次提交。回调在锁外执行。此处是软件任务同步；本次没有新增 FPGA CDC crossing 或约束。

## 网页 API 与配置行为

| API | 行为 |
| --- | --- |
| `GET /` | 固件内嵌配置页面 |
| `GET /api/config` | SSID、是否有密码、目标 IPv4/端口，不返回密码 |
| `PUT /api/config` | JSON 配置校验后保存到 NVS，再更新当前快照 |
| `GET /api/status` | 网络、PSRAM/内部堆、上传字节、包数、重试、结果与 errno |
| `POST /api/upload` | 单次上传 PSRAM 自测缓冲区，202；忙时 409；不可用时 503 |
| `POST /api/stop` | 请求终止当前上传，202；本地已排队的数据报不能撤回 |

配置 JSON 必需 `ssid`、`target_ip`、`target_port`；`password` 可省略以保留原密码，显式空字符串表示开放 Wi-Fi。空 SSID 启用配网热点，空目标 IP 暂停后续上传。SSID 最多 32 UTF-8 字节；密码为空、8..63 字节或 64 个十六进制字符；目标需规范点分十进制单播 IPv4，不接受前导零、未指定、环回、组播地址；端口为 1..65535 整数。请求体最多 768 字节。

示例（HTTP 运行于 TCP，数据运行于 UDP）：

```json
{"ssid":"your-router","password":"your-password","target_ip":"192.168.1.100","target_port":50001}
```

配置仅在验证与 NVS 保存成功后更新。写入失败返回 500，当前运行配置保持原值；重启恢复整个已保存配置 blob。初始化 NVS 失败不会自动擦除其他数据，控制台会显示错误。

Wi-Fi 凭据变化会阻止新提交、结束当前上传，并在 HTTP 响应之后延迟约 500 ms 应用。断线自动重连（重试间隔逐步增加至 16 秒）；持续 30 秒无连接或 SSID 未配置时恢复热点。正常连接后网页始终可从 STA 局域网 IP 访问。

## 测试与板级验收

```powershell
Set-Location 'D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61'
.\tests\run_tests.ps1
```

本机主机 C 编译器来自 AMDDesignTools 的 MinGW，可用 `-Compiler` 指定其他 GCC，`-Python`、`-Node` 指定解释器。测试输出/覆盖率在 `tests/output/时间戳`，每次运行隔离。

软件测试包括：C 可移植发送/配置核心，Python 原始接收与真实回环 UDP/CLI 文件保存，48 小时测试工具的本地 HTTP+UDP 模拟，实际内嵌网页脚本的 DOM/API 模拟。覆盖率仅代表 `raw_transfer.c`、`config_model.c`，不代表 Wi-Fi 驱动、FreeRTOS、真实 flash 或完整固件覆盖。

板级步骤：

1. 核对模块确有 PSRAM、Flash 容量，启动日志没有 PSRAM 测试错误；核对默认自测长度与 B-mode 配置。
2. 完成首次配网，关闭/恢复路由器，验证 30 秒恢复热点、重连后 15 秒关闭热点。
3. 在两台 PC 运行接收程序，在线修改目标 IP/端口；正在传输的数据固定旧目标，下一次传输进入新目标。修改 Wi-Fi 时当前上传结束。
4. 验证重复点击仅接受一次、停止后可再次上传、断网产生失败回调、重启恢复配置。每次接收校验字节与文件长度一致。
5. 固件可通过 menuconfig 缩短总超时，在控制条件下验证 timeout 路径；PSRAM 不可用时上传按钮禁用，不从 SRAM 发送测试内容。
6. 做 48 小时重复自测：先配置目标为当前测试 PC 路由接口地址、确认设备 idle，关闭其他同端口接收程序。运行下列工具，每轮先监听再触发上传，逐字节校验、记录时间/吞吐/内部堆。默认允许 UDP 帧丢失：坏帧整帧丢弃，继续下一帧，不重传坏帧。

```powershell
python 'D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\pc_tools\soak_test.py' --device-ip 192.168.1.61 --hours 48 --interval 1 --log soak_48h.jsonl
```

`192.168.1.61` 替换为 ESP32 STA IP，日志文件不可已存在。工具不更改设备配置、不累计保存 binary 文件，每轮记录结果和内部堆低水位。新的验收策略是：允许丢帧，但有效帧必须长度和内容全部正确，不交付残缺/损坏帧，设备不能挂死；检查内部堆是否持续下降。丢帧率记录为测量结果，尚未指定其上限。实际无线性能、复位掉电/NVS 恢复、浏览器显示及 48 小时稳定性需要板上实测；本机模拟测试不能替代。

默认完成时输出 `Soak COMPLETED` 和有效/丢弃帧数、丢帧率，不再用“PASSED”表示零丢包。只要正常结束且至少收到一帧有效数据，退出 0；HTTP 触发/状态查询、设备发送错误或计数异常、帧间清理失败仍退出 1；全程零有效帧也退出 1。`--stop-on-frame-error` 可恢复首个坏帧即停的严格诊断模式；`--frames N` 可限制最多触发 N 帧，帧数或时长先到即结束。

当前设备的 3 分钟命令（日志不能已存在）：

```powershell
python .\pc_tools\soak_test.py --device-ip 192.168.3.27 --hours 0.05 --interval 0 --log soak_3min_optimized.jsonl
```

自动测试在 UDP 接收初始化完成后才释放独立 HTTP 触发线程；主线程持续收包，不等待 HTTP 响应。HTTP 请求结束后，最多等待 35 秒确认设备空闲，再观察/清理残留包直到连续 0.25 秒安静（清理阶段最多 2 秒），才能进入下一轮。自动模式将接收函数内的额外包等待与帧间清理合并成这一个窗口；如果窗口中出现当前帧同一来源的额外包，即使长度和内容已满足预期，也将该帧丢弃，不能只清理不报错。整个测试保留同一个监听 socket，并过滤最近 16 个已观察到的设备发送端口，降低迟到的旧帧数据污染新帧的风险。旧版先等待 HTTP 响应再读 UDP，可能把积压队列的快速出队误算成高吞吐，旧日志速率不宜直接比较。

`--frame-quiet` 可调整唯一的安静窗口（0.01..1 秒，默认 0.25）。默认只合并重复等待，没有缩短保留下来的窗口；先使用默认值建立基线，再对照较短窗口是否增加坏帧/跨帧污染，不能靠缩短窗口宣称链路可靠。单次接收 CLI 仍保留原来的 0.25 秒额外包观察。

每轮新增诊断字段：`receive_mode=concurrent_http_udp`、`http_trigger_elapsed_s`、`trigger_error`、`udp_rcvbuf_bytes`（系统实际 socket 接收缓冲大小），以及 `device_busy`、`device_sent_bytes`、`device_packets`、`device_retries`、`device_socket_errno`。状态查询异常另记 `status_error`，不覆盖先前的 UDP 错误。发送重试是本地发送接口重试，不是接收丢包统计；`device_socket_errno` 可能是已恢复的历史错误。

`passed` 仍表示本帧完整且自测内容正确；`frame_status` 为 `valid` 或 `discarded`，`discard_reason` 保留原因。`valid_frames`、`discarded_frames`、`frame_drop_rate` 是累计统计，后者为丢弃帧数/触发帧数，不是 UDP 包丢失率。`drained_datagrams` 表示轮间丢弃的残留包。坏帧的内存数据直接释放，不合并到下一帧，也不写出业务文件。

优化版额外记录 `post_receive_frame_datagrams`（合并窗口中属于当前帧的额外包）、`frame_quiet_s`、`cycle_elapsed_s`、`run_elapsed_s`、`valid_fps`、`valid_payload_mbit_s`。累计有效帧率/有效负载吞吐从预热完成、开始运行循环时计时，包含收包、校验、控制请求、观察窗口及先前轮间休息，不含当前轮结束后尚未发生的休息；一次性缓存准备不计入。`mbit_s` 仍仅表示单帧首末包出队跨度内的速率，不能用它代替全过程吞吐。

并行收包不保证消除真实 UDP 丢包；缺包、乱序、重复、内容异常仍使该帧无效，但默认继续测试。单次 `raw_udp_receiver.py` 仍保留严格退出码和 `.partial` 诊断文件；这些文件不是完整业务帧。`--interval` 只控制轮间休息，不能给单帧发送限速。

此策略仅适用于已知长度、一次 HTTP 触发对应一次独立 UDP socket 上传的自测帧。没有给固件加入帧头、帧号、包序号或重传。一般连续业务流仅按长度拼接不能可靠识别帧边界，也无法靠总长度发现等量丢包+重复；自测内容可检测顺序/字节异常，但不能证明来自哪一帧。轮间安静窗口和端口历史是隔离措施，不是可靠帧协议。后续接入连续真实图像流时，仍需明确可识别的帧边界及包归属方案。

构建与当前验证记录见 `VERIFICATION.md`。
