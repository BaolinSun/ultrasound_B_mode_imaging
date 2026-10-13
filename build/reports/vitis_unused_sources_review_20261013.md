# Vitis 2026.1 未使用源码清理候选（2026-10-13）

## 当前状态

初始清单在分析阶段生成；用户随后确认：**保留 tmp/tmp.c 和 tmp/tmp.h，删除其余候选**。实施清单为每份源码 18 个文件，共 36 个路径。删除前保存快照，删除后验证 USB/SDIO 构建，不下载或运行硬件。下文为删除前的分析依据，最终结果另行记录。

当前应用定义：[vitis-comp.json](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/vitis-comp.json)，平台 ubmi_p，standalone_ps7_cortexa9_0。
当前工作区：[应用源码目录](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/main.c)。规范源：[build/src/ps](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/main.c)。

A 组 12 个文件是较明确的旧实现、占位或零外部引用模块；B 组 8 个文件当前未使用/空实现，但保留了将来可能启用的入口。若全部清理，工作区 20 个文件、规范源 20 个文件，共 40 个路径。可以只选择 A。

## 判定依据及边界

- 读取 main.c/.h、输出模式宏、UserConfig.cmake、CMakeLists.txt、工程生成脚本和相关模块的 include/函数调用。
- 对现有 35 个应用 .obj 运行 Vitis 2026.1 arm-none-eabi-nm，检查导出函数/变量在其他应用对象中的未定义引用；同时检索源码。不是只根据文件名或当前 SDIO 宏删除。
- 现有对象/ELF 是工程已有构建结果，本次未创建新的构建。静态零引用并非对所有未来宏/功能的保证；确认后仍必须重新构建 USB、SDIO 两种模式。
- outbyte.c 在应用对象扫描中没有普通引用，但链接器通过 --wrap=outbyte 使用它；最终 ELF 存在 __wrap_outbyte，**必须保留**。这类链接入口不能按零引用误删。
- 当前工程仍编译并链接部分未调用的函数（ELF 中存在 hulfman_encode/timer_init/wifi_mem_init）；“出现在 ELF”不等于运行中被调用。
- 压缩模块两份文件的原始字节哈希不同，但文本 diff 未见内容差异，需保留各自原文件作为备份，不能简单用一份覆盖另一份。

## 工作区删除候选（逐文件）

| 组别 | 文件（链接为绝对路径） | 理由及注意事项 |
|---|---|---|
| A | [data_transfer/axidma.c](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/data_transfer/axidma.c) | 旧 FreeRTOS/AXI DMA 实现，生成脚本明确排除；当前硬件无对应 AXI DMA，启用 USE_DMA 会编译报错。 |
| A | [data_transfer/axidma.h](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/data_transfer/axidma.h) | 同上；现有引用仅为旧代码中的注释。 |
| A | [data_process/compress.c](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/data_process/compress.c) | 导出 17 个符号，当前其余 34 个应用对象没有对它的外部引用；不参与当前原始帧 TCP/UDP 上传。 |
| A | [data_process/compress.h](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/data_process/compress.h) | 只供同模块及已排除的旧 AXI DMA 使用。 |
| A | [linux_wifi/linux_wifi.c](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/linux_wifi/linux_wifi.c) | 旧 DDR 共享内存 Linux Wi-Fi 路径，6 个导出符号无外部引用；主函数 wifi_mem_init 调用已注释。不是 ESP32 SDIO/Wi-Fi 实现。 |
| A | [linux_wifi/linux_wifi.h](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/linux_wifi/linux_wifi.h) | 主函数只有未使用的 include，删除时一并移除 include。 |
| A | [eeprom/eeprom.c](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/eeprom/eeprom.c) | 仅 include eeprom.h，没有函数/变量定义。 |
| A | [eeprom/eeprom.h](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/eeprom/eeprom.h) | 仅占位宏与 include，无其他应用文件使用。 |
| A | [TX7332/tx7332.c](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/TX7332/tx7332.c) | 整份文件为注释，编译对象没有导出符号。 |
| A | [TX7332/tx7332.h](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/TX7332/tx7332.h) | 整份文件为注释，保留的 include 不产生声明或定义。 |
| A | [TX7332/tx7332_spi.c](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/TX7332/tx7332_spi.c) | 整份文件为注释，编译对象没有导出符号。 |
| A | [TX7332/tx7332_spi.h](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/TX7332/tx7332_spi.h) | 整份文件为注释；实际 PL TX7332 配置在 bram 模块中，不能删除该活跃功能。 |
| B | [key/key.c](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/key/key.c) | 按键函数没有外部引用，主函数 key_init 已注释。删除后不再保留旧按键功能实现。 |
| B | [key/key.h](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/key/key.h) | 主函数只有 include；按键引脚宏只在本模块使用。 |
| B | [tmp/tmp.c](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/tmp/tmp.c) | temp_init/read_temp 没有外部引用，temp_init 为空实现。删除后不再保留该温度采集入口。 |
| B | [tmp/tmp.h](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/tmp/tmp.h) | 主函数只有 include；温度宏/接口未被其他活跃模块使用。 |
| B | [timer/timer.c](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/timer/timer.c) | 旧 TTC 延时模块的函数/变量没有外部引用；不要与 BSP xiltimer 或 sleep/usleep 混淆。 |
| B | [timer/timer.h](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/timer/timer.h) | 主函数只有 include；TmrCntrSetup 仅本模块使用。 |
| B | [data_transfer/data_recv.c](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/data_transfer/data_recv.c) | data_recv_init/data_recv 都为空实现；main 仍调用 data_recv_init，删除时必须同步移除该空调用。 |
| B | [data_transfer/data_recv.h](D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/src/data_transfer/data_recv.h) | 删除时移除 main 中对应 include 和旧注释调用；并不删除 PL→DDR 成像 RTL。 |

