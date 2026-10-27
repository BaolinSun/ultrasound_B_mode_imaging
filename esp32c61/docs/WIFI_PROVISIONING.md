# 无 USB 控制台的二维码配网

每台设备使用独立、长期保留的配置热点名称和密码。二维码用于让手机连接**设备自己的 WPA2 热点**，不是路由器密码，也不会自动打开网页或自动完成配网。

正常使用及换网络无需 USB。设备首次准备、升级固件及恢复整片擦除后的资料，仍需使用烧录接口；这些操作使用烧录工具，不需要打开 `idf.py monitor` 或通过控制台获取密码。

## 日常使用

1. 电池上电。没有保存路由器信息时立即开启设备热点；已有信息但持续连接失败约 30 秒后开启热点。
2. 用手机系统相机扫描机身 Wi-Fi 二维码并连接热点。手机提示无互联网时保持连接；也可手动输入标签上的热点名称和密码。
3. 手动打开 **http://192.168.4.1/**，填写路由器 SSID、密码及 PC 地址/端口，点击“保存并应用”。PC 地址暂不确定时可留空，默认端口为 50001。
4. 页面显示连接结果。成功后复制显示的新局域网地址；热点继续保留 **30 秒**并显示倒计时。
5. 将手机/电脑切回路由器网络，打开刚才记录的地址。ESP32 地址由路由器 DHCP 分配，不固定为 192.168.3.27。

普通 HTTP 页面中的剪贴板接口可能被浏览器限制。复制按钮失败时会提示手动复制页面地址，不要求 USB 辅助。掉线后自动重连，持续失败时重新开启原设备热点，二维码保持有效。这里不包含 captive portal 或 mDNS，扫码后仍需手动打开固定配网页面。

## PC 准备工具

以下命令在 `esp32c61` 目录执行。COM3 和示例 MAC 需替换为实际值。**这些是交付后的人工操作说明，本次实现没有执行设备访问、烧录或复位。**

先在运行工具的 Python 环境安装依赖：

```powershell
python -m pip install -r .\pc_tools\requirements-provisioning.txt
```

工具默认使用本机 ESP-IDF 6.1 源码 `D:\Program Files\esp\v6.1\esp-idf`，并调用安装器 Python 生成 NVS；不依赖参考上位机工程。其他安装位置可在子命令前指定 `--idf-path` 和 `--idf-python`。

### 已有设备：升级后导出原凭据

先正常更新本工程 ESP32 固件及分区表：

```powershell
.\build.ps1 -Action flash -Port COM3
```

旧配置有效且独立设备分区为空时，固件自动保留原热点名称、16 位密码、路由器和目标地址，先保存并读回设备凭据，再迁移网络配置。无需看到任何控制台日志。

导出并生成二维码标签：

```powershell
python .\pc_tools\provision_device.py export --port COM3
```

默认资料目录为 `provisioning/<出厂MAC12位>/`。目录已存在时拒绝覆盖；需要另存时显式指定新目录：

```powershell
python .\pc_tools\provision_device.py export --port COM3 --output-dir .\provisioning\existing_device_export
```

**export 不写 Flash，但会使用 ROM 下载模式并复位 ESP32。** write 也会复位 ESP32。准备设备时应停止上传并关闭占用该端口的其他工具；不应在正常成像过程中调用它们。

### 新设备：预先生成、写入并贴码

已知出厂 MAC 时，生成完全离线的设备资料：

```powershell
python .\pc_tools\provision_device.py generate --mac 10:20:30:40:50:60
```

若尚不知道 MAC，可在准备阶段使用烧录工具读取，无需 monitor：

```powershell
python -m esptool --chip esp32c61 --port COM3 read-mac
```

先更新本工程固件和分区表，再写入为该 MAC 生成的资料：

```powershell
.\build.ps1 -Action flash -Port COM3
python .\pc_tools\provision_device.py write --port COM3 --bundle .\provisioning\102030405060
```

写入前核对芯片、出厂 MAC、实际分区表、镜像内容和哈希；仅写 `device_nvs` 的 24 KiB，完成后逐字节读回验证。不擦除用户 NVS，不改写应用。相同凭据重复写入返回无需写入；不同凭据、MAC 不匹配、损坏数据或不兼容分区表均拒绝，不提供强制覆盖开关。

没有有效设备凭据且没有可迁移的旧配置时，设备不会随机生成未知密码或开启开放热点。必须先完成上述准备；若已有有效路由器信息，STA 仍可尝试连接，以便网页显示准备状态。

### 资料与二维码

每份资料包含：

- `identity.json`：设备 MAC、热点名称和密码，作为恢复原标签的依据。
- `device.csv`、`device_nvs.bin`、`image.sha256`：使用 IDF 官方工具生成的可核验镜像。
- `wifi_qr.png`：标准 `WIFI:T:WPA;S:...;P:...;;` 热点连接二维码。
- `label.txt`：热点名称、密码及固定配网地址，供印制标签。

请将二维码和文字说明一起贴在对应设备上，保留原始资料用于售后恢复。密码只存在这些准备资料及设备凭据分区中，不通过普通网页 API 或控制台日志返回。工具不会在控制台打印密码；`provisioning/` 整目录 Git 忽略，不能当作可丢弃构建目录。

