# Wi-Fi 下发 Zynq 命令

AFE 独立电源控制新增 `AFE SLEEP`、`AFE WAKE`、`AFE STATUS`。SLEEP/WAKE 每批只能一条，恢复后不会自动启动扫描、高压或上传；安全流程、配套 bit 要求见 [AFE 电源说明](AFE_POWER.md)。

## 1. 通路与使用前提

```text
网页 / Python → ESP32 HTTP → GPIO11/TX → Zynq MIO26/UART0 RX
PS 回执 / 日志 / 温度 → MIO27/TX → ESP32 GPIO10/RX → 网页缓存 / USB monitor
```

UART0 使用 115200、8N1、无流控，双方共地。不是将 monitor 键盘输入透传给 Zynq；UART1 的 460800 波特率和原 USB 配置入口继续保留。命令不放入图像 TCP/UDP 载荷，也不依赖图像目标 PC IP 或是否打开 monitor。

需要同时更新本功能对应的 **Zynq PS ELF 和 ESP32 固件**，仅更新其中一端无法完成协议握手。PL、bit、XSA 未修改，BOOT.BIN 不自动覆盖。构建方法见 [build.ps1 命令](BUILD_COMMANDS.md)。实际下载、烧录和发射操作须由用户确认条件安全后执行。

### 电池供电与访问边界

控制不需要连接 USB、打开 monitor 或输入控制口令；电池供电、Wi-Fi 连接后，直接打开网页或执行下面的 Python 命令。Wi-Fi 配网与目标地址的 NVS 保存行为不变。

已移除控制口令的生成、NVS 读取/保存、USB 打印和 HTTP 请求头校验。旧固件留下的 NVS 口令记录不再读取，不需要擦除 NVS 或重新配网。

**控制接口不鉴权，HTTP 未加密：能访问 ESP32 的网络设备也能下发命令。仅限可信、受控的局域网，不得映射到公网或接入不可信共享网络。** 客户端编号用于事务互斥，不是身份认证；网页确认和 `--allow-hv` 防止误操作，不是网络访问控制。

高压开启仍要求网页明确确认或 Python 显式 `--allow-hv`；命令校验、配置互斥、停流排空、重复请求识别和 Wi-Fi `CFG END` 保持高压关闭等机制不变。

## 2. 常用操作

以下命令在 `esp32c61` 目录的 PowerShell 执行，无需口令或环境变量。IP 替换为 ESP32 的实际地址。

```powershell
# 查询 PS 软件状态，不启动成像
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 --status

# 停止扫描，同时关闭高压
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "BEAM STOP"

# 停机并关闭高压
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "HV DISABLE"

# 在线设置接收数字增益，不自动重启扫描
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "GAIN SET 2"
```

以下两项会改变设备工作状态，只能在硬件及发射条件安全时明确执行；文档展示不代表允许自动执行：

```powershell
# 明确开启高压，必须带 --allow-hv
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "HV ENABLE" --allow-hv

# 开始扫描，本命令不自动开启高压
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "BEAM START"
```

网页“Zynq 控制与配置”卡片提供扫描、高压和高级多行命令；数字增益与下采样位于独立的“超声参数控制”卡片。“安全停止”和“关闭高压并停机”直接执行 `HV DISABLE`，优先关闭高压，再撤销扫描请求；不是硬件急停装置。网络断开或 PS 无响应时，不能保证远程关断已生效。

“SDIO 单帧/连续”只传输 DDR，不启动扫描；“停止全部传输”仅取消上传，不等同于关闭高压。

单条 `HV DISABLE` 具有优先关断路径：即使普通控制批次忙碌，也可进入唯一的有界关断待处理槽，打断排空或回执等待、停止后续普通命令，再由同一串口发送任务执行。原批次可能返回失败、未知或因结果缓存替换而不可查询；不要自动重发。关断仍须收到 PS 回执才能确认软件操作完成，不保证网络/串口/PS 故障时的物理关断。

普通扫描运行不附带网络失联自动关高压功能；配置事务的 60 秒看门狗仅保护正在进行的 Wi-Fi 配置。重启 ESP32 也不能当作关闭 Zynq 高压的手段。需要停止时明确发送 HV DISABLE，并核对回执及实际硬件状态。

