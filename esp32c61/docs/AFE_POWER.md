# AFE5832 独立全局掉电与恢复

本阶段只控制 AFE5832 的 PDN_GBL，不包含 TX7332 Standby、CPU WFI、CPU1 操作、DDR 低功耗或外部时钟配置。基线 `4d8c78c`，功能分支 `feature/afe5832-power-control`；旧待机分支没有整体合并。

## 使用

更新配套 PL bit、Zynq 应用和 ESP32 固件后，使用配置页的“AFE5832 电源控制”卡片。上电仍正常初始化 AFE，不自动待机。唤醒只恢复停机就绪，不自动恢复扫描、高压或上传。

在 `esp32c61` 目录也可运行：

```powershell
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "AFE STATUS"
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "AFE SLEEP"
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "AFE WAKE"
```

每个 SLEEP/WAKE 独占一批，不与配置或启动命令拼批。PREPARE 为内部协调命令，不允许 HTTP/Python 单独提交。查询不唤醒；网页轮询只读缓存，不发送周期性 UART 查询。

状态未知或未与配套 PS 握手时，上传互锁不解除。原始 UDP 自测格式、TCP/UDP 图像协议及接收工具不变。

## 流程与互锁

状态：`READY → QUIESCING → SLEEP → WAKING → READY`，异常进入 `FAULT`。

- ESP32 禁止新上传并发送 PREPARE；PS 关闭高压、清除扫描与 TX 配置启动请求，禁止新图像请求及参数写入。
- 保持 AFE 时钟，让已接受的 SDIO/USB/Wi-Fi 事务结束；不使用取消接口冒充正常排空。ESP32 检查 PSRAM/发送回调，再只读查询 PS，等待最后的 SDIO 租约和 USB DMA 退休。
- 同一会话提交 SLEEP 后，PS 检查空闲和帧缓存 PAUSE 硬件确认，保存关键寄存器，置接收局部复位，最后拉高 GPIO71/PDN_GBL。GPIO70/PDN_FAST 保持低。
- 进入总截止 90 秒。失败不强制释放租约、不复位在途 AXI/DMA；排空未确认时不掉电。GPIO读回不是实际芯片功耗测量。
- 唤醒保持高压/扫描禁止，拉低 PDN_GBL，等待 10 ms。检查关键 ADC/VCA 寄存器保持；不执行 AFE 总复位或整套默认初始化覆盖用户参数。
- 按本地 AFE5832LP 规格书第 165 页 §10.4，在时钟稳定至少 100 µs 后置 0x41/0x42 的 PLLRST1/2，保持 20 µs 再清除，保留其他位；产生 AFE 专用 TX_TRIG 同步脉冲，不触发 TX7332。
- 先恢复 MMCM，接收链仍复位；确认 DCLK 活性、MMCM、IDELAY，再更新恢复代次、释放接收链。必须连续至少 128 次新 FCLK 合法交替且对齐，返回当前代次确认；恒定全 0/全 1 和旧锁定值不能通过。
- 新时钟活动后再完成帧缓存双域清理，报告 READY。恢复截止 5 秒。失败保持互锁，不自动重放、扫描、开启高压或复位整机。

AFE/BRAM/TX 参数写入、USB/UART/Wi-Fi 的扫描和高压入口均受互锁保护；停止与关闭高压始终可用。配置事务冲突时拒绝进入待机。故障允许明确重试唤醒，但未确认排空时拒绝用复位“解卡”。

如果进入流程因网络发送超时而失败，手动重试唤醒仍会检查 ESP32 发送回调/缓冲区和 PS DMA 所有权；未排空时报告忙碌，不会跳过检查或强制复用。请待旧任务结束后再明确重试。

Wi-Fi、网页、日志、LED、三路温度继续运行。DTGC 不被恢复代码重写，其完整保持及恢复后的图像效果需板测。UART 丢失/复位后 ESP32 状态未知并禁止新上传，需用户查询/唤醒。

## 独立 PL 接口与 CDC

参数偏移 `0x004C0300～0x004C033F`，PS 地址 `0x404C0300`；不修改 UBF1 帧缓存 ABI：

| 偏移 | 含义 |
|---|---|
| 0x00 | 版本 `0x55414631`（UAF1） |
| 0x04 | bit0 MMCM 复位，bit1 接收链复位 |
| 0x08 | 恢复代次，接收链复位期间写入 |
| 0x0C | bit0 DCLK 活性，bit1 MMCM，bit2 IDELAY，bit3 新 FCLK 对齐，bit4 当前代次有效 |
| 0x10 | 已确认代次 |
| 0x14 | 8 位 Gray 原始时钟活动计数，不是频率 |
| 0x18 | 接收资格确认标志 |

正常低五位为 `0x1F`，必须结合 PS 状态及恢复代次。调试器直接写控制寄存器会绕过软件排空检查，不是支持的用户入口。

CDC crossing：Gray 计数→两级同步，限定 max-delay/bus-skew；单 bit→两级同步；代次→保持数据请求/确认握手并限定路径。各接收域复位异步置位、同步释放。局部复位不复位共享 AXI/DDR/PS/SDIO/JTAG；不改变 MMCM 频率或 `ila_bf` 原有探针。

## 构建、回退与验收

必须使用配套 bit/XSA、PS ELF 和 ESP32 固件；旧 bit 会报 AFE_ABI。原 release 分支保留，回退需同时恢复配套产物。原磁盘输出备份在 `build/backups/afe_power_4d8c78c/`，不据此保证备份产物全部来自同一 Git 提交。构建结果及哈希见 [构建记录](AFE_POWER_BUILD.md)。

在工程根目录运行离线验证：

```powershell
.\build\scripts\test_afe_power.ps1
.\build\scripts\test_frame_store.ps1
.\esp32c61\tests\run_tests.ps1
```

本次不自动操作板卡、不覆盖 BOOT.BIN。板测另行确认设备：先验证高压关闭下的正常时钟基线、掉电、恢复和温度更新，再验证 USB/SDIO 活跃传输时进入待机及重复循环。成像恢复需单独授权。电源轨功耗、温度相关时钟故障及 48 小时稳定性分别记录，离线仿真不代表这些项目已通过。

高压关闭读回仅确认现有使能 GPIO，不证明高压储能电容已放电。待机时使用 AFE 时钟的 ILA 无法采样属于预期，不等同于 PS/JTAG 已失联；后者仍需板测验证。
