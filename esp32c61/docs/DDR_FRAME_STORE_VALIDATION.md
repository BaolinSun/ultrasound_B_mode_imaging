# DDR 三缓冲与动态帧长验证记录（2026-10-19）

## 范围与结论边界

工作目录为 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging`，
分支 `hardware/ubmi-pcb-v2`，修改前代码基线 `562222ad2f195d1bf527411722dc0f1fffd0ca3d`。
修改前已有文档编辑已保留；没有修改 F 盘工程或 `ultrasound_dev_kit`。
本次没有下载、烧录、启动扫描、开启高压，也没有重新打包或覆盖 BOOT.BIN。

源码和使用说明见 [DDR_FRAME_STORE.md](DDR_FRAME_STORE.md)。
必须配套使用 UBF1 bit、PS 帧租约代码和 SDIO v3 ESP32 固件；旧版不兼容时拒绝上传。
网络 TCP/UDP 协议仍为 v1，原始 PSRAM UDP 自测不变。

## 已完成的软件与仿真检查

- 真实 XPM 异步 FIFO 的 RTL self-checking testbench 通过：五档帧长、逐字一致、
  最后 1024 字节短突发、随机 AXI AW/W 背压、VALID/data 保持、4 KiB 边界、
  租约排他与迟到释放、最新帧淘汰、停扫保留、缺行/长度错误、AXI 错误、
  FIFO 溢出排空与双域清理、截断帧超时及复位重新握手。
- C 自检查通过：实际 PS MMIO 适配、三槽地址与动态长度、租约/超时、
  实际 SDIO 主机/ESP32 接收任务五档末窗口及 CRC/停止/复位/缓冲所有权。
- 完整 Python 回归：166 项，165 项通过、1 项因历史采集样本不存在而跳过；无失败。
- 网页 JavaScript：原设备页 12 项、PC 图像页 5 项通过。
- 图像测试覆盖完整 64 行、缺行/重复/乱序/混合配置拒绝、五档深度/TGC 映射、
  会话与配置切换清缓存；d=0 灰度与只读参考计算比较，误差不超过 1 灰阶。
- 原始 UDP、TCP/带头 UDP、Wi-Fi 命令、温度和 UART 桥的主机回归通过。
  这些是软件模拟测试，不是实际板级结果。
- 没有收集 RTL 行/分支/FSM 覆盖率或门级仿真数据，不宣称达到覆盖率目标。

日志：`esp32c61/logs/frame_store_rtl_test.log`、`frame_store_tests.log`。
RTL 入口为 `build/scripts/test_frame_store.ps1`；软件入口为 `esp32c61/tests/run_tests.ps1`。

## 内存与编译检查

- 应用链接 DDR 区间限制为 `[0x00100000, 0x1C000000)`，堆栈同处该区域。
  既有 USB 缓冲 `0x1C000000` 与三帧槽 `0x20000000～0x202FFFFF` 均不参与自动分配。
- `build/scripts/build_frame_store_apps.ps1` 构建 USB/SDIO 两种宏模式，生成 `.map`，
  并调用 `check_frame_store_elf.ps1` 检查实际 ELF 的全部 ALLOC 段，不只检查宏常量。
- ESP-IDF 使用本机 `D:\Program Files\esp\v6.1\esp-idf`、目标 ESP32-C61。
  TCP/UDP × default/throughput 四组构建通过；4 MiB Flash 分区配置保持不变。
  各应用约 952～954 KB，应用分区剩余约 38%。
- 性能 TCP 配置静态 DIRAM 占用 135553 B（52.82%），剩余 121071 B；
  这是链接期统计，不包含运行时任务、Wi-Fi/TCP 动态分配或堆低水位。
  仍需板测确认实际最低空闲堆，不能将静态余量当作运行时保证。

## 硬件约束与独立风险

输入数据及行边界通过 XPM async FIFO；请求、确认、溢出和忙状态通过两级同步器。
输入时钟域复位采用异步置位、两级同步释放；仅这两个复位同步器 CLR 引脚取消
无意义的异步相位恢复检查，级间 D 路径不切断，也不豁免普通功能寄存器的复位路径。
新 Gray 指针约束不得被整时钟组 false path 覆盖。旧 AFE/系统域例外改为排除新模块的
旧端点局部例外，并保留实际 launch/capture 时钟条件，避免误切双时钟 RAM 同域路径。
时钟组与 max-delay 的优先级依据 [AMD UG903](https://docs.amd.com/r/en-US/ug903-vivado-using-constraints/About-Clock-Groups)。
旧端点集合较大，工具可能报告约束集合处理性能警告；不得据此宣称旧 CDC 电路可靠。

修改前基线 CDC 报告已有 Critical：CDC-1=35、CDC-4=4、CDC-10=6、CDC-13=11；
这些问题没有作为本次帧缓存功能的一部分整体修复。AFE DCLK 输入周期仍为原有 2.5 ns，
部分旧输入延时未完整约束；温度相关时钟故障没有解决。全工程不能标记为 CDC clean
或已经完成真实 I/O 时序签核。

## 板测尚未执行

尚不能宣称持续采集时覆盖 CRC 错误为零、真实成像正确、实际吞吐提升或 48 小时稳定。
待另行确认设备后，应按使用说明依次验证五档帧长、停扫单帧重读、连续只读新帧、
慢网络最新帧策略、复位/取消恢复，并分别统计 PL 发布、SDIO 接收、PC CRC 有效吞吐、
显示帧率和源端丢弃原因。不要使用固定测试图案验证替代真实采集验收。

## 后续修复：BD 冷加载丢失 para_* 端口（2026-10-19）

原先将外部 BRAM `para` 接口只保留 CLK/RST/DOUT，同时单独导出
`para_addr/en/we/din`。Vivado 2026.1 保存后重载会丢失这四个独立端口，
出现 BD 41-2162；此前构建脚本临时补回端口，掩盖了保存重载缺陷。

现在去掉不完整的外部 BRAM 接口，将七根信号全部显式导出，名字、位宽和方向不变。
控制器与帧缓存仍共用相同四根控制网络，读回、时钟和复位保留原有拓扑。
规范 BD、Vivado 管理 BD、包装 HDL 和正常生成入口均已同步。
`configure_para_ports.tcl` 增加保存后关闭/重开断言；重开后只能检查，不能再次补线。
新工程生成脚本中指向旧 DDR 写入器的 CDC 断言同时换成实际帧缓存同步器检查。

验证：

- Vivado 保存→关闭→重开通过；独立只读进程再次打开通过，四条 BD 41-2162 不再出现。
- 正常帧缓存集成入口重复执行后，再次独立冷加载通过；不依赖只运行一次的修复脚本。
- 七个端口方向、位宽、控制器连接和帧缓存分支均检查通过。
- 硬件脚本回归 26 项通过，含缺端口/错位宽/错连线检测。
- 结构连接图按模块作用域对照：11 个模块、932 项模块端口声明、23 个实例一致。
  这不是通用 RTL 形式等价证明；工具拒绝行为 RTL，且不将简单导线别名误判为功能变化。
- 全部层次 IP 参数及地址映射不变；包装 HDL 仅生成日期变化。
- 正式 bit、XSA、BOOT.BIN 哈希与本记录清单一致，没有重新生成这些产物或操作板卡。
  原有 CDC/I/O/AFE 接口告警不视为本次已解决。

备份：`build/verification/para_port_fix_checkpoint_20261019_153313888/`，
含修改前 BD、脚本、生成 HDL 和全部已跟踪未提交改动补丁。
记录：`build/verification/repair_para_ports_v2.console.log`、
`verify_para_ports_final.console.log`、`para_ports_integrated.console.log`、`para_ports_script_tests.log`。
冷加载复查入口为 `build/scripts/verify_para_ports.tcl`，该脚本不保存、不补线、不生成 HDL。

## 最终实现与产物

Vivado 2026.1 最终实现（最终约束验证 13:55）：

- WNS **+0.797 ns**、TNS 0；WHS **+0.012 ns**、THS 0；脉宽余量 +0.264 ns。
  满足当前已声明约束及 WNS >0.5 ns 目标，不等于未约束 I/O 已完成签核。
- 全设计 LUT 30880/78600（39.29%）、FF 62430/157200（39.71%）、
  BRAM 188.5/265（71.13%）、DSP 293/400（73.25%）。
- 相对保存的基线净增加 1489 LUT、953 FF、1 BRAM36 等效块，DSP 不变；
  新帧缓存本身使用 1608 LUT、1146 FF、5 BRAM36、0 DSP。
- 新帧缓存及新增处理忙同步器的 CDC 条目均为 Info（CDC-3/9），没有 Critical。
  模块最差 setup +3.925 ns、hold +0.095 ns；组合逻辑约 5 级 LUT 加专用进位链，
  仍应在后续功能扩展时注意计数/比较路径，不能仅以 LUT 级数估计专用进位链延时。
  XPM 双向 Gray 指针 max-delay（10 ns / 35 ns，按当前设计约束推导）与 bus-skew
  检查通过；没有 TIMING-24 覆盖警告。物理时钟准确性仍以板级测量为准。
- 全局 CDC Critical 仍为 CDC-1=32、CDC-4=4、CDC-10=7、CDC-13=11。
  相对基线新增可见的一个 CDC-10 位于 `ila_bf_i` 加密内部；没有修改其探针或
  内部 RTL，没有豁免该告警，也不宣称这项风险已解决。
- 精确 CLR 例外消除了 TIMING-6/7；仍有旧 I/O 延时、同步器/复位和约束处理性能告警。
  不能将“当前约束 timing met”解释成全设计 CDC/真实 I/O 全部合格。

报告位于 `build/reports/sdio_{timing,cdc,bus_skew,methodology,utilization}.rpt`，
模块报告为 `frame_store_{resources,setup,hold,all_cdc,clock_interaction,cross_sys_afe,cross_afe_sys}.rpt`。
保存的修改前报告位于 `build/reports/frame_store_baseline/`。

硬件重建入口 `build/scripts/build_frame_store_hardware.ps1`；综合预钩子只限制本工程
综合进程数，避免本机旧扩展在并行子进程中触发写入 C 的错误，不修改共享用户配置。
最终只读模块检查为 `report_frame_store.tcl`。本次仅补充复位 CLR 例外的再验证与导出
另有 `finalize_frame_store.tcl`，没有重新布线或改变逻辑，后续完整重建已直接包含此例外。

配套平台及 SHA256 清单如下；未经过此清单匹配的旧输出不能作为本次交付。

最终平台来自本节 XSA，独立构建于
`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\frame_store_platform`。
实际 BSP 导出目录为该目录下 `ubmi_p\export\ubmi_p\sw\standalone_ps7_cortexa9_0`，
平台入口为 `ubmi_p\export\ubmi_p\ubmi_p.xpfm`。USB/SDIO 均用此 BSP 重新链接成功。
SDIO ELF 的 text/data/bss 为 184523/140604/163248 B，全部 ALLOC 段最高末地址
`0x00178590`；USB ELF 同样通过独立 ALLOC 检查。
两份 `.map` 位于 `build/vitis_frame_store_{usb,sdio}/B_mode_focus_imaging.map`。

当前 Vitis 应用源码已同步，并在原 IDE 工程构建目录实编译通过；没有整目录覆盖用户
平台设置。若 IDE 后续自动下载平台所带 bit，应先更新硬件规范为下面的新 XSA，
或引用上述独立导出的平台。只更新 ELF 而继续下载旧 bit 不受支持。
旧输出 ELF/FSBL 已备份于 `build/frame_store_previous_artifacts/`。

### Zynq / PL 交付（绝对路径与 SHA256）

- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.bit`
  — `0BAB4402607BA0BD627C5B13764A82DC36CB3A19E47E4FB09FEBD81C12ED7544`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.xsa`
  — `84B914125AEE6D1184C8C9ADB98904CCAD0EEE42E26DFCF528D4E898B165DF4D`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.ltx`
  — `14C3551092C8CE1BC4DE3249568A9235D6020986ED08B74237C43DB9331FB559`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging.elf`（SDIO）
  — `DEAE2C3E3C6387887CC79F67C64FF7FBDBC6A2126BCE7CF40645BC0F120FA538`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging_USB_frame_store.elf`（仅 USB 配置，不输出 USB 图像）
  — `504C1D0DE94D54CE3BA7FCD25D4E2F01FC669D8B4B49D1BB064D8CA77DE75EB1`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\zynq_fsbl.elf`
  — `456D457AA63343E5AEF958BA83AD7AD689D540FBB2F5D7E9FE6C448E6564AB1A`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\frame_store_vitis_platform.zip`（BSP、FSBL、XPFM 与硬件规范）
  — `23B5739BC1587F615A28F1AD14D12453917E4FFE15B279B5887A83913223639F`