### 网页下采样设置

扫描后出现 `DDR_DRAIN_OR_ABI` / `DDR_DRAIN_BUSY` 的 PL 排空修复见
[PAUSE 修复说明](DDR_PAUSE_FIX.md)。该修复需同时更新 bit 与 PS ELF，不能只更新 PS 或 ESP32；
下面“首次网页下采样功能无需改 bit”的说明针对原功能入口，不包含此次 PL 忙状态修复。

ESP32 页“超声参数控制 → 图像下采样”提供五档选择：

| 抽取倍率 | `T_timing_hex[9]` | 每线传输点数 | 每帧字节数 |
|---|---:|---:|---:|
| 1 倍（不抽取） | 0 | 6144 | 787456 |
| 2 倍 | 1 | 3072 | 394240 |
| 4 倍 | 2 | 1536 | 197632 |
| 8 倍 | 3 | 768 | 99328 |
| 16 倍 | 4 | 384 | 50176 |

选择后点击“停扫并应用下采样”，网页一次提交以下四条命令，复用有界异步任务、配置归属和排空超时：

```text
CFG BEGIN <随机事务编号>
BRAM WR t_timing 9 1 <0～4>
CFG END
STATUS
```

先禁止新上传并排空已接受数据，再由 PS 停扫、关闭高压及等待帧缓存 PAUSE 确认，之后才写参数。任一步失败不继续配置，不自动重试；结果未知时先查询状态。安全停止按钮仍可使用。只改第 9 个 32 位字；原始 6144 点、64 线及其他扫描时序不变。

网页核对逐条 ACK 的事务编号、命令及最终 `STATUS` 的 `seq/down/cfg/run/hv`，只有档位匹配、事务结束、扫描与高压关闭才报告成功。成功后上传也保持停止，**不会自动开启高压、扫描或接收**。使用步骤：

1. 在 ESP32 页查询 Zynq 状态，选择倍率并应用，等待“配置成功”。
2. 按实际安全条件，通过现有按钮手动开启高压（如需要，仍需确认）和开始扫描；二者独立，不由下采样功能代为执行。
3. 在 PC 图像页重新点击“开始接收”，不要同时点击 ESP32 的 SDIO 按钮；普通接收工具则先启动监听，再点击 SDIO 单帧/连续。
4. 查看新完整帧的实际档位、点数与帧长。若 PC 正在接收时应用配置，设备会正常排空停流，PC 可能提示“上传已结束”，需要手动重新开始接收。

界面区分待设置倍率、PS 已下发配置和最近完整接收帧。`STATUS down` 是 PS 写入成功路径的软件记录，扫描时序地址不能可靠回读，不代表 PL 已接受。`sdio.last_received_bytes` / `last_received_frame` 仅在完整帧 CRC 通过后更新，SDIO 新会话清零；网页按长度推导最近接收档位，旧帧不会冒充新配置。PC 图像页进一步按线头核验真正的图像格式。

旧 PS 固件没有合法 `down` 字段时禁用下采样应用并提示更新；网页轮询仍只读缓存，不周期性下发串口查询。下采样不写 NVS，重启采用 C 数组默认档位。仅减少输出数据量，不保证采集或显示帧率等比提高；大倍率降低轴向采样密度。

2026-10-25 网页兼容修复：如果 PS 回执已有 `down=0`，页面却仍提示缺少 `down`，可能是旧网页不接受字段名中的数字，遇到 `cpu1_disabled=1` 后拒绝了整条状态。修复后字段名允许数字出现在首字符之后，兼容 CPU1/WFI 及后续扩展字段，同时继续拒绝重复字段和非法格式；配置忙碌、AFE/TX 未就绪等禁止条件保持不变。此修复只需更新 ESP32 固件，然后强制刷新页面，不需要重新下载 Zynq、更新 bit/XSA 或改变下采样参数。

在 `esp32c61` 目录退出占用串口的 monitor 后，使用原构建脚本更新（端口以实际连接为准）：

```powershell
.\build.ps1 -Action flash -Port COM3
```

烧录完成后用 `Ctrl+F5` 刷新 `http://192.168.3.27/`，再点击“查询状态”。修复已通过包含完整 CPU1/WFI 状态的五档配置回归及默认/combined 固件构建；未在本次诊断中自动烧录或应用配置。

