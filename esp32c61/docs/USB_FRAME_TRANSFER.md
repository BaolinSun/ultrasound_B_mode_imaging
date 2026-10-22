# USB 受保护完整帧传输（2026-10-22）

## 当前行为

Zynq 使用现有 DDR 三缓冲的申请/释放接口，申请最新完整帧后，持有租约到
USB 最后一个 DMA 描述符成功完成且端点空闲。正常发送、停止和异常清理都不允许
PL 覆盖正在读取的槽位。热下载后，在 DDR 帧缓存 INIT 之前先有限停止遗留 USB DMA；
不能确认停止时禁用帧缓存初始化，不强行收回租约。

**USB 仅支持不抽取：`T_timing_hex[9]=0`，6144 点 × 64 线，787456 字节。**
软件配置和申请到的帧描述符都必须满足条件；不满足只拒绝，不替用户改参数。
Wi-Fi 仍支持五档动态帧长。此次没有修改 PL、帧缓存 ABI、SDIO 协议或 ESP32 固件。

每次 `EF 01 10 00` 请求返回一份完整帧：48 × 16384 + 1024 字节。
PC 最后一次 IN 完成后可立即请求下一帧；允许暂存一个已满足末描述符成功条件的请求，
但必须等上一槽位释放后才开始下一次申请。当前帧尚未完成时忽略提前请求，待处理请求最多一个；
完成后的每个有效请求都视为下一次取帧，旧协议没有请求编号去重机制。
不补零、不重复帧、不增加帧头或结束标记，不在分块之间发送 ZLP。
保留 VID/PID `0424:4940`，请求端点 `0x01`、图像端点 `0x81`。
高速 Bulk 最大包长为 512 字节，不能与软件的 16 KiB 发送块混淆。
本版本要求 USB 高速连接；更新后建议断开重连，使新描述符生效。

每份图像保留原来的 8 个 16 位线头字，采用小端序。上位机必须验证全部 64 条顺序线：
`0x1C00 / 0x1800`、线号低 7 位为 0～63、抽取字段低 4 位为 0。
第 64 条线由固定帧长度验证，不依赖不存在的第 65 条线头。
停止扫描后，可以再次请求最后一帧，因此 USB 请求/显示 FPS 不一定等于独立采集 FPS。

原始 USB 没有新增应用 CRC 或 PC 回执；发送完成表示 USB DMA 完成，
不是 PC 图像处理完成或文件持久化证明。不要将本结果与 Wi-Fi 的 PC CRC 回执混为一谈。

## 输出选择与构建

默认仍为 SDIO/Wi-Fi，USB/SDIO 图像输出互斥。C 宏位于：

- 规范源 `build/src/ps/data_transfer/output_config.h`
- 当前 Vitis 源 `workspace/B_mode_focus_imaging/src/data_transfer/output_config.h`

正式选择 USB 时，将两处 `DATA_OUTPUT_MODE` 同步改为 `DATA_OUTPUT_USB` 并重新编译应用。
Vitis 下载运行只需匹配的应用 ELF，**本次无需更换 bit/XSA**。SD 卡自启动所用的旧
BOOT.BIN 不会自动更新；本次没有打包或覆盖它，也没有下载、烧录、启动扫描或高压。

在 ultrasound_B_mode_imaging 根目录，离线构建两种宏模式：

```powershell
.\build\scripts\build_frame_store_apps.ps1
```

脚本用编译参数分别覆盖为 USB/SDIO，不改变默认头文件选择，不访问板卡。
产物与 2026-10-22 SHA256：

