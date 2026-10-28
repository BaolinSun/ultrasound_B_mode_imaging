# 冷启动失败后按温度自动恢复 PS 图像源

## 行为

适用于本次观察到的：冷机启动时帧缓存初始化失败，出现
`@DDR ERROR frame-store ABI/init` 和
`@SDIO SOURCE_UNAVAILABLE frame-store init/ABI; upload disabled`；
升温后重新下载应用可以恢复的情况。该现象与温度、时钟的因果关系仍需独立硬件验证。

新增逻辑只在**本次上电首次帧缓存初始化失败**、且启动时已确认继承 USB DMA 空闲的情况下启用。
正常启动不等待升温，不增加任何自动恢复；正常运行之后出现的错误不触发本机制。

1. 沿用每 10 秒温度采样，使用 **Zynq 片内 XADC 温度达到 60.00°C** 作为门限。外部电源/AFE 温度不参与触发。
2. 温度读取失败或缓存超过 10 秒时不执行；不依赖 USB monitor 或网页是否打开。
3. 同时要求扫描、TX 配置启动请求和高压均关闭，无参数事务、待处理 UART 命令、USB 图像/回执 DMA 或 SDIO 帧租约；TX 就绪，AFE 未掉电，现有 AFE 控制版本、DCLK/MMCM/IDELAY/FCLK 就绪标志均合法。
4. 先让 SDIO 退出旧握手，有限等待确认本地主机空闲，再重新初始化帧缓存。
5. 重新绑定 AFE 启动状态及新接收代次，清除冷启动留下的 AFE 失败门控；确认帧缓存 PAUSE 排空成功，再恢复 SDIO 图像源握手。
6. 成功后停止自动重试。扫描、高压、上传继续保持关闭，由用户重新查询状态并手动启动。

每次失败后至少间隔 **30 秒**才允许再试；等待期间继续服务温度、UART/USB 命令及正常前台逻辑。
单次帧缓存 INIT 和 PAUSE 各沿用 3 秒截止时间，AFE 代次确认沿用 100 ms，均非无限等待。
手动 AFE/TX 电源操作优先，执行后取消本次自动恢复，避免自动初始化覆盖用户的待机/恢复流程。
仅查询状态不会取消；配置及危险启动在恢复等待期间返回 `BOOT_RECOVERY_WAIT_TEMP_OR_CLOCK`。
关闭高压、停止扫描、状态查询始终可用。

门限和重试间隔位于 `build/src/ps/system_ctrl/startup_recovery.h`，当前 Vitis 源中有同步副本。
该门限是针对现有板级现象的软件恢复条件，并非推荐工作温度或温度保护限值；原温度保护保持不变。

## 日志

```text
@BOOT_RECOVERY WAIT_TEMP threshold_mC=60000 retry_ms=30000; scan/HV/upload blocked
@BOOT_RECOVERY TRY attempt=1 zynq_mC=...
@BOOT_RECOVERY DDR rc=0 stage=6 version=55424631 ...
@BOOT_RECOVERY READY attempts=1; scan=0 hv=0 upload=stopped; query PS status then start manually
@SDIO ONLINE SD1 4bit; waiting webpage start
```

温度达标但实际时钟/接口条件不满足时，每 30 秒最多提示一次：

```text
@BOOT_RECOVERY WAIT_CLOCK zynq_mC=... version=... control=... flags=...
```

温度达到 60°C **不等于时钟已恢复**，本逻辑不会伪造 READY。其他失败阶段显示
`RETRY SDIO_NOT_IDLE`、`DDR rc=-1`、`RETRY AFE_NOT_READY` 或 `RETRY DDR_PAUSE`。
启动时若继承 USB DMA 无法证明确认空闲，则显示 `DISABLED`，必须人工排查，不自动撤销租约。

## 更新与验收

- **仅更新 Zynq 应用 ELF**。无需更换 bit/XSA、FSBL 或 ESP32 固件；未覆盖 BOOT.BIN。
- 不调用完整 `AFE5832_init()`、`para_down()`，不重新初始化 GPIO、USB 控制器、系统中断或 CPU；不覆盖已下发的模拟参数或下采样配置。
- AFE 重绑定只沿用现有接收链局部复位/新代次确认，不修改 MMCM 频率、AFE SPI/PLL 初始化、外部时钟或 RTL。
- 冷启动发生失败后保持设备正常供电，观察温度及以上日志。READY 后在网页查询 Zynq/AFE/TX 状态，再按需手动启动扫描、高压和上传。
- 若持续 WAIT_CLOCK，本功能不能修复物理时钟异常；保留日志中的标志和温度继续定位。

本轮离线自检查覆盖门限、温度有效性、忙碌准入、手动操作取消、各阶段失败、30 秒退避、计时回绕、成功后不再次初始化，以及禁止自动扫描/高压/上传。
已完成 USB/SDIO 应用构建与链接地址检查；**未连接板卡，冷启动升温自动恢复效果尚需实际验收**。

## 本次构建结果（2026-10-28）

- Vitis IDE / 标准输出（SDIO）：
  `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf`
  与 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging.elf`。
  SHA256：`c51fed21a02770e9cc3399e659608e8bb62340c33a76e7a47247903bc27d6c94`。
- USB 对照产物：
  `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\cold_start_recovery_20261028\B_mode_focus_imaging_USB.elf`。
  SHA256：`b8cc9c97517a271e2ca0a2cbbae3d9d017a4a59707327bff0f9ed51b60264a35`。
- 同目录 `manifest.json` 记录完整产物路径、哈希及测试摘要。修改前相关源文件、旧 ELF 在
  `build/backups/cold_start_recovery_20261028`。
- SDIO 链接结果：text 227515 B / data 140604 B / bss 166064 B，
  相比修改前 IDE ELF，text 增加 2784 B、bss 增加 32 B；未增加整帧缓冲。
  应用分配结束 `0x00185090`，未侵入 USB 保留区或 DDR 三帧槽位。
- 新恢复模块 self-check：生产代码行覆盖率 100%、分支覆盖率 100%。
  原生 C 回归通过；conda `esp32c61` 下 241 项 Python 测试中 240 通过、1 项历史样本缺失跳过，
  网页 43 项、图像页 5 项通过。实际冷启动/温升/成像恢复尚未板测。
- 汇总回归日志：`esp32c61/logs/startup_recovery_final_regression_20261028.log`；
  构建日志：`startup_recovery_apps_20261028.log`、`startup_recovery_ide_20261028.log`（均位于同一 logs 目录）。