本功能需要更新 PS ELF 与 ESP32 固件，不需要更新 bit/XSA，也不自动修改 BOOT.BIN。软件记录不能代替物理扫描或高压状态确认。

## 3. 全部原始命令

命令区分大小写，使用单个空格分隔；整数支持无符号十进制或 `0x` 十六进制。Wi-Fi 命令不接受符号、额外参数、嵌入换行或 `*`，每条最多 159 字节。命令文件每行一条，空行和以 `#` 开头的整行注释忽略。

| 命令 | 参数与作用 | 配置事务要求 |
|---|---|---|
| `STATUS` | 查询协议版本、事务和软件控制状态 | 无 |
| `CPU1 DISABLE` | 显式使 CPU1 复位并停钟；不自动启动它，不改变 CPU0/成像状态 | 不允许配置事务进行中；仅 Wi-Fi 入口 |
| `CPU0 SLEEP` | ESP32 编排：TX 待机确认 → 等 1 秒 → AFE 待机；不向 PS 发送同名命令 | 每批一条，无配置事务 |
| `CPU0 WAKE` | ESP32 编排：AFE 就绪确认 → 等 1 秒 → TX 唤醒；保持扫描、高压、上传停止 | 每批一条，无配置事务 |
| `CFG BEGIN <seq>` | seq 为 0～4294967295；关闭高压、停止扫描并进入配置 | 当前没有其他事务 |
| `CFG END` | 恢复 AFE 正常寄存器映射并结束配置；Wi-Fi 保持高压关闭、扫描停止 | 当前客户端持有 |
| `CFG ABORT` | 中止配置、关闭高压并停机；不回滚已写入的寄存器 | 只能清理自己的事务；无事务时可停机 |
| `AFE WR <map> <addr> <data>` | map 为 `GLOBAL/ADC/VCA/DTGC`；addr 0～255，data 0～65535 | 当前客户端持有 |
| `AFE RD <map> <addr>` | 读取 AFE 寄存器；同上 map/addr 范围 | 当前客户端持有 |
| `BRAM WR <section> <offset> <count> <words...>` | offset 为字索引；count 1～8；每个 word 为 32 位无符号数 | 当前客户端持有 |
| `TX7332 WR tx7332_config_data <offset> <count> <words...>` | offset 为字索引；count 1～8；每个 word 为 32 位无符号数 | 当前客户端持有 |
| `TX7332 RD <addr>` | addr 0～1023；运行中可能返回忙，应先停止扫描 | 无 |
| `BEAM START` | 恢复扫描请求并重新触发 TX 配置；不自动开启高压 | 不允许在配置中启动 |
| `BEAM STOP` | 先关闭高压，再清除扫描与 TX 配置启动请求；不再保留高压 | 停止操作可跨入口执行 |
| `HV ENABLE` | 开启高压使能 GPIO；网页要求确认，Python 要求 `--allow-hv` | 不允许在配置中开启 |
| `HV DISABLE` | 先关闭高压使能，再完全停止扫描请求 | 关闭操作可跨入口执行 |
| `GAIN SET <shift>` | shift 0～8；每步约 +6.02 dB，放大噪声并可能饱和；不自动改变模拟增益 | 无事务时可在线修改；有事务时仅所属客户端 |

这里的“停止”延续当前 PS/PL 控制语义，**不代表新增了硬件排空、当前脉冲完成或安全待机确认**。本功能未引入低功耗状态机，也没有对 PS 命令增加物理完成回读。

### BRAM 区域

| section | 深度（32 位字） | 用途 |
|---|---:|---|
| `fdemod_sin` | 8192 | 解调正弦系数 |
| `fdemod_cos` | 8192 | 解调余弦系数 |
| `log_table` | 1024 | 对数查表 |
| `x_element` | 64 | 阵元位置 |
| `hamming` | 64 | 加窗权重 |
| `dfilter` | 64 | 数字滤波系数 |
| `t_timing` | 10 | 扫描时序；第 0 个字必须保持 6144 |
| `sin_beta` | 64 | 扫描方向参数 |
| `delay_profile` | 16384 | 延迟表 |

