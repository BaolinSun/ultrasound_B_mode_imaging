# 停止接收保留 SDIO 链路（2026-10-28）

## 修复的问题

旧实现将网页/PC 的主动停止直接转为 SDIO 驱动销毁、重建。TCP 发送回调尚未返回时，
PSRAM 槽位仍为 SENDING，而驱动重建又清除了主机握手标志，后台因此重复触发复位。
主动取消还被当作发送故障再次触发清理。Zynq 同时仍可能执行 CMD52/CMD53，出现连带命令超时、
数据 CRC 错误及重新枚举。用户日志中的数毫秒内密集 `SDIO reset` 与此路径吻合。

本次将**正常停流**与**真实故障重建**分开。冷启动 60°C 恢复功能保留，修改前提交检查点为 `12c0ea9`。

## 正常停止

1. PC 先将 TCP 接收标记为计划停止，保持接收连接，再提交 `/api/sdio/stop`。
2. ESP32 禁止新帧、新窗口额度，发出非零 STOP_REQ 序号并置 STOPPING。
   正在发送的 TCP/UDP 帧允许取消；取消前标记对应槽位，回调返回前不复用 PSRAM。
3. Zynq 在前台观察 STOPPING；已经进入的同步 CMD53 先返回，再确认 SD1 DMA 空闲、释放 DDR 帧租约。
   确认会话及停止序号未变化后才写 STOP_ACK。正在等 BEGIN 回执、等待窗口或等待 DONE 都能进入此路径。
4. ESP32 接收并丢弃晚到的数据块，逐个归还 DMA 描述符。收到本次 STOP_ACK 后才丢弃未完成接收帧；
   继续等待所有发送回调，槽位全空闲后清除 STOPPING。保持 SDIO 驱动、主机握手及停止期间的会话号。
5. 网页显示“正在停止（等待主机与回调）”，禁止重新启动。PC 等待 `busy=false`、
   `sdio.running=false`、`sdio.stopping=false`、`free_slots=2` 后才关闭接收端并显示停止完成。

正常停止不会重新枚举 SDIO，也不会自动停止 Zynq 扫描、关闭高压、改变 AFE/TX 电源或重新初始化 DDR。
图像显示工具仍只管理自己启动的上传，不接管其他任务。
部分帧及尚未发送的 READY 帧可计入丢弃；预期的 `RAW_CANCELLED` **不计入错误，不触发重建**。

## 超时、故障与诊断

- 正常停流总截止 5 秒；PC 最多等待 7 秒，保持接收端直到设备清理完成。
- 未得到主机确认或发送回调超时才进入故障路径。真正的 CRC、驱动、网络或发送失败仍停止本轮测试。
- 故障路径只停止/销毁驱动一次，保留 SENDING/RAW_TEST 槽位，等回调归还所有权后才重新初始化。
  回调一直不返回时保持不可启动，不能强行释放、覆盖缓冲区。初始化失败保持不可用，不紧循环反复分配。
- `/api/status.sdio` 新增 `stopping`、`reset_count`、`reset_reason`。后者标记最近一次实际驱动重建原因，
  如 `host_abort`、`stop_host_timeout`、`stop_callback_timeout`、`sender_failure`、`frame_crc` 等。
- 启动/故障重握手使用的 `host_abort` 会重建驱动，但属于显式握手，不计作数据错误。
  判断停止是否触发重建，应比较开始与停止后的 `reset_count` **增量**，不要求累计值为零。
- 每次新上传仍可生成新的传输会话号；正常停止本身不更换会话。会话变化不等于芯片或整机复位。

正常停止预期日志：

```text
[ZYNQ] @SDIO STOPPED token=...; DMA idle, DDR lease released; link retained
I (...) sdio_bridge: SDIO stopped; host idle and callbacks returned; session=...
```

真实故障重建预期日志：

```text
W (...) sdio_bridge: SDIO reset reason=..., new session=...; explicit start required
```

## 配套更新

**需要同时更新 ESP32 固件和 Zynq 应用 ELF。无需更换 bit/XSA、FSBL，不自动覆盖 BOOT.BIN。**

