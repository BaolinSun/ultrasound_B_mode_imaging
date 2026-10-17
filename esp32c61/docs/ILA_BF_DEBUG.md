# 原始 ila_bf 波束合成调试

## 本次范围

恢复 `heart_top.sv` 中现有 `ila_bf_i` 对应的真实 ILA IP。按用户要求，原 5 组探针及连接完全不变，不增加探针、同步器或探针流水级；采样深度 8192、输入流水级 0。`ila_tx`、`ila_timing` 仍为空壳，不在本次恢复范围。

Vivado 在存在真实 BF ILA 时可能保留其他两个空壳为黑盒。实现前脚本仅删除两个已确认无输出的空壳实例，防止 INBB-3 阻止实现；不会移除真实 BF ILA 或功能逻辑。

规范源、Vivado 管理源、当前工程与重建脚本同步维护。`ila_bf.xci` 升级到 Vivado 2026.1；构建检查真实核存在，避免只保留 RTL 名称却没有硬件 ILA。

## 探针（原样保留）

ILA 采样时钟是 `data_clk`（AFE 数据域）。

| 探针 | 位宽 | 原始连接 | 用途 |
|---|---:|---|---|
| probe0 | 17 | `{proc_en,proc_in[15:0]}` | bit16 为输入有效；低16位为首通道输入样本 |
| probe1 | 2 | `{rtc_rxen,proc_top_i.rtc_bfen}` | bit1 接收窗口、bit0 波束合成窗口 |
| probe2 | 17 | `{up_dataen,up_data}` | bit16 上传有效；低16位为成像处理后的行头或数据 |
| probe3 | 32 | `proc_top_i.rx_foc_delta_rddata` | 波束合成延迟相关 RAM 输出 |
| probe4 | 7 | `rtc_line` | 原有 0～127 索引；不要等同于 PC 的 0～63 逻辑图像行号 |

**既有 CDC 风险未修复：** `rtc_rxen`、`rtc_bfen`、`rtc_line` 来自系统扫描时钟域，直接被 `data_clk` 采样。单 bit 边沿存在亚稳态风险，多 bit 行号可能不相干；按要求不改变这些探针，不为其新增宽泛 false path。它们仅用于趋势观察，不能据此宣称跨域时序准确或 CDC clean。重点用本地域的 probe0/probe2 判断输入及处理输出活动。现有功能路径的 CDC 仍需独立整改。