`TX7332 WR` 的 `tx7332_config_data` 深度为 128 个 32 位字，不属于 `BRAM WR` 区域。所有写入均要求 `offset + count <= depth` 且实际数据字数等于 count。每个 32 位字的具体位域沿用现有 PS/PL 及参考 JSON，不由 Wi-Fi 重新解释。

当前固定上传为每帧 787456 字节，Wi-Fi 入口拒绝将 `t_timing[0]` 改成非 6144；不会修改或自动适配 ESP32/PC 帧长。原 USB/UART 接口不增加此兼容限制，混用时应自行保持一致。现有 DDR 覆盖及帧起点问题仍未解决。

## 4. JSON 与文本批量下发

工具独立实现参考工程的参数打包规则，支持相同的 AFE/DTGC、TX7332 和 BRAM JSON 格式，不导入参考工程代码、不自动使用其默认 JSON。

```powershell
# 先检查并显示命令；不联网、不生成运行日志
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 --target afe5832 --config 'D:\configs\afe5832.json' --dry-run

# 下发明确指定的 JSON；自动包裹 CFG BEGIN / END
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 --target afe5832 --config 'D:\configs\afe5832.json'

# 扫描时序文件中第 0 个字必须是 6144
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 --target t_timing --config 'D:\configs\t_timing.json'

# 文本文件包含完整命令序列，包括需要的 CFG BEGIN / END
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 --file 'D:\configs\commands.txt'
```

`--target` 支持 `afe5832`、`tx7332`、`delay_profile`、`demod_coeffs`、`log_table`、`x_element`、`hamming`、`dfilter`、`t_timing`、`sin_beta`、`all`。

单目标可用 `--config`，也可用 `--afe5832-config`、`--tx7332-config`、`--delay-profile-config`、`--demod-coeffs-config`、`--log-table-config`、`--x-element-config`、`--hamming-config`、`--dfilter-config`、`--t-timing-config`、`--sin-beta-config`。`--target all` 要求明确指定上述全部文件。`demod_coeffs` 文件须同时包含 `fdemod_sin` 和 `fdemod_cos`；其他 BRAM 目标文件只包含对应区域。

JSON 全部在 PC 校验并生成命令后才联网；无效文件不会产生部分写入。但执行途中硬件失败、断网、中止不会回滚此前成功的寄存器写入。配置失败后应完整重新配置，而不是把 ABORT 当成回滚。

参考工程 `t_timing_default.json` 为 2048 点，**不能直接用于当前 6144 点链路**；工具会拒绝，不会静默修改输入。

批量配置先阻止新上传 BEGIN，再等待当前 SDIO 帧、待发送完整帧和发送回调结束，最多等待 90 秒。正常排空不取消在途帧；异常按原数据通路规则处理，未释放资源时不强制复用。配置期间禁止新上传，结束后不会自动重新上传、开启高压或启动成像。

### 回执与报告

```text
@ACK <seq> <operation>
@ACK <seq> GAIN_SET <实际shift>
@RD <seq> <addr> <AFE数据>
@RD <seq> TX7332 <addr> <TX数据>
@NACK <seq> <error>
@STATUS v=1 cfg=<0/1> owner=<0/1/2> seq=<seq> run=<0/1> hv=<0/1> run_src=ps spi=<0/1> down=<0～4> afe=<state> tx=<state> cpu1_disabled=<0/1> wfi=<0/1>
```

owner：0 为 UART1，1 为 USB，2 为 Wi-Fi，仅在 cfg=1 时代表事务所有者。`run_src=ps` 表示 run 是 **PS 最后下发的扫描请求缓存**，spi 同样是 PS 下发的 SPI 启动请求；hv 是使能 GPIO 读回。它们都不是实际高压电压、PL 扫描活动或事务排空的证明。PL 复位或 JTAG 直接写寄存器也不会同步修改 PS 缓存。

当前 bit 中 `0x40011528/0x4001152C` 支持 SPI/扫描请求写入，但静态参数读通路没有包含这两个地址；此前直接读 `0x4001152C` 得到的 `run=0` 不能证明未启动扫描。修复只涉及 PS，不把缓存伪装成硬件反馈。开始扫描不自动使能高压；停止请求仍保留既有整帧停止语义。