| 模式 | ELF 绝对路径 | SHA256 |
|---|---|---|
| USB | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\vitis_frame_store_usb\B_mode_focus_imaging.elf` | `AF5D90F0A5FBF9E0C892A2DBD56593AC54740CC68621B0C0E67D4F0A281F708D` |
| SDIO | `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\vitis_frame_store_sdio\B_mode_focus_imaging.elf` | `163729B26523DDB9E20EB420793AC7B3659B9FD54A8384D6AB15DB11275996C3` |

| 模式 | text | data | bss | 分配段最高结束地址 |
|---|---:|---:|---:|---|
| USB | 189851 | 140604 | 164624 | `0x0017CAF0` |
| SDIO | 196027 | 140604 | 165680 | `0x0017CF10` |

均通过链接检查，不与 USB 保留区 `0x1C000000` 或帧槽位 `0x20000000～0x202FFFFF` 重叠。
没有增加第三份整帧软件缓存。旧 USB 示例代码的已有未使用参数等编译警告仍存在，
不能把“构建通过”理解为这些旧警告全部清零。

## 配套上位机

`D:\MyProjects\py_prj\ultrasound_dev_kit` 新分支为 `feature/usb-protected-frame`，
在原 `para_autotune` 分支上建立，原有 `.gitignore` 修改原样保留且不纳入功能提交。
原 GUI、TGC、LUT、扫描转换及高压/扫描控制行为不变。

在该上位机目录及其已安装依赖的 Python 环境执行：

```powershell
python .\ultrasound_desktop_app.py
```

独立单帧接收/原有 MAT 保存与绘图入口：

```powershell
python .\utils\usb_reader.py
```

不要将新的 787456 字节发送端与旧的 1024000 字节窗口读取器混用。
接收开始不负责生成图像；先用已有控制入口配置不抽取并生成完整 DDR 帧。
高压及扫描操作遵守原上位机的明确控制流程，本次改动不新增自动开启行为。
不要同时运行两个占用 USB 端点的接收/配置程序；配置前停止接收，完成后重新建立接收会话。

## 停止和错误恢复

每次固件请求总截止为 4 秒，上位机一次收帧共用 5 秒截止，拆分读取不重置计时。
没有完整帧时不自动启动扫描；非零抽取、无帧超时或描述符错误会报告 `@USB ... error=...`。

USB 停止、断开、复位或错误后先有限 flush（100 ms）并确认 DMA 空闲，才释放租约。
无法确认时保留槽位并进入故障，不持续在主循环反复等待。配置开始时禁止新请求，
排空已接受的图像，再执行原停扫/DDR 排空流程；排空失败报告 `USB_DRAIN`，不写参数。
配置回执使用有界持久缓冲，与图像共用 IN 调度，不穿插图像载荷。

短帧或任何接收错误后，本轮接收停止并在 GUI 明确提示，不自动发送下一请求或跨请求拼帧。
先停止接收程序，断开重连 USB，再重新启动接收；不依靠扫描载荷猜测重新同步位置。
GUI 可保留最后有效图像，但错误状态下不把它当成新图像。

## 离线测试和待做板测

```powershell
.\build\scripts\test_usb_frame.ps1
.\esp32c61\tests\run_tests.ps1
```

上位机目录：

```powershell
python -m unittest discover -s tests -p test_usb_frame_protocol.py -v
```

USB 状态机的行/分支取值覆盖均 100%；实际端点适配器 USB 模式行覆盖 100%，
分支取值覆盖超过 90%。同时覆盖 SDIO 编译模式及 BSP 未括号计时宏形态。
模拟断网、停止、错误、复位、回执隔离和租约保留均不等于真实硬件验证。
上位机 10 项测试验证全部 64 线、拆分读取、单一截止、坏帧拒绝及冻结时错误提示；
合成帧走原 TGC/扫描转换/LUT 后灰度逐像素一致。
现有 C 回归通过；Python 204 项中 203 项通过、1 项因历史坏 TXT 样本已删除而跳过，
网页测试 26 + 5 项通过。USB 自检查还覆盖末包到达后下一请求与 IN 中断/前台释放的三种先后顺序。

实际板测另行确认：先在高压关闭条件下生成数据，检查 USB 连续收帧、三槽不覆盖、
停止/重连、配置期间排空及两种输出模式；再进行实际有效吞吐和 48 小时稳定性测试。
本次没有测得 USB 实际 FPS，不宣称达到旧版本 25 FPS。

修改前备份：`build/backups/usb_frame_20261022`。构建/自检日志位于 `build/usb_frame_*.log`，
完整回归日志为 `build/usb_full_regression_final.log`；这些生成文件不纳入 Git 功能提交。