## 必须同步处理的规范源路径

以下文件不会在本次确认前删除。只删工作区而保留规范源，会在重新生成 Vitis 工程时再次导入：

- A：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/data_transfer/axidma.c`
- A：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/data_transfer/axidma.h`
- A：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/data_process/compress.c`
- A：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/data_process/compress.h`
- A：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/linux_wifi/linux_wifi.c`
- A：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/linux_wifi/linux_wifi.h`
- A：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/eeprom/eeprom.c`
- A：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/eeprom/eeprom.h`
- A：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/TX7332/tx7332.c`
- A：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/TX7332/tx7332.h`
- A：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/TX7332/tx7332_spi.c`
- A：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/TX7332/tx7332_spi.h`
- B：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/key/key.c`
- B：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/key/key.h`
- B：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/tmp/tmp.c`
- B：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/tmp/tmp.h`
- B：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/timer/timer.c`
- B：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/timer/timer.h`
- B：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/data_transfer/data_recv.c`
- B：`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/src/ps/data_transfer/data_recv.h`

## 明确保留

- main.c/main.h、imaging_config.h，以及 CMake/应用定义、链接脚本。
- data_transfer/sdio_bridge、sdio_host、sdio_cmd_engine、bridge_protocol、output_config：当前 SDIO 数据路径和编译期 USB/SDIO 模式。
- usb 目录：即使 SDIO 模式仍用于 USB 参数配置，并支持 USB 数据模式；包含 Chapter9、WinUSB、COMMS、Ringbuffer 等有效依赖。
- uart/uart.c/.h、uart_log.c/.h、outbyte.c：参数配置和 Zynq→ESP32 日志转发。
- bram.c/.h、bram_config.c、delay_profile.c、demod_coeffs.c、log_table.c：初始化参数和运行时配置仍有外部引用。TX7332 注释文件可清理不等于移除实际 TX7332 功能。
- AFE5832、CLOCK、GPIO、HV、iic、pus_operation、system_ctrl：启动及配置仍有有效引用。
- ubmi_p/BSP/FSBL 与 Xilinx 驱动：由平台生成和管理，不按应用暂时未调用直接删除。
- .cache、compile_commands.json、对象文件、ELF 等为生成物，不混入本次源码删除清单。

## 确认后的实施范围

1. 记录 Git 状态，保存本任务相关改动；针对确认的精确文件创建可恢复备份/版本快照，保留其他用户变化。
2. 删除确认的 A 或 A+B 两份源码；不删除整棵 data_transfer 或任意笼统通配路径。
3. 同步移除 main、usb、pus_operation 等文件中失效 include/旧注释引用；B 中 data_recv 删除时同时去除空初始化调用。
4. 更新工作区 UserConfig.cmake 的源文件和 include 目录，以及生成脚本相关说明/列表；不能只删除文件留下构建引用。
5. 以 Vitis 2026.1 分别构建 USB/SDIO 宏模式、检查链接/日志包装符号，运行相关软件自检查并报告结果。**确认前未证明清理后的构建通过。**
6. 不改 RTL、引脚、TCP/UDP/SDIO 协议或采集算法。不下载板卡、不烧录；若后续需要板测，另行确认。

本次是 Zynq 应用源码清理，不会释放此前 ESP32 TCP 测试中的内部 SRAM。