该问题的板测、构建及 RAM 下载边界见 [扫描状态修复记录](SCAN_RUN_STATUS_FIX.md)。

运行报告默认生成 `logs/zynq-control-日期时间-随机后缀.json`，不覆盖已有文件；也可指定 `--log example.json`，自动放入 `logs`。返回码 0 为全部命令已收到成功回执，1 为失败或结果未知；参数错误为 2。

常见错误：`NO_CFG`、`CFG_ACTIVE`、`CFG_OWNER`、`AFE_MAP_FAIL`、`AFE_WR_FAIL`、`AFE_RD_FAIL`、`BRAM_RANGE`、`TX7332_RANGE`、`BEAM_ACTIVE_USE_BEAM_STOP`、`FRAME_LAYOUT_6144_REQUIRED`、`HANDSHAKE_REQUIRED`、`STALE_REQUEST`、`REQUEST_CONFLICT`、`DRAIN_TIMEOUT`。参数解析也可能返回 `*_ARGS`、`*_DATA`、`*_OP` 或 `UNKNOWN_CMD`。

2026-10-20 起，帧缓存未初始化、ABI 不匹配或恢复失败时，`BEAM START` 返回 `@NACK <seq> DDR_RESUME_OR_ABI`，而不是在 run=0 时误报成功。失败路径保持扫描、高压关闭；正常 `BEAM START` 仍不自动开启高压。此时应先解决 `@DDR INIT` 错误，不能以反复点击 SDIO 单帧恢复。旧 USB/UART 配置结束时的自动启动若失败，也会返回该 NACK。详见 [DDR 初始化诊断](DDR_INIT_DIAGNOSIS.md)。

失败立即停止后续命令。客户端仅在已确认持有事务且结果明确失败时尝试 ABORT；结果未知时不自动重发。PS Wi-Fi 配置事务连续 60 秒没有有效命令会关闭高压并清理事务，不自动恢复成像。网页状态轮询只读取缓存，不给串口事务续期。

## 5. HTTP 与串口协议

控制接口不要求认证请求头；POST 仍要求 `Content-Type: application/json`，不提供跨域 CORS 放行。控制任务尚未就绪时返回 HTTP 503，不再返回口令校验的 HTTP 401。CORS 不等于网络访问控制。

`POST /api/zynq/commands` 示例请求体：

```json
{"client_id":"1234abcd","request_id":"00000001","commands":["STATUS"],"allow_hv":false}
```

客户端号、请求号均为非零的 8 位十六进制字符串；每批 1～8 条命令，请求体最多 2048 字节。接受后返回 HTTP 202 和 `{"job_id":"..."}`；GET `/api/zynq/jobs/<job_id>` 查询 `busy/state/count/completed/results/error`。`state` 为 `running/succeeded/failed/unknown`，优先关断等待执行时为 `queued`。

仅保存一个活动批次和一个已完成批次；同一客户端及请求编号在缓存中重复提交相同命令返回原任务，不重复发送，内容冲突则拒绝。任务退出缓存后不能通过重发旧 POST 恢复结果，HTTP 断开也不代表命令没有执行。工具不自动重试 POST。

配置事务期间只允许所属客户端继续配置；其他客户端及 USB/UART 写入被拒绝，关闭高压、暂停扫描和状态查询保留。Python 默认每次进程生成新客户端号；如确需跨多次调用维持同一事务，可显式使用相同 `--client-id`，但更推荐一个文本文件完成完整事务。

串口封装：

```text
@WFC1 <session:8hex> <request:8hex> <length:decimal> <command>*<crc32:8hex>\n
@WFR1 <session:8hex> <request:8hex> <length:decimal> <response>*<crc32:8hex>\n
```

CRC32 为 IEEE/zlib，覆盖 `*` 前所有 ASCII 字节；length 只计内部命令或回执。整行上限 256 字节。串口会话独立于 HTTP 客户端号，ESP32 生成会话并单调递增请求号；PS 仅对匹配请求返回封装回执。首次或恢复时先 STATUS 握手，内部命令不自动重放。PS 保留最近一次响应，并拒绝已经越过的旧请求号；复位后不能推断复位前未知命令是否执行。