整片擦除 Flash 会同时删除设备凭据。重新烧录配套固件后，用**备份的同一份 bundle**执行 write，即可继续使用原二维码；不要重新 generate 一个新密码来恢复旧标签。

## 存储、接口与兼容性

|分区|偏移|大小|用途|
|---|---|---|---|
|nvs|0x9000|0x6000|原用户 NVS；位置不变|
|phy_init|0xF000|0x1000|原 PHY；位置不变|
|factory|0x10000|0x177000|原应用；位置与容量不变|
|device_nvs|0x3F0000|0x6000|独立设备凭据，单独准备|

Flash 仍为 4 MiB。设备凭据存储版本为 1，使用单个可原子更新的 NVS string：命名空间 `device`、键 `identity`，内容为 `DVC1|出厂MAC12位大写|SSID|16位密码`。SSID 允许 1～32 个 ASCII 字母、数字、短横线和下划线；新设备默认名称 `Ultrasound-C61-<出厂MAC后6位>`。旧设备迁移保留原 SoftAP MAC 后缀名称，导出的二维码使用实际存储值。

用户配置升级为版本 2，不再包含热点密码。版本 1 的原始 140 字节 blob 通过独立兼容结构读取；迁移失败时保留原记录，不释放其唯一凭据副本。设备凭据已持久保存而网络配置迁移暂时失败时，继续使用原网络信息，下次启动重试。用户配置清空或损坏不会重新生成设备密码；本次不新增网页恢复出厂接口，不自动擦除 NVS。

`GET /api/status` 新增：

- `provisioning_state`：`ready`、`unprepared`、`missing_partition`、`invalid`、`mac_mismatch`、`migration_failed`。
- `ap_close_remaining_ms`：热点成功联网后的剩余保留时间；没有活动倒计时时为 0。
- `wifi_disconnect_reason`：最近 Wi-Fi 断开原因码；成功获得 IP 后清零。

`GET/PUT /api/config` 的网页语义不变，不返回任何密码。修改路由器信息会结束当前上传、延迟约 500 ms 应用；只改 PC 地址不重新配网，下一次传输生效。Wi-Fi 凭据保存在用户 NVS，重启恢复。

## 构建、升级与离线验证

日常 `build.ps1` 和 `idf.py flash` 的 Flash 输入仅有 bootloader、分区表及应用，**不包含设备凭据镜像**。脚本只迁移生成 sdkconfig 中的分区选择，保留性能参数；检测到错误实验档配置时在修改前拒绝。其他位置的旧构建配置若仍选择旧分区，CMake 会明确报错，先运行对应 `build.ps1` 更新配置，不会静默构建缺少设备分区的固件。

本次必须更新 **ESP32 分区表和应用**。无需更新 Zynq ELF、bit、XSA、FSBL 或 BOOT.BIN。默认 combined 的编译与烧录仍使用 `build-throughput-fast-socket-exp-combined`。

在成像工程根目录离线运行：

```powershell
.\build\scripts\test_provisioning.ps1
.\esp32c61\tests\run_tests.ps1
```

新增 C 自检查执行真实存储及 Wi-Fi 管理代码，覆盖版本迁移、中断恢复、持久保存失败、MAC 绑定、错误凭据、SDK 初始化故障、30 秒热点回退/关闭及并发配置；PC 自检查使用真实 IDF NVS 生成/解析、实际 PNG，ROM 读写使用模拟设备，不访问串口。

离线构建、覆盖率、分区/Flash 输入和回归结果集中在 `logs/provisioning_*_20261027.*`；产物清单见 `logs/provisioning_manifest_20261027.json`。软件测试不能替代实际手机扫码和无线验收。

2026-10-27 离线验证结果：

- 真实配置存储代码行/分支方向覆盖 100%/100%；凭据编解码 100%/91.86%；真实 Wi-Fi 管理代码 98.73%/94.37%。覆盖迁移各阶段故障、网络配置保存失败、错误 MAC、初始化异常、旧 IP 事件、配置代次变化及热点关闭与新配置请求并发。
- 全量 Python 229 项，228 通过、1 项历史 TXT 样本缺失跳过；网页 33 项、图像网页 5 项及构建/PS 源码集成 32 项通过。二维码工具用实际 IDF NVS 镜像和 PNG 做 9 项自检查，构建入口另有 10 项自检查。
- ESP-IDF 6.1 的默认配置和日常 combined 配置均完成构建；分区位置与应用容量检查通过，正常 Flash 输入不含 `device_nvs.bin`。固件大小、SHA256 及最终内存报告以清单和对应构建日志为准。
- 本次没有新建额外接收/发送任务或整帧缓冲；新增 NVS 分区的运行时堆开销仍需上板观测，不能由 Flash 镜像大小推算。

后续板测需另行确认：使用电池、断开 USB，分别验证新设备配网、错误密码重试、换路由器、成功提示与 30 秒关闭、重启保持、连续上传后恢复配网，以及旧设备迁移后原二维码仍有效。本轮未执行这些板测，也未测量功耗或吞吐变化。