已逐字节哈希确认：XSA 内嵌 bit 与上述 bit 相同；导出平台的硬件 XSA 与上述 XSA 相同。
`ila_bf` 原五个探针已通过构建检查，未修改探针定义。

### ESP32 应用（绝对路径与 SHA256）

- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-default-tcp-baseline-socket\ultrasound_psram_udp.bin`
  — `AFC1DD7D6290FB27911D3ADAC947F2B38BE79307F2CDE9C0C291CC37093E280A`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-tcp-fast-socket\ultrasound_psram_udp.bin`（性能 TCP）
  — `01C4E9FE36F2416DE87E7F92A0E80CD6AC22A4695635F71921120816AB595472`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-default-udp-baseline-socket\ultrasound_psram_udp.bin`
  — `EA9CFAE461CD01C0B18368511B95F7F607041E1E901B7A5260E9313E37E1FCF4`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-udp-fast-socket\ultrasound_psram_udp.bin`
  — `45BCAC5BAC48CFCB48F72E28672A0094652C51327AC4AF134CDC678FD9DA80F4`

性能 TCP 配套引导与分区文件：

- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-tcp-fast-socket\bootloader\bootloader.bin`
  — `25FD217AF5B8B27BA4DC3560AC5E9BDE576EB0323CE4C6F2366FFD4711B4B13F`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-tcp-fast-socket\partition_table\partition-table.bin`
  — `5295E8C42D6FB5F95E4E8A185932C6191D27CFC5078322475347E40E71AF815A`

使用每个构建目录自身的 `flash_args`，不要交叉组合引导、分区及应用。
本次没有执行 flash；这些 SHA256 只标识离线构建产物，不代表已安装到设备。

原 `build/output/BOOT.BIN` 保持不变，SHA256：
`30BE9B06D8E058C6D26B249EA960DC460CB467EE3EF7D57B3812F2DDDB256360`。