单条 UART 命令等待最多 20 秒；Python 每批等待默认 300 秒，可通过 `--timeout` 调整。优先关断在尚未确认会话时先按顺序发送只读 STATUS 建立会话，但不等待它的回执，再单次发送 HV DISABLE，避免被额外的查询超时阻挡。CRC 只检测损坏，不提供串口来源认证；当前 HTTP 控制接口也不鉴权，须由可信网络隔离限制访问。

## 6. 软件验证与板测边界

### 网页下采样配置（2026-10-20）

- 完整主机 C 自检查通过，包含真实 PS 命令解析、MMIO 参数写入/软件记录、ESP 控制排空、CRC 通过后记录最近帧及失败恢复。PS 三个修改文件在规范源和 Vitis 源中逐字节一致。
- Python 回归运行 169 项，168 项通过、1 项因历史异常样本缺失跳过；网页 30 项通过。覆盖五档长度、TCP/UDP、TXT、切换后的点数/采样率/深度/平滑、无效档位、事务归属、排空失败不写参数、回执不匹配、未知结果、重复点击及不自动启动。
- 回归同时发现 TCP 接收工具在 Windows 低分辨率计时器下极短接收可能得到 0 秒，导致汇总除零。现返回 null 速率而非崩溃或虚构吞吐，新增固定零时差测试。首次测试环境无 NumPy/Pillow，最终使用已有 `C:\Users\sunbaolin\miniconda3\envs\py-utils\python.exe` 完成全部回归，没有安装或修改系统依赖。故障注入日志中的 “Soak FAILED” 为预期用例，以最终 unittest OK 和进程退出码 0 为准。
- Vitis 2026.1 USB/SDIO 及日常 workspace 均构建通过，DDR 布局检查通过。SDIO text/data/bss=186075/140604/163312 B，USB=179803/140604/162256 B。既有 USB fall-through、声明与符号比较等警告仍保留，未宣称本次修复。
- ESP-IDF 6.1 默认/throughput-fast 均构建通过，BIN 分别为 963536/965120 B，应用分区均剩余 37%；静态 DIRAM 123599/135561 B，两档 .bss 均为 31064 B、.data 均为 16187 B。新增最近完整帧统计为两个 uint32 字段，不新增任务或整帧缓冲；静态余量不是运行时堆低水位。内嵌网页较本轮备份增加 5429 B，未改 TCP 缓冲调优。
- 当前 SDIO ELF 已同步到 `build/output/B_mode_focus_imaging.elf`。本次不连接硬件、不下载/烧录、不更新 bit/XSA 或 BOOT.BIN；真实五档帧率、图像质量和长期稳定性尚未板测。备份与离线日志位于 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\web_decimation_20261020_145243`。

本轮可用产物（绝对路径；应用 BIN 不是完整 Flash 镜像，更新 ESP32 应使用原构建脚本）：

```text
Zynq SDIO / 当前 Vitis:
D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf
SHA256: 75054DB528A9606FD00260400DF7A081E60DA133F039297387130B1185448D95

ESP32 默认:
D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build\ultrasound_psram_udp.bin
SHA256: C6FEC6F4C8B3AA5E82CCA1C8FE1E1FFA2740D78FE8B101E77020BF9B2F998719

