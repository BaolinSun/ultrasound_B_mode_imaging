# build.ps1 命令使用说明

本文命令在 **PowerShell** 中执行，工作目录为 `esp32c61`，不是 `docs`。

```powershell
Set-Location 'D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61'
```

本文 `COM3` 仅为示例，请替换为当前设备端口。本文提供操作说明，不表示已经执行烧录或板测。

## 1. 日常使用：性能配置

当前默认就是 **combined 40 MHz 性能档**：`Profile=throughput`、`TcpTuning=fast`、`Experiment=combined`。
启用 lwIP 核心锁/输入锁、线程安全检查和 TCP PSRAM 直接提交，上传发送缓冲23040 B，普通HTTP socket仍5760 B。
无参数脚本使用 `build-throughput-fast-socket-exp-combined` 目录。编译、烧录和 monitor 始终选择同一配置。

```powershell
# 编译性能配置，不烧录
.\build.ps1

# 烧录性能配置；IDF 会按需先编译
.\build.ps1 -Action flash -Port COM3

# 查看同一配置的串口日志，不执行 flash
.\build.ps1 -Action monitor -Port COM3
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

裸 `idf.py` 使用根目录sdkconfig与`build`，本机该配置已同步为相同的combined性能参数；新建配置也从`sdkconfig.defaults`取得这些默认值：

```powershell
idf.py build
idf.py -p COM3 monitor
```

当前性能配置：

```powershell
idf.py -B .\build-throughput-fast-socket-exp-combined build
idf.py -B .\build-throughput-fast-socket-exp-combined -p COM3 monitor
```

**环境激活不等于构建目录选择。** 裸命令仍使用`build`；脚本默认使用隔离combined目录。两者是不同产物路径，但本机已经核对性能参数一致。
`sdkconfig.defaults`只为新配置提供默认值，不会覆盖其它电脑上已有的手动sdkconfig；已有旧配置请优先使用默认`build.ps1`，不要以为切换源码就自动迁移其全部设置。

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
| 不指定配置参数 / `-Profile throughput` | `build-throughput-fast-socket-exp-combined` | 构建目录内的 `sdkconfig` |
| `-TcpTuning mid` | `build-throughput-mid-socket-exp-combined` | 构建目录内的 `sdkconfig` |
| `-Transport udp` | `build-throughput-udp-fast-socket-exp-combined` | 构建目录内的 `sdkconfig` |
| `-Profile default -Experiment none` / 裸`idf.py` | `build` | `esp32c61/sdkconfig` |
| `-Experiment none` | `build-throughput-fast-socket` | 保留该目录已有配置，不强制combined |

默认配置命令示例：

```powershell
.\build.ps1
.\build.ps1 -Action size
.\build.ps1 -Action flash -Port COM3
.\build.ps1 -Action monitor -Port COM3
```

不同目录的固件不要混淆；脚本保留已有sdkconfig，不会每次恢复出厂配置。`-Profile default`是兼容的配置名称，不再表示日常默认选项；`-Experiment none`只是不应用实验覆盖，**不是恢复旧固件**。
要调整已有配置，应对同一目录运行menuconfig；combined若被改成不匹配的核心锁/路径/频率，脚本会拒绝继续而不静默烧录。

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

`auto`在默认`throughput`下选择`fast`；显式`-Profile default`才选择`baseline`容量。增大容量会消耗更多内部内存，不保证吞吐更高；`large/xlarge`属于显式实验档，不建议无测量直接升档。

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

`-Experiment none|core|direct|combined|psram80`，现默认combined。
例如下面两条命令等价，仅编译“核心锁+减少应用复制”、保持40 MHz：

```powershell
.\build.ps1 -Profile throughput -Experiment combined
.\build.ps1
```

使用`build-throughput-fast-socket-exp-combined`，不会改原性能目录的sdkconfig。
选择core/direct/psram80时，flash/monitor仍必须带同一个Experiment；combined日常命令可以省略。
80 MHz不作为默认，本轮10分钟测试没有显示端到端优势，详见[8倍长测报告](TCP_10MIN_8X_20261021.md)。
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

## 8. combined 默认切换验证（2026-10-21）

修改前任务相关实验/测速内容已保存为Git检查点 `697f7e6`。
本机旧根sdkconfig另备份为：
`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\default_combined_20261021\sdkconfig.before`。
仅定向更新默认性能选项，没有整文件替换用户的其它配置。

- 无参数`.\build.ps1`构建通过，使用`build-throughput-fast-socket-exp-combined`。
- 激活环境后裸`idf.py build`构建通过，使用根`build`。
- 两个产物均为971616 B，应用分区尚余约37%；不是可用堆内存百分比。
- 从空sdkconfig实际运行IDF配置流程，核对新建配置同样为combined；三份有效配置的核心锁/输入锁、线程安全检查、直接提交、IRAM优化、40 MHz、23040 B上传/5760 B普通socket及TX BA=32均一致。
- Python回归204项：203通过、1项缺少历史样本跳过；其中构建入口模拟测试9项通过，包含默认选择和monitor不烧录。
- 本次未连接、下载或烧录板卡；已安装固件不会因源码默认值变化自动更新。

构建/测试日志统一在`esp32c61/logs/default_combined_*_20261021*.log`。
两个不同构建目录的镜像可能因编译时间等元数据而具有不同哈希，不代表关键性能配置不同。

返回 [文档索引](README.md)。