内部 SDIO 协议升级为 **v4**，使用空闲共享寄存器 56/57 传递 STOP_REQ/STOP_ACK，状态位 `0x40` 为 STOPPING。
旧新固件混用会因协议不匹配拒绝上传。TCP/UDP 图像包头、线描述符、五档动态帧长和原始 PSRAM 自测协议不变。
PC 显示与测速命令不变，重新启动 Python 工具以载入新代码；更新 ESP32 后强制刷新网页。

```powershell
# 在 esp32c61 目录，使用当前默认 combined 构建烧录；端口请自行确认
.\build.ps1 -Action flash -Port COM3
python .\pc_tools\bmode_web_viewer.py --device-ip 192.168.3.27
python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 60 --log .\logs\stop_fixed_60s.jsonl
```

分别运行显示或测速工具，不要同时占用 50001 端口。以上命令不负责启动扫描或开启高压。

## 离线验证与板级验收

- ESP32 真实后台任务测试：260 次开始/停止、停止序号回绕、重复停止、晚到 BEGIN/DMA、
  最后短窗口、回调先/后返回、主动取消、主机确认/回调超时，以及真实故障只重建一次。
- 将真实 PS bridge 与真实 ESP32 bridge 连接到严格模拟 SDIO 对端，覆盖五档长度和 20 轮停止竞态。
  STOP_ACK 写出时断言本地主机已空闲且 DDR 租约已释放；正常停止驱动重建与错误计数均不增长。
- PC 测试验证接收端等主机/回调及两个槽位归还后才关闭，停止中的设备不可被新任务接管，网页禁止提前启动。
- 回归现有冷启动恢复、USB/SDIO 宏、UART/温度/电源控制、TCP/UDP、动态帧长及图像格式。
- 停流核心覆盖目标为行 >95%、分支 >90%；完整桥接模块的历史防御性分支覆盖另行记录，不冒充全部达到目标。

本轮仅离线实现、测试和构建，**未连接、烧录或运行板卡**。板测时先确认两端均为 v4，
比较停止前后 `reset_count/errors`，确认 `host_sdio=true`、`stopping=false`、`free_slots=2`；
重复开始/停止并检查图像完整性、有效吞吐和堆低水位。真实断网、接收端退出和两端复位恢复应另行验收。
历史 `frame=8798` 的持续 BEGIN 回执等待不能仅凭停止修复宣称已解决，需独立板测跟踪。

## 本次构建结果

- Zynq 当前 Vitis 应用及 USB/SDIO 两种宏模式构建通过，链接检查确认应用与 DDR 三槽位区域无重叠。
- ESP-IDF 6.1 默认、combined 配置构建通过，应用分区剩余约 35%。
- Python 回归共 246 项：245 项通过，1 项因历史 TXT 样本缺失跳过；网页测试 43 项、图像页测试 5 项通过。
- 新增停流核心行覆盖 100%、分支覆盖 93.90%；整个桥接模块行覆盖 99.05%、分支覆盖 84.35%，后者未达到 90% 分支目标。
- 构建与测试日志位于 `esp32c61/logs/sdio_stop_*_20261028.log`，产物清单位于 `build/output/sdio_stop_20261028/manifest.json`。

配套产物（绝对路径）：

| 用途 | 文件 | SHA-256 |
|---|---|---|
| 当前 Vitis 应用 | `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/build/B_mode_focus_imaging.elf` | `B0D02118B104CF2A29FBB288CFC8D5BC3AEAB79B953F69556AA6878A3415CD4C` |
| 标准输出应用（同上） | `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/B_mode_focus_imaging.elf` | `B0D02118B104CF2A29FBB288CFC8D5BC3AEAB79B953F69556AA6878A3415CD4C` |
| combined ESP32 应用 | `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/esp32c61/build-throughput-fast-socket-exp-combined/ultrasound_psram_udp.bin` | `2E66FCE8BAA2009B1765F151BF6C8E686ADF0C0FD1A6B8CEB9756D94AA8A85C7` |
| 默认配置 ESP32 应用 | `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/esp32c61/build/ultrasound_psram_udp.bin` | `9B27415D551D448B00568A63AF015D443BCE8E110987BC678D227365ACB307D9` |

USB/SDIO 两种宏模式 ELF 另存于 `build/output/sdio_stop_20261028/`。原构建产物备份位于 `build/backups/sdio_stop_20261028/`。
没有生成或覆盖 BOOT.BIN，没有修改 bit/XSA 或 FSBL；以上均为离线结果。
