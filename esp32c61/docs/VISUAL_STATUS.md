# 网页状态与控制布局：2026-10-28

## 状态面板

- Zynq 控制页用六个独立状态块显示服务、任务、配置事务、PS 通信、扫描请求和高压使能。普通视图不再展示整行 `@STATUS`，原始回执在折叠的“通信诊断”和“命令执行记录”中保留。
- 高压控制与扫描控制分组；查询按钮在状态区旁。开启高压仍需确认，关闭高压和安全停止沿用优先级关断命令，不被普通任务忙碌阻塞。
- TX7332 与 AFE5832 合并为一个电源管理面板，桌面并排、窄屏纵向排列；保留独立待机、唤醒和查询按钮。
- CPU0 用模式标签、WFI 标志和累计次数/时间统计卡显示；CPU1 用控制状态、复位/停钟标签显示，寄存器折叠保存。未固定禁用不推断 CPU1 正在执行任务；WFI 可用不等于 CPU0 瞬时睡眠。
- 绿色表示就绪/已确认，蓝色表示活动或已待机，黄色表示过渡/高压使能/需关注，红色表示故障，灰色表示未知或旧快照。每个状态同时有文字，避免仅靠颜色区分。
- PS 和芯片快照超过 30 秒明确标为缓存，显示更新时间并提示手动查询。不会新增定时 UART 查询。CPU 状态继续遵守已有过期/断连规则；离线时清除旧的有效状态样式。
- 数字恢复检查不代表模拟图像质量已验证。AFE 唤醒后的图像差异问题仍未解决，见 [体模切换测试](AFE_TX_IMAGE_CYCLE_20261028.md)。

## 停扫语义

删除“暂停扫描（保留高压）”按钮。网页的“安全停止”和“关闭高压”均下发 `HV DISABLE`，保持原高优先级关断行为。

为避免高级命令或其他入口保留旧语义，本次也同步修改规范 PS 源与当前 Vitis 源的 `BEAM STOP`：先关闭高压，再调用 `Beamform_Halt()` 清除扫描和 TX 配置启动请求；高压使能读回未关闭时返回 `HV_DISABLE_FAILED`，不回报成功。USB、UART1 与 Wi-Fi 的命令入口共用该逻辑。内部 `Beamform_Pause()` 不作全局改写。

这些是控制请求和 GPIO 读回语义，不是测量实际高压或 PL 全部事务排空的硬件证明。没有改变 AFE/TX 状态机、成像算法、帧协议、CPU 待机策略或 PL。

## 更新要求与产物

需要更新 **ESP32 固件与 Zynq 应用 ELF**。ESP32 更新后强制刷新网页。bit/XSA/FSBL 不变，本次未打包或覆盖 BOOT.BIN，未连接或操作板卡。

当前 IDE 应用：

`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/build/B_mode_focus_imaging.elf`

常用输出 `build/output/B_mode_focus_imaging.elf` 已同步，SHA256 为：

`acf5eabf85cab2c264ce26a8da5fbda776baad8ebf98cbc5a5a9bc953288ffa4`

日常 combined 固件仍由 `esp32c61/build.ps1` 选择：

`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/esp32c61/build-throughput-fast-socket-exp-combined/ultrasound_psram_udp.bin`

配套副本目录为 `build/output/visual_status_20261028/`；完整绝对路径与哈希在 `esp32c61/logs/visual_status_manifest_20261028.json`。旧常用应用备份在 `build/backups/visual_status_2c127b0/`，源码回退基线为 `2c127b0`。

## 离线验证

- 网页实际脚本的 DOM/API 测试 39 项通过，包括状态颜色/文字、无效值、旧缓存、断连、CPU 模式、待机互锁、停止按钮命令和错误反馈。图像页原有 5 项通过。
- PS 实际解析测试验证 Wi-Fi/USB 的停扫先关闭高压、高压读回失败返回 NACK；其余 UART、USB、SDIO、温度、CPU 及 TCP/UDP 回归通过。
- Python 回归 237 项中 236 通过，1 项历史 TXT 缺失跳过；均使用 conda `esp32c61` 离线环境。
- ESP-IDF 6.1 默认与 combined 构建、Vitis 2026.1 USB/SDIO 与当前 IDE 构建通过。新增网页/样式增加静态固件内容；PS 链接数据和 BSS 大小保持不变，已有 USB 等编译告警未宣称清零。
- HTML 面板结构闭合、109 个 ID 无重复。浏览器工具不允许本地文件预览，因此没有把桌面/手机实际浏览器渲染或板测列为已通过；响应式布局由 CSS 与交互测试核对，实际显示待更新固件后验收。
