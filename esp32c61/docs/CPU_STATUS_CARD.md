# Zynq CPU 状态卡片

ESP32 配置网页在“温度监测”之后、“设备与传输状态”之前增加独立“Zynq CPU 状态”卡片，分别显示 CPU0 与 CPU1。卡片只读，不提供 CPU 启停操作。

## 显示含义

|区域|字段|含义|
|---|---|---|
|CPU0|运行模式|正常前台运行、安全空闲模式、待机条件未满足或 WFI 不可用|
|CPU0|WFI 功能|宏允许且私有定时器初始化/自检查通过；不等于当前正在睡眠|
|CPU0|累计次数、累计等待|实际完成的 WFI 等待段计数与累计时间；从本次 PS 启动累计|
|CPU0|定时器中断|私有定时器 IRQ 数，包含启动自检；不代表全部 WFI 唤醒次数|
|CPU0|说明|配置占用、传输未结束、排空证明未确认、电源状态未就绪等原因|
|CPU1|控制状态、复位、时钟|读取实际 `A9_CPU_RST_CTRL`，分别显示复位和停钟位|
|CPU1|控制寄存器|原始十六进制值；bit1=复位、bit5=停钟，两者都为 1 才显示固定禁用|

CPU0 的“安全空闲模式”表示采样时双待机及硬件排空条件已确认，CPU0 可按原有严格中断/队列检查自动进出 WFI。**正在执行上报代码时 CPU0 是醒着的，因此网页不能当成瞬时睡眠指示器。** 可在一段时间内观察 WFI 次数和累计等待是否增长。命令、温度定时器及其他中断会短暂唤醒 CPU0；累计等待包含中断返回等开销，不能直接换算精确功耗。

CPU1 的“固定禁用”表示复位与时钟停止，并非独立电源切断。若两位未同时成立，网页显示实际控制状态，不推断 CPU1 正在执行任务，也不改变原有“禁用失败仅告警”的策略。

## 更新与异常状态

数据路径为 `PS 前台 → 现有 UART0 → ESP32 缓存 → GET /api/status → 网页`：

- 首次前台服务、稳定双待机进入/退出及 WFI 故障时上报；周期上报复用原有约 10 秒温度采样期限，不增加定时器或定时唤醒。
- 网页沿用现有刷新周期，只读取 ESP32 缓存，不定期发送 `STATUS` 或其他 UART 命令。无需打开 USB monitor。
- UART 控制/日志发送尚未排空时推迟上报。上报使用现有有限 20 ms UART 发送路径，失败不循环重发。
- 启动标记清除旧值；UART 溢出/接收错误使状态失效。超过 30 秒无新报告标为过期。
- 未收到报告、旧 PS 不支持上报、数据过期或网页断连时显示未知/等待/过期，温度和 CPU 卡片各自维护有效性，不把旧值或 0 当作新测量。

`/api/status` 新增 `cpus` 对象：`status/period_ms/age_ms/seq`，以及 `cpu0`、`cpu1`。无效状态下数值/布尔字段为 `null`；CPU0 `mode/reason` 为 `unknown`。原有 `STATUS` 文本及其 128 字节上限不变，图像、温度及控制协议不变。

UART 使用独立 `@CPUS1` 行，9 个固定 8 位十六进制字段依次为：序号、WFI 功能位、模式、原因、累计次数、累计等待微秒低/高 32 位、定时器 IRQ 数、CPU 控制寄存器；末尾 `*` 后为 IEEE CRC32，覆盖 `*` 前全部 ASCII 字节。整行含 CRLF 共 98 字节；解析缓冲固定 128 字节。CRC/长度/枚举错误、重复或旧序号均拒绝，序号支持 32 位回绕。此报告不增加第三份图像缓冲或接收任务。

## 更新方法

**需要同时更新 Zynq 应用和 ESP32 固件。无需更换 bit、XSA、FSBL；本次未覆盖 BOOT.BIN，也未操作板卡。**