Debug Hub 单独连接 `clk_100m`，与 ADC 数据时钟分离。依据 [AMD UG908 2026.1](https://docs.amd.com/r/en-US/ug908-vivado-programming-debugging/Debug-Cores-Clocking-Guidelines)，Hub 使用稳定、自由运行的系统时钟。**这不代替 ILA 自身的采样时钟：** AFE 未初始化或 `data_clk` 停止时，ILA 仍可能无法采集，并报告采样时钟错误。

## 构建与文件

在 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging` 根目录执行已有硬件构建入口：

```powershell
.\build\scripts\build_sdio.ps1 -Stage hardware
```

此命令生成 bit、LTX、XSA，不打包 BOOT.BIN。其他当前工程和干净重建入口也同步恢复 ILA。

成功构建后使用同一轮产物：

- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.bit`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.ltx`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.xsa`

构建过程不自动连接硬件，不烧录 ESP32，不下载 Zynq，不更新 BOOT.BIN，也不启动扫描或高压。旧产物备份在 `build/output/ila_bf_before_20261017_104010/`。BOOT.BIN 和 Vitis 平台中的旧 bit 不会因此自动替换；若随后由 Vitis 重新配置 FPGA，必须选择本次新 bit，否则可能再次加载不含 ILA 的旧版本。

## Hardware Manager 操作

以下是用户自行操作步骤，不表示已完成板测：

1. 保持高压关闭并停止扫描，在 Program Device 中选择上述 bit 和匹配的 LTX。
2. 运行匹配的 PS 应用完成 AFE/参数初始化；确认应用未重新加载旧 bit。
3. 刷新硬件，找到 `ila_bf_i`；先用 Run Trigger Immediate 观察 data_clk 下的数据。
4. 将 `probe2[16]`（上传有效）设为等于 1 的触发条件，或将 `probe0[16]` 设为等于 1；先 Arm，再通过现有入口发送 `BEAM START`。该命令本身不启高压；不要为数字链路调试发送 `HV ENABLE`。
   需要观察一条连续输出行时，建议触发位置设为 1024，给触发后的 6152 个数据字留出采样空间；若有效信号有间隙，8192 个采样时钟不一定覆盖整条线。
5. probe0 有有效输入但 probe2 没有输出时，继续定位波束合成/处理链；两者均无有效活动时，先查扫描窗口、TX 配置就绪及 ADC 时钟。必须结合实际波形，不以软件 `run=1` 或一次 CRC 代替硬件运行证据。
6. probe2 的有效数据中检查 `1C00 1800` 和相邻行头间距；这有助于区分处理输出已经异常，还是之后 FIFO/DDR 写入才发生丢字。

仅看输入采样或延迟 RAM 值不能证明完整波束合成正确。这里没有新增物理扫描活动反馈或 ADC 时钟计数寄存器；恢复 ILA 也不等于 DDR 不更新问题已解决。

## 验证

```powershell
python -m unittest discover -s .\build\scripts\tests -p test_bf_ila.py -v
```

自检查验证两份原始探针连接不变、无 BF 空壳、IP 配置、重建入口和 Hub 时钟。实现后额外检查真实 `ila_bf_i` 和各探针宽度，导出时序、资源、CDC 与 DRC 报告。原有功能 RTL 未修改，不将原有 CDC 风险标记为通过；板级采集及 48 小时稳定性未由软件检查替代。

### 2026-10-17 本次构建结果

- Vivado 2026.1 综合、实现及 bit 生成成功；实现网表不存在未解析的非原语黑盒，真实 ILA 的 5 个端口宽度验证通过。
- 两份 `heart_top.sv` 与修改前没有 Git 差异；升级前后可比较的 3110 个 ILA 用户参数值全部一致。
- 原探针集成自检查 7 项、Git 忽略规则回归 4 项通过。没有新增功能数据通路 RTL，也未执行板级采集。
- 最终 WNS **0.937 ns**、WHS **0.026 ns**，setup/hold 失败端点均为 0；脉宽余量 0.264 ns，总线偏斜最小余量 9.271 ns。
- 资源：LUT 29391/78600（37.39%）、FF 61477/157200（39.11%）、BRAM Tile 187.5/265（70.75%）、DSP 293/400（73.25%）。相比无 BF ILA 的上一轮，增加 1352 LUT、2680 FF、17 BRAM Tile，DSP 不变。
- DRC 无 Error；仍有 RAM 异步控制、DSP 流水级等告警，methodology 仍有缺失 I/O 延迟等告警，不是完整签核通过。
- CDC Critical 共 **56** 条：原功能路径仍为 38 条；另 18 条指向恢复的原始 ILA 异步探针（CDC-1 9 条、CDC-13 9 条，来自两个扫描标志和七位行索引）。不更改原探针，也不新增宽泛时序例外掩盖它们。**该结果不是 CDC clean。**

报告位于 `build/reports/sdio_timing.rpt`、`sdio_utilization.rpt`、`sdio_drc.rpt`、`sdio_cdc.rpt`、`sdio_bus_skew.rpt`、`sdio_methodology.rpt`；本轮实现日志为 `build/reports/ila_bf_impl.log`。

同一轮输出的 SHA256：

```text
ultrasound_B_mode_imaging.bit  b7afb84f3124bc4746493c5ab843c4501dcdb9391fae3ba846e27233539fb02f
ultrasound_B_mode_imaging.ltx  14c3551092c8ce1bc4de3249568a9235d6020986ed08b74237c43db9331fb559
ultrasound_B_mode_imaging.xsa  348e0cb247de5fd6b8a1910e1c4fe249200101bb4b3bb6ce0bf655c668fa4f00
```

已核对 XSA 内嵌 bit 与独立 bit 逐字节一致；LTX 仅含真实 `ila_bf_i`，端口宽度为 17/2/17/32/7，UUID 为 `492A0BD663405A3B8FAAC5070FB5E4DD`。

现有 `BOOT.BIN` **未更新**，SHA256 仍为 `30be9b06d8e058c6d26b249ea960dc460cb467ee3ef7d57b3812f2dddb256360`；不能将它当作包含本次 ILA 的新启动镜像。本次没有烧录、JTAG 下载、扫描或高压操作。
