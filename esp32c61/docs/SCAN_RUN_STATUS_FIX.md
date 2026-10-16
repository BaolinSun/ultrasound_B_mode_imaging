# 扫描启动后 run 始终为 0：定位与修复

日期：2026-10-16。修改前相关代码已保存为 Git 检查点 `8210e74`。

## 原因

`BEAM START` 已通过 HTTP/UART 到达 PS 并返回 `@ACK ... BEAM_START`。
扫描请求写地址为 `0x4001152C`，SPI 启动请求写地址为 `0x40011528`。
当前 PL 的 `para_st.sv` 会处理对应的扫描时序字 11、10 的写入，但读数据/读有效
选择逻辑没有包含扫描时序区。PS 原先用 `XBram_ReadReg(...1152C...) & 1` 报告 run，
板上读到 0；这不是有效扫描状态。不能据此断定扫描没有启动。

## 修改

- 规范 PS 源及当前 Vitis 源同步修改。`Beamform_SetControl()` 在下发写操作后记录
  PS 请求，`STATUS` 使用统一 getter，不再读取上述写入专用地址。
- 增加 `run_src=ps spi=<0/1>`，明确 run/spi 是 PS 请求缓存，非硬件活动或排空证明。
  PL 单独复位、JTAG 直接写寄存器不会更新此缓存。
- TX7332 读取的忙碌保护也改用统一请求状态，避免 run 始终误读 0 时放行读操作。
  该保护仍不证明停止后的在途硬件事务已经排空。
- 不改变 BEAM START/STOP 的物理控制时序、不自动开启高压、不修改 PL 或图像协议。
  不需要新 bit、XSA 或 ESP32 固件。

## 板上验证

用户授权后，仅使用 JTAG 加载新 SDIO PS ELF，保留板上 bit。先执行 HV DISABLE，
测试中不执行 HV ENABLE，最后再次执行 HV DISABLE：

```text
开始：@ACK 0 BEAM_START
状态：@STATUS v=1 cfg=0 owner=0 seq=0 run=1 hv=0 run_src=ps spi=1
停止：@ACK 0 HV_DISABLE
状态：@STATUS v=1 cfg=0 owner=0 seq=0 run=0 hv=0 run_src=ps spi=0
```

为避免只验证软件缓存，通过 JTAG 被动读取整个 1024000 字节成像 DDR 环
（`0x20000000` 起，共 256000 个 32 位字），连续计算三次 CRC：

| 条件 | 三次 CRC32 |
| --- | --- |
| 扫描请求开启、高压关闭 | `d531593f`、`eb5bcac2`、`57e7bbf1` |
| 停止并等待后、高压关闭 | `2dd69a22`、`2dd69a22`、`2dd69a22` |

这支持扫描控制能够启停 DDR 更新，而非仅把网页数字改为 1。这里只验证数字控制及内存更新，
不证明发射电压、图像质量、整帧一致性或无覆盖；并发写入时 CRC 本身不是帧校验。
此前仅抽查 DDR 起始 64 字节未观察到变化，不代表整个环没有更新。现有 bit 未包含可用 ILA，
未验证内部逐次物理收发信号。

## 软件验证与产物

- 新增实际 `bram.c` 的 MMIO 自检查，覆盖写地址/顺序、SPI 低高沿、暂停后恢复、
  停机、无高压隐式开启、不读写入专用地址、TX 忙碌拒绝及旧 START/STOP 兼容。
- PS UART 实际解析测试覆盖 BEAM START/STATUS/STOP；原生 C 回归通过。
- Vitis 2026.1 SDIO 与 USB 宏模式构建通过；text/data/bss 分别为
  `179835/140604/163152`、`175035/140604/162192` B。
- Python 回归 129 项中 128 项通过，仍有此前已知规范/管理 BD 文本比较失败；
  网页 11 项通过。未修改 BD 来掩盖失败。
- ELF、绝对路径与 SHA256 见 [构建清单](../logs/scan-run-build-manifest.json)。
  正常 Vitis 下载使用 `workspace/B_mode_focus_imaging/build/B_mode_focus_imaging.elf`。

**本次只下载 RAM 中运行的 PS 应用，未覆盖 BOOT.BIN。重新上电会重新加载原启动镜像，
应通过 Vitis 重新下载修复后的 ELF；不要把旧 BOOT 当作已包含修复。**
结束状态为 `run=0 hv=0`。未进行高压发射或超声成像质量验收。

返回 [命令说明](WIFI_COMMANDS.md) / [文档索引](README.md)。