ESP32 性能档:
D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket\ultrasound_psram_udp.bin
SHA256: 39730F7FCE591461FFB768115C59BED58A37315B46841FDBD1F19A4CA55362E2
```

### 当前版本：移除 USB 口令（2026-10-16）

- 本次只修改 ESP32 控制接口、网页、Python 工具及相关测试文档；不修改 PS、PL 或数据传输协议。已使用前述 Wi-Fi 命令 PS ELF 时，只需更新 ESP32 固件和 PC 工具，不需要重新下载 PS 或生成 BOOT.BIN。
- ESP-IDF 6.1 默认与 throughput/fast 配置均构建成功；BIN 分别为 949712 B、951280 B，静态 DIRAM 分别为 123559 B、135521 B。高压确认、事务所有权、排空、超时和优先关断机制保留。
- 原生 C 自检查通过；Python 129 项中 128 项通过，唯一失败仍为此前已有的规范/管理 BD 文本差异。控制工具专项 10 项及网页 11 项通过，包括无口令下发/查询、高压确认和服务未就绪拒绝；不以软件测试替代板测。
- 新固件位于 `build/output/wifi_control_no_token_20261016/`。原 `wifi_control_20261016/` 中的 ESP32 BIN 是移除口令前的历史版本，不要混用；PS ELF 可继续使用原版本。
- 路径与 SHA256 见 [无口令版构建清单](../logs/wifi-control-no-token-build-manifest.json)，回归及构建日志为 `esp32c61/logs/wifi-control-no-token-*.log`。
- 未烧录、未 JTAG 下载、未开启高压或启动成像；BOOT.BIN、bit 和 XSA 的哈希保持不变。待实际更新固件后，再验证电池供电、无 monitor 时的控制。

### 初版软件结果（2026-10-16，移除口令前）

- Vitis 2026.1 的 SDIO、USB 两种 PS 应用均完成构建。SDIO 应用 text/data/bss 为 179707/140604/163152 B；USB 为 174907/140604/162192 B。当前 Vitis 源与规范源的本次修改一致。
- ESP-IDF 6.1 的默认和 throughput/fast 配置均构建通过，应用分区仍约剩余 38%。默认 DIRAM 为 123599/256624 B（48.16%），性能配置为 135561/256624 B（52.82%）；这些是静态占用，不是运行时堆余量。
- 新增控制任务栈为 4096 B，回执队列、批次与优先关断槽有界；HTTP JSON 的临时分配及上传期间的真实堆低水位仍需板测。没有新增整帧 PSRAM 缓冲区。
- 原生 C 自检查通过，包括实际 PS 解析、ESP 控制任务、HTTP 处理器、AFE SPI 有限等待，以及既有数据传输和 UART/温度回归。优先关断测试覆盖普通批次已排队、正在等待回执两种情况。
- Python 全量回归 128 项，127 项通过；唯一失败为原有 `test_source_and_bd_copies_match` 中规范 BD 与 Vivado 管理 BD 的文本比较。本次没有修改 BD 或掩盖该失败。网页交互 11 项通过。新增 Python 控制/JSON/参考一致性测试 9 项通过。
- 初版未烧录、未 JTAG 下载、未启动成像或开启高压，未覆盖 BOOT.BIN。

新产物集中在 `build/output/wifi_control_20261016/`：`B_mode_focus_imaging_sdio.elf`、`B_mode_focus_imaging_usb.elf`、`esp32c61_default.bin`、`esp32c61_throughput.bin`。当前 Vitis 可用 ELF 仍位于 `workspace/B_mode_focus_imaging/build/B_mode_focus_imaging.elf`，对应 SDIO。ESP32 建议通过原构建目录的 `build.ps1` 烧录完整镜像组合，不把单独的应用 bin 当作完整 Flash 镜像。

绝对路径、SHA256、备份目录和未修改的 BOOT/bit/XSA 哈希见 [构建清单](../logs/wifi-control-build-manifest.json)。日志见 `esp32c61/logs/wifi-control-*.log`；最终 ESP32 build 日志同时包含 size 结果。原 `build/output/BOOT.BIN` 仍是旧版本，不能据此认为新控制功能已经写入启动镜像。

自检查运行实际 PS 解析和 ESP 控制任务的主机桩，覆盖安全 END、所有者、SPI 错误传播相关接口、CRC/分段/溢出、重复/迟到、排空、超时及失联。参数打包与只读参考工程逐项比较；网页测试覆盖无需口令、高压确认及上传门控。

板测尚需逐项验证：两端固件更新后查询状态；monitor 不连接时仍可配置；完整 JSON 下发后高压关闭；USB/UART 冲突拒绝；连续上传中排空配置；两端重启及断线恢复；实际吞吐与内部堆最低余量。只有在另行确认后才允许开启高压/扫描。软件测试不能替代真实电压、成像正确性或 48 小时板级稳定性验证。

## TX7332 独立待机命令

- `TX7332 SLEEP`：两阶段安全停扫、排空传输后进入 STDBY。
- `TX7332 WAKE`：撤销 STDBY，等待恢复，保持扫描、高压和上传停止。
- `TX7332 STATUS`：只读查询状态、标志及失败原因，不隐式唤醒。

电源操作必须单独提交，内部 PREPARE 不对外开放；不改变现有 TX7332 WR/RD。
需要配套 bit/XSA、PS 和 ESP32 固件；AFE/TX 独立状态及接线、测试说明见 [TX7332 电源控制](TX7332_POWER.md)。

返回 [文档索引](README.md)。


## 网页面板顺序与运行诊断（2026-10-28）

面板依次为：Zynq 控制与配置、Zynq CPU 状态、AFE / TX 电源管理、超声参数控制、温度监测、设备与传输状态、SDIO 测试、连接与目标地址。
数字增益和五档下采样的命令、停扫/排空和准入规则不变；“超声参数控制”同步显示最近命令执行结果，异常不会自动重发。

“设备与传输状态”新增可视化的 Wi-Fi、SDIO 主机握手、DDR 源端状态、上传任务、TCP 连接及目标配置状态，并显示：

- ESP32 本次启动后的运行时长，帮助区分 ESP32 重启和 SDIO 局部复位。
- 内部 SRAM 当前空闲、历史低水位和 TCP 配置的保护预留；保护预留不是已分配内存。
- SDIO 累计复位次数及最近原因；`host_abort` 表示启动/主机重握手，不能当成主动停止错误。
- TCP 连接/回执错误计数、最近错误原因及 Wi-Fi 最近断开原因码。

历史错误计数和内存低水位不代表当前仍故障；空闲时 TCP 尚未建连属于正常情况。DDR 是经 SDIO 同步的初始化/完整帧等待/ABI 状态，不冒充实时整机健康结论。
状态缺失时显示未知，网页断连后清除当前状态展示，不继续显示旧的绿色状态。仅复用现有网页轮询；新增 `uptime_ms` 由 ESP32 本地单调时钟生成，不向 Zynq 周期查询，不影响 CPU0 WFI。

本次只更新 ESP32 网页及状态接口，不修改 PS、PL、bit/XSA 或 BOOT.BIN。更新 ESP32 固件后强制刷新页面（Ctrl+F5）。离线网页交互测试 46 项通过；实际无线和板级行为不以离线测试代替。


## 超声实时图像：启动与打开

在接收电脑的 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61` 目录，使用已安装显示依赖的 Python 环境执行：

