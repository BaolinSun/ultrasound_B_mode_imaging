# TX7332 独立 STDBY 待机与唤醒

## 范围

本功能从已验证 AFE 分支的 `092cad8` 开始，位于 `feature/tx7332-power-control`。
仅增加 TX7332 的 STDBY 引脚控制；不切换动态 LDO 或片内波束合成模式，不复位 TX，不改变 AFE 电源状态。
不移植旧整机待机分支中的 CPU/WFI、时钟配置及温度故障诊断改动。

依据 `D:/MyProjects/zynq_prj/TX7332.pdf` 第 13、14、33 页：Standby 典型为 0.6 mW/通道，典型唤醒 0.1 ms。
本实现默认在撤销 STDBY 并收到新硬件确认后至少等待 1 ms；毫秒计时使用两拍门限消除向下取整误差，实际等待约 1～2 ms。
典型值不是保证最大值，功耗及模拟恢复仍需实测。
Standby 会冻结片内时钟与 T/R 状态，因此只能在正常停扫、SPI 和数据事务完全结束后进入。

## 使用

必须配套更新 bit/XSA、Zynq 应用与 ESP32 固件；旧 bit 没有 UTX1 接口时禁止启动成像或高压，不提供旧接口回退。
网页的“TX7332 电源控制”卡片提供“TX 待机”“唤醒 TX”“查询 TX 状态”。
网页及温度曲线继续在线；进入及恢复后扫描、高压和上传均保持停止。
AFE 与 TX 可依次独立待机、按任意顺序唤醒，只有两者均就绪后才能手动启动成像。

以下命令在 `esp32c61` 目录执行，只发送明确指定的命令，不自动启动扫描或高压：

```powershell
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "TX7332 STATUS"
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "TX7332 SLEEP"
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "TX7332 WAKE"
```

`SLEEP/WAKE` 必须单独提交，内部 `TX7332 PREPARE` 不允许通过网页高级输入或 Python 直接提交。
GPIO11→MIO26 及既有 UART 请求/回执封装不变，不需要 USB 口令。

## 状态与安全流程

状态：`ready → quiescing → sleep → waking → ready`；异常为 `fault`，失联或不匹配为 `unknown`。

进入流程：禁止新提交 → PS 关闭高压、请求正常停扫（不截断当前 TX 配置） → ESP32 排空已接受帧 →
PS 确认 USB/SDIO DMA 及租约释放 → DDR 硬件 PAUSE → 全部 SPI 引擎、排队请求及扫描实际空闲 → STDBY 确认。
进入总期限 90 秒；失败不强制掉电、不强行撤销 DMA 或复用帧槽位。

恢复流程：保持禁止启动 → 撤销 STDBY、保留事务门控 → 等待匹配回执及至少 1 ms → 解除 TX 门控。
恢复 PS 期限 5 秒（ESP32 包含通信余量为 7 秒），不下发整套 TX 配置，不产生发射脉冲。
`ready` 是数字控制与引脚状态证明，不是 TX 模拟输出、T/R 实际状态或高压电压测量。
现有 TX7332 RD 的固定延时/合并读数没有被拿来充当恢复证明。

AFE 已睡眠时，接收 FIFO 写时钟可能停止。此时只允许复用本次 PS 启动后仍有效的 PAUSE 证明：
ACK/SEQ、ABI、结果及 paused/idle 标志匹配，且无新 INIT/RESUME/租约操作；否则拒绝 TX 电源操作。
AFE/TX 分别持有门控票，唤醒任意一颗不会解除另一颗的限制。配置、AFE 与 TX 电源过渡互斥。
高压关闭、安全停止及查询始终可用；故障不自动重试或恢复扫描。

`STATUS` 增加 `tx=<状态>`；`TX7332 STATUS` 返回 `@TX state=... flags=... idle=... error=...`。
其中 `idle` 指 USB/SDIO 传输和租约是否空闲；TX 硬件事务空闲另看 flags bit0，两者都不能代替模拟输出测量。
HTTP `/api/status` 的 `zynq_control` 增加 `tx_state/tx_flags/tx_error/tx_age_ms`。
网页查询只读缓存；用户明确查询才发送 UART，不持续轮询芯片。

## UTX1 寄存器与 CDC

参数窗口偏移 `0x004C0340..0x004C037F`，PS 基地址 `0x404C0340`；原 AFE/帧缓存 ABI 不变。

|相对偏移|内容|
|---|---|
|0x00|只读版本 `0x55545831`（UTX1）|
|0x04|控制影子：bit0 HOLD，bit1 STDBY；先写元数据再提交|
|0x08|非零请求编号；忙时不允许修改在途元数据|
|0x0C|已完成请求编号；重复编号不重复执行|
|0x10|bit0 实际事务空闲；bit1 HOLD；bit2 STDBY；bit3 控制就绪；bit4 错误；bit5 请求处理中|

控制数据从参数时钟域保持到 TX 200 MHz 域确认，回执数据同样保持；请求/确认各两级同步。
空闲状态先在源域寄存后再两级同步。局部 XDC 限定握手数据延迟，仅例外首级同步器，不关闭整域时序检查。
TX 域连续八拍空闲才操作 STDBY，包含四路真实移位器、触发流水级、配置子状态机、待读请求及实际扫描状态。
不复位共享时钟、AXI、SDIO、JTAG 或 AFE 接收链；不改变既有 ILA 探针。

## 验证与板测边界

离线入口：`build/scripts/test_tx_power.ps1`、`esp32c61/tests/run_tests.ps1`。
覆盖状态/所有权/超时/旧回执/复位、四路真实 SPI 读写排空，以及 AFE/TX 独立睡眠与唤醒组合。
构建及资源、时序、CDC 结果以同目录交付记录为准，不以仿真代替板测。

本次不自动下载、烧录或覆盖 BOOT.BIN。另行确认设备后再验收：

1. 正常基线：确认扫描与高压状态、传输完整性。
2. 高压关闭条件下，分别测试 TX 待机、唤醒及反复循环。
3. 验证 AFE/TX 两种进入顺序、两种唤醒顺序，网页与温度持续在线。
4. 验证 USB、SDIO/TCP/UDP 活跃传输时安全排空，拔线或超时不得误报告成功。
5. 单独授权后恢复成像；测各电源轨功耗，记录温度/时钟异常并执行 48 小时稳定性测试。

本功能不视为修复既有温度相关时钟问题。
