# Vitis 2026.1 当前 IDE 工作区 UART 修复

日期：2026-10-12。修复前已将现有 `UserConfig.cmake` 与 `ps7_init.tcl` 改动提交为 `14c6a95`；未覆盖用户源文件、未删除/重新生成整个工作区、未下载或运行设备。

## 平时在 Vitis 中的操作

继续打开原工作区：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace`。

1. 选择应用 `B_mode_focus_imaging`，执行 **Clean → Build**。
2. 使用原硬件 Download/Run 配置下载应用，无需切换到临时构建工作区。
3. ESP32/monitor 先启动，再启动或复位 Zynq，观察 `[ZYNQ] @UART_LOG ...`。本次未代替用户执行 Download 或板测。

IDE 显示旧 BSP 属性时可重开工作区，让它重新加载磁盘配置。正常应用 Clean/Build 不需要先运行额外脚本。

## 修复内容

- `uart_log.c` 用具名字段初始化真实 SDK 的四字段 `XUartPsFormat`：BaudRate=115200、DataBits=`XUARTPS_FORMAT_8_BITS`（编码 0）、Parity=`XUARTPS_FORMAT_NO_PARITY`（编码 4）、StopBits=`XUARTPS_FORMAT_1_STOP_BIT`（编码 0）。先前的三字段初始化会把波特率填成 8、数据位编码填成 4；即使链接通过，也会在第一条日志前退出。此缺陷已修正。
- 应用 `outbyte.c` 定义 `__wrap_outbyte()`，`UserConfig.cmake` 增加 `-Wl,--wrap=outbyte`。BSP 中的强符号 outbyte 保持不变，不会与应用重复定义。规范源、当前 IDE 源和新工程生成脚本均同步；未使用允许所有重复符号的宽松链接选项。
- 当前 BSP stdout 为 UART0 `0xe0000000`，stdin 保持 UART1 `0xe0001000`。重新构建实际 BSP，并将其 include/lib 同步到当前平台 export。
- UART1 参数处理、USB/SDIO 宏选择、Wi-Fi/SDIO/UDP 功能未改变。最终应用仍为默认 SDIO 模式，USB 验证 ELF 单独保存。
- 修正测试桩四字段布局与真实 SDK 格式编码；新增直接对实际 BSP 头文件核验的测试，以及强符号 BSP 与应用包装共同链接的自检查，避免再次误报 UART 自检查通过。

应用字符包装不依赖 BSP stdout 元数据或 weak 补丁，因此 BSP 重建后即使 SDK 恢复自己的强符号，也不会引起重复定义或把应用日志切回 UART1。Vitis 2026.1 SDK 的独立 stdout 元数据生成缺陷可能在完整 BSP 再生成时重现；如需恢复元数据，执行工程根目录的 `.\build\scripts\repair_vitis_workspace.ps1`。该工具幂等修正生成源，只访问当前 BSP/应用文件并使用 CMake/empyro 构建后端，不依赖独占 IDE 工作区锁。运行期间不要同时触发 GUI Build；后续普通 Build 无需额外工具。

## 实际编译验证

验证的是当前 `workspace`，不是另一个临时平台：

- BSP 重新配置/构建/安装成功；export 的 stdout/stdin 正确。
- 使用与 Vitis IDE 相同的 `empyro.bat build_app -s ...\workspace\B_mode_focus_imaging\src -b ...\workspace\B_mode_focus_imaging\build`，执行 Clean 后全量 Build，再执行增量 Build，均退出 0。
- USB 宏模式 Build 通过，无 SDIO test_frame；恢复 SDIO 后再次 Build 通过，DDR 测试帧仍为 `0x151940..0x211d40`（787456 B、64 B 对齐）。
- 当前 BSP `libxil.a` 的 outbyte 仍为强符号 `T`，应用中 SDK outbyte 和 `__wrap_outbyte` 可以共存。
- ARM 反汇编核实包装函数使用 UART0 地址；最终 ELF 有 19 处调用 `__wrap_outbyte`，0 处调用 SDK 的默认 outbyte。包括标准 printf 的 `_write` 路径；`main` 仍在第一条打印前初始化 UART0。
- 原链接重复定义错误与 UART StopBits 初始化警告均消失。现有未使用变量、返回值等旧警告未在本次扩大范围处理，不宣称工程所有警告已清零。
- C 自检查、72 项 Python 与 7 项网页测试全部通过。UART 初始化行/分支覆盖均为 100%；这是软件验证，不能代替 UART/SDIO 板级运行结果。
- 当前工作区检查脚本通过：源一致性、UART0/UART1/SD0/SD1 映射、BSP stdout/stdin、包装链接选项、DDR 范围、BOOT 顺序与 bit 哈希。

验证日志（绝对目录）：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\reports`：

