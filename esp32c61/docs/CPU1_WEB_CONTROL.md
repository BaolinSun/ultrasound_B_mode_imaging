# 网页手动关闭 CPU1

## 当前行为

应用和工程本地 FSBL 在启动时仅观察 `A9_CPU_RST_CTRL`，不主动将 CPU1 复位或停钟。启动日志为：

```text
@CPU1_FSBL startup=observe disabled=<0或1> control=<实际寄存器>
@CPU1 startup=observe disabled=<0或1> control=<实际寄存器>
```

这不代表主动启动 CPU1。BootROM、调试器或旧启动镜像留下的状态保持不变；若本来已复位并停钟，新应用不会解除它。CPU1 仍不运行本工程的应用任务。

## 网页操作

1. 打开 ESP32 配置页，例如 `http://192.168.3.27/`，更新固件后强制刷新页面。
2. 在“Zynq CPU 状态”卡片点击“查询 CPU 状态”，等待有效快照。
3. 点击“关闭 CPU1”。网页提交 `CPU1 DISABLE`，收到成功回执后查询 `STATUS`，两次结果均确认才显示成功。
4. CPU1 复位和停钟位均成立时，关闭按钮禁用。关闭后不提供重新启动入口。

未收到状态、状态过期、控制忙碌或配置事务占用时不能关闭。AFE/TX 已待机时仍允许该独立操作，不自动唤醒它们，不调整 CPU0 WFI、不启动扫描、高压或图像上传。

失败显示错误或结果未知，不自动重发。可显式查询实际状态；寄存器只读查询不会重新操作 CPU1。

## 命令与回执

从 `esp32c61` 目录、conda `esp32c61` 环境执行：

```powershell
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 -c "CPU1 DISABLE"
python .\pc_tools\zynq_wifi_control.py --device-ip 192.168.3.27 --status
```

```text
@ACK <seq> CPU1_DISABLE cpu1_disabled=1
@NACK <seq> CPU1_<错误原因>
```

仅 Wi-Fi 封装命令可以执行此操作；USB/UART1 原始入口拒绝。配置事务进行中也拒绝，返回 `CPU1_OWNER_OR_CFG`。同一请求重复到达由现有协议返回缓存回执，不重复执行。新的显式关闭请求本身也可重复调用，已关闭时不先恢复 CPU1。

沿用既有 CPU0 专用、有限等待的读改写：先置 `A9_RST1`（bit1），确认后置 `A9_CLKSTOP1`（bit5），保留 CPU0 和其他控制位，并恢复原 SLCR 锁定状态。这是复位与停钟，不是独立电源切断。失败仍只告警，不增加扫描、高压或 WFI 准入条件。

## 需要更新的固件

- ESP32：更新应用以取得新按钮、命令白名单；分区表和设备热点凭据不变。
- Zynq：更新应用 ELF；SD 卡完整启动还必须使用本次重编的 FSBL。仅下载新应用不能抵消旧 FSBL 已执行的关闭动作。
- 不需要更新 bit/XSA。此次没有覆盖 `BOOT.BIN`；已有启动镜像仍含旧 FSBL/应用，不能直接当作新启动流程使用。

当前 Vitis 工程应用：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/build/B_mode_focus_imaging.elf`。

当前平台导出 FSBL：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/ubmi_p/export/ubmi_p/sw/boot/fsbl.elf`。

产物副本及哈希见 [本次构建记录](CPU1_WEB_BUILD.md)。已离线检查启动不写 CPU1 控制、手动关闭顺序、重复/失败、配置冲突、待机操作、状态查询和原有通信；板级电流和实际启动行为仍需用户更新后验收。

## 回退

本次修改前的相关源与旧固件备份在 `build/backups/cpu1_web_4e7d159/`。回退需恢复配套应用与 FSBL；仅切换 Git 不会恢复生成的 ELF。旧的固定关闭实现说明保留在 [CPU1 历史说明](CPU1_CLOCK_STOP.md)，当前行为以本文为准。
