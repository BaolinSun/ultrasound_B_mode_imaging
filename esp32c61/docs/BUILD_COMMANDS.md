# build.ps1 命令使用说明

本文命令在 **PowerShell** 中执行，工作目录为 `esp32c61`，不是 `docs`。

```powershell
Set-Location 'D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61'
```

本文 `COM3` 仅为示例，请替换为当前设备端口。本文提供操作说明，不表示已经执行烧录或板测。

## 1. 日常使用：性能配置

当前速度优先的常用配置为 `-Profile throughput`，默认 TCP 调优档为 `fast`，使用 `build-throughput-fast-socket` 目录。建议编译、烧录和 monitor 始终选择同一配置。

```powershell
# 编译性能配置，不烧录
.\build.ps1 -Profile throughput

# 烧录性能配置；IDF 会按需先编译
.\build.ps1 -Profile throughput -Action flash -Port COM3

# 查看同一配置的串口日志，不执行 flash
.\build.ps1 -Profile throughput -Action monitor -Port COM3
```

monitor 中按 `Ctrl+]` 退出并返回 PowerShell。同一个串口不要同时被其他 monitor 或串口工具占用。打开 monitor 可能按 IDF 默认行为复位 ESP32，应避开正在进行的重要传输。

## 2. 仅激活 ESP-IDF 环境

```powershell
.\build.ps1 -Action activate
idf.py --version
```

`activate` 只加载并检查 ESP-IDF 6.1 环境，不配置工程、不编译、不烧录、不打开串口。执行后可在**同一个 PowerShell 终端**直接使用 `idf.py`，无需再次激活，也不需要点源语法。

不要另起 `powershell -File` 或 `pwsh -File` 子进程来激活父终端；子进程退出后不能把环境传回父终端。新开的其他终端仍需各自激活。

除 `activate` 外的脚本操作也会先激活环境。脚本不永久修改系统环境变量；`activate` 不切换当前目录，其他操作结束后恢复原工作目录。

### 激活后直接使用 idf.py

默认配置：

```powershell
idf.py build
idf.py -p COM3 monitor
```

当前性能配置：

```powershell
idf.py -B .\build-throughput-fast-socket build
idf.py -B .\build-throughput-fast-socket -p COM3 monitor
```

**环境激活不等于性能配置选择。** 之前执行过 `-Profile throughput`，不意味着后续裸 `idf.py build` 会自动使用它；裸命令仍使用默认 `build` 目录。性能目录首次生成及配置管理建议使用 `build.ps1`。

## 3. Action 参数速查

每次调用选择一个 `-Action`；省略时默认为 `build`。

| Action | 用途 | 示例 |
|---|---|---|
| `activate` | 仅激活当前终端环境 | `.\build.ps1 -Action activate` |
| `build` | 编译固件，不烧录 | `.\build.ps1 -Profile throughput -Action build` |
| `size` | 查看固件静态内存占用 | `.\build.ps1 -Profile throughput -Action size` |
| `flash` | 编译并烧录所选配置，需要端口 | `.\build.ps1 -Profile throughput -Action flash -Port COM3` |
| `monitor` | 打开所选配置的日志监视器，需要端口 | `.\build.ps1 -Profile throughput -Action monitor -Port COM3` |
| `menuconfig` | 编辑所选配置，不自动烧录 | `.\build.ps1 -Profile throughput -Action menuconfig` |

除 `activate` 外，脚本会检查目标为 `esp32c61`；缺少配置或 Kconfig 更新时可能先执行配置刷新，其他构建动作也可能按 IDF 依赖触发编译。`monitor` 不调用 `flash`。修改配置后需重新编译并烧录才影响板上固件。

`size` 是静态内存报告，不等于运行时空闲堆或最低堆余量。

## 4. 默认配置与性能配置

| 参数组合 | 构建目录 | sdkconfig |
|---|---|---|
| 不指定配置参数 | `build` | `esp32c61/sdkconfig` |
| `-Profile throughput` | `build-throughput-fast-socket` | 构建目录内的 `sdkconfig` |
| `-Profile throughput -TcpTuning mid` | `build-throughput-mid-socket` | 构建目录内的 `sdkconfig` |
| `-Profile throughput -Transport udp` | `build-throughput-udp-fast-socket` | 构建目录内的 `sdkconfig` |

默认配置命令示例：

```powershell
.\build.ps1
.\build.ps1 -Action size
.\build.ps1 -Action flash -Port COM3
.\build.ps1 -Action monitor -Port COM3
```

不同目录的固件不要混淆；默认配置不等同于吞吐性能配置。脚本保留已有 sdkconfig，不会每次恢复出厂配置。要调整已有配置，应对同一配置运行 `menuconfig`。

## 5. TCP 调优与协议对照（可选）

`-TcpTuning` 支持以下档位，主要选择上传连接的 TCP 发送容量，并非所有连接统一使用该容量：

| 档位 | 上传 TCP 发送容量 |
|---|---:|
| `baseline` | 5760 B |
| `mid` | 11520 B |
| `fast` | 23040 B |
| `faster` | 28800 B |
| `large` | 46080 B |
| `xlarge` | 57600 B |

默认 `auto`：`throughput` 选择 `fast`，默认配置选择 `baseline`。增大容量会消耗更多内部内存，不保证吞吐更高；`large/xlarge` 属于显式实验档，不建议无测量直接升档。

```powershell
# 中档性能配置，仅编译
.\build.ps1 -Profile throughput -TcpTuning mid

# 如需烧录该档及查看日志，保持参数一致
.\build.ps1 -Profile throughput -TcpTuning mid -Action flash -Port COM3
.\build.ps1 -Profile throughput -TcpTuning mid -Action monitor -Port COM3
```

`-Transport auto` 为默认值，遵循 `main/wifi_transport_config.h` 的 C 宏。`-Transport tcp/udp` 是构建期对照覆盖，分别使用隔离目录，不是网页运行时切换；只影响 SDIO 帧上传，不改变原始 PSRAM UDP 自测。

```powershell
# 显式编译 UDP 帧模式用于对照，不烧录
.\build.ps1 -Profile throughput -Transport udp
```

显式选择 `-TcpTuning` 或 `-Transport` 也会启用隔离目录。一般目录名为 `build-<Profile>-<TcpTuning>-socket`；显式协议时为 `build-<Profile>-<Transport>-<TcpTuning>-socket`。烧录或 monitor 同样需要带上对应参数。

## 6. 独立数据路径实验（可选）

新增`-Experiment none|core|direct|combined|psram80`，默认none，不改变原命令选择。
例如仅编译“核心锁+减少应用复制”、保持40 MHz：

```powershell
.\build.ps1 -Profile throughput -Experiment combined
```

使用`build-throughput-fast-socket-exp-combined`，不会改原性能目录的sdkconfig。
flash/monitor也必须带同一个Experiment。80 MHz仅是隔离实验配置，未确认器件及PCB前不要烧录。
全部档位、测速及回退流程见[TCP 数据路径实验](TCP_DATA_PATH_EXPERIMENTS.md)。

## 7. ESP-IDF 安装路径

脚本默认使用：

- ESP-IDF：`D:\Program Files\esp\v6.1\esp-idf`。
- 安装器激活脚本：`C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1`。

其他安装位置可同时指定两个参数（替换为实际路径）：

```powershell
.\build.ps1 -Action activate -IdfPath 'D:\Program Files\esp\v6.1\esp-idf' -ActivationScript 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
```

若激活脚本不存在，必须预先具备已激活的 IDF Python 环境；脚本仍检查 IDF 版本为 6.1。路径有空格时应保留引号。

返回 [文档索引](README.md)。