```powershell
python .\pc_tools\bmode_web_viewer.py --device-ip 192.168.3.27
```

保持程序运行，再在**同一台电脑**打开 ESP32 的“超声成像系统控制台”，点击 **Zynq 控制与配置 → 超声实时图像**。
按钮在新标签页打开 `http://127.0.0.1:8080/`，原控制台保留；在图像页点击“开始接收”，无需再点设备页的 SDIO 单帧或连续测试。

- 图像按钮只是打开网页，不会自动启动 Python、扫描、高压或上传；成像仍通过已有控制入口手动启动。
- ESP32 的上传目标必须指向运行本程序的电脑，数据端口默认 `50001`。
- `127.0.0.1` 是当前浏览器所在电脑；手机或其他电脑不能通过这个入口访问接收电脑。
- 无法打开一般说明本机图像服务尚未启动；自定义 `--http-port` 时直接打开 Python 打印的地址。
- “停止接收”只停止上传，不负责关闭扫描或高压。关闭浏览器不会自动停止后台上传，结束时先点“停止接收”。

缺少依赖时执行 `python -m pip install -r .\pc_tools\requirements-viewer.txt`。更完整的操作和数据格式见 [Wi-Fi 图像显示说明](WIFI_IMAGE_VIEWER.md)。
原独立“实时超声图像”卡片已移除，命令及操作说明集中在本文；温度面板以三路数值卡片和深色趋势图显示，每 10 秒采样、仅保留最近 6 分钟，错误或漏采样仍保留断点。

## 网页高级参数编辑入口

控制台“Zynq 控制与配置 → 高级参数配置”支持 AFE/TX 寄存器和全部系数表、JSON 导入导出及批次下发。
原始采样固定 6144 点，`t_timing[0]` 与 `[8]` 必须为 6144，仅 `[9]` 改变抽取。
配置后保持扫描、高压、上传停止；详细流程及 PC 显示参数见 [网页参数配置](WEB_PARAMETERS.md)。