Vitis 正常构建、下载当前应用，或选用已构建的默认 SDIO/WFI=1 ELF：

`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/B_mode_focus_imaging.elf`

与当前 IDE 的 `workspace/B_mode_focus_imaging/build/B_mode_focus_imaging.elf` 相同，SHA256：

`AB5EC5F9DF33B66684BBA50C38B4F6AAD6553501CBA9829C9CAE93ABCC6A1A0E`

在 `esp32c61` 目录手动更新 ESP32（示例端口按实际修改）：

```powershell
.\build.ps1 -Action flash -Port COM3
.\build.ps1 -Action monitor -Port COM3
```

日常默认 combined 固件：

`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/esp32c61/build-throughput-fast-socket-exp-combined/ultrasound_psram_udp.bin`

SHA256：`8A9C8E36759DC382B35A597A6252349B11D50A3840713B6A0CA13D87E06A8418`。

两端更新后重新打开网页，等待第一组 CPU 状态。只更新 ESP32 时，新卡片会等待支持 `@CPUS1` 的 PS 应用；不应把该现象误判为 CPU 故障。

## 2026-10-25 离线验证与产物

修改前工作区干净，基线为 `47ca6ce`；相关源及旧固件备份在 `build/backups/cpu_status_card_47ca6ce/`。

- 实际协议/缓存/JSON 自检查通过：CRC、严格格式、分段、中文日志混入、长行、溢出、重启、重复/旧报告、序号回绕、过期、64 位等待统计及 CPU1 四种控制位组合。
- CPU 缓存和 JSON 源行/分支方向覆盖均为 100%；真实 CPU0 适配层行覆盖 100%、分支方向 94.19%；既有 WFI 核心保持 100%/100%。共享内联协议的防御性格式化失败分支未全部触发，不宣称所有新增代码分支均达标。
- 原有原始 UDP、帧 TCP/UDP、动态长度、USB、温度、UART、Wi-Fi 控制、AFE/TX 电源回归通过；Python 214 项通过、1 项历史 TXT 测试跳过，另新增 4 项 CPU 源码通路约束通过；网页 32 项、图像网页 5 项、构建/PS 集成 32 项通过。
- USB/SDIO × WFI 开关四种应用、当前 IDE 默认应用及 ESP-IDF 默认/combined 两种配置构建通过。已有 AFE/USB 编译警告仍存在，不宣称清零。
- 使用本地模拟数据预览卡片，确认双栏布局和数值渲染；这不是板测。

|应用|text B|data B|bss B|
|---|---:|---:|---:|
|SDIO WFI=1 / 默认|224027|140604|166032|
|SDIO WFI=0|222747|140604|166032|
|USB WFI=1|217691|140604|164976|
|USB WFI=0|216411|140604|164976|

默认 PS 相对上次版本增加 text 2080 B、bss 32 B；分配区末端 `0x00185070`，未进入 USB/DDR 帧保留区。ESP32 应用镜像 986288 B，比备份增加 6000 B，应用分区仍余 549712 B（约 36%）；没有新增任务栈或整帧分配。镜像大小不能当作运行时堆余量。

四档及默认 ELF 在 `build/output/cpu_status_card_20261025/`；完整绝对路径/哈希在 `esp32c61/logs/cpu_status_card_manifest_20261025.json`，测试/构建日志在 `esp32c61/logs/cpu_status_card_20261025/`。bit/XSA/FSBL/BOOT.BIN 与本任务前哈希一致。日志、备份及生成物遵循现有 Git 忽略规则。

尚未烧录或上板验证本次卡片。后续可在正常、单颗待机、双待机及显式唤醒条件下对照状态、累计量和约 10 秒刷新；验证 UART 丢失/重启/断连时不会显示旧值。实际功耗、通信吞吐和长期稳定性仍需分别验收。