- `vitis-workspace-uart-repair.log`：BSP、当前 IDE 应用 Clean Build 与增量 Build。
- `vitis-ide-usb-uart-build.log`：USB 模式。
- `vitis-ide-sdio-uart-build.log`：最终 SDIO 模式。

回归日志：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\tests\output\vitis-ide-uart-tests.log`。

首次使用 Vitis 服务端 API 修复因 IDE 占用工作区锁被拒绝，未强制解除锁或关闭 IDE；最终改用同一个构建后端完成修复。失败尝试保留在 `vitis-workspace-uart-repair-lock-attempt.log`，不作为成功记录。

## IDE Download 的实际文件

现有 launch 配置未改写，仍指向以下绝对路径：

- ELF：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf`
- bit：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\_ide\bitstream\ultrasound_B_mode_imaging.bit`
- PS 初始化：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\_ide\psinit\ps7_init.tcl`

IDE bit 与 `build/output/ultrasound_B_mode_imaging.bit` 哈希一致；IDE PS 初始化与当前平台两份 `hw/sdt/ps7_init.tcl` 哈希一致（`96EE9A11731A9D029150322D95F08B23236FBAC9701527B992DC5F4C60568109`）。当前平台 XSA 与 canonical XSA 哈希一致（`CE6C1D71E7979169A9B5468D16B4B73CD61BB0EC7C5D8F226988107E77C8AAFC`）。

PS 初始化文件及 `compress.c`、`pus_oper.c` 修复前后哈希不变，未覆盖已有修改。

## 更新后的 BOOT.BIN

未改 RTL/bit/XSA，不需要重新实现硬件。使用当前 IDE 的应用 ELF、当前平台 FSBL 和既有匹配 bit 重新打包；最终仍为 SDIO 模式。输入顺序及 SHA256：

```text
A71159C76FA6ACBBF5EBC9EDB4D58AABE21B3F4A1A81E67C98C69D4AED0C74C4  D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\zynq_fsbl.elf
29B2A863B48FD3C284AA89CD69B90D42372C7AD9E0A85CE74274FADAE5C3E939  D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.bit
DAC8553A32FD15D0E77A15AF26127145CC366B30E5B499665D0A2AB098F05542  D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging.elf
F4CE9ACE1F624E87419BF24DDC404ADB0465A7CE3B070A3B61F87307042E8A0B  D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\BOOT.BIN
EAE65B74378F38E3479AD2E84153B031EBAABA80747C9AE25B826FA2A7557D16  D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging_USB_verify.elf
```

当前 IDE ELF 与上述输出应用 ELF 哈希相同；输入清单为 `build/output/boot_inputs.sha256`。本次只更新 Zynq 软件，ESP32 固件无需因这次修复重新烧录。

前次记录 `UART_BRIDGE_VERIFICATION.md` 已标注缺陷及历史性质，不能继续把其旧产物当作 UART 已验证版本。既有成像 RTL CDC/约束风险没有改变、没有被本次编译修复关闭；实际板级运行、连续稳定性仍待用户验证。
