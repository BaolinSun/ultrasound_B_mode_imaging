# USB / SDIO 编译期选择与 BOOT 输入记录

日期：2026-10-11。工具：Vivado/Vitis 2026.1、ESP-IDF 6.1。

> 以下为历史验证记录。2026-10-22 已将 USB 图像适配至 DDR 受保护完整帧，
> 当前行为、配套上位机及新 ELF 见 [USB 完整帧说明](USB_FRAME_TRANSFER.md)，不要沿用旧窗口接收器。

## 本次修改

- 开始时 Git 工作区干净，基线 `899eae5`，无需空检查点提交。
- 新增 `build/src/ps/data_transfer/output_config.h`，`DATA_OUTPUT_MODE` 默认 `DATA_OUTPUT_SDIO`，可改为 `DATA_OUTPUT_USB`。修改后重新编译并更新 Zynq 启动镜像，不能在运行时改变。
- 删除 UART/USB 解析器的 `OUTPUT USB/SDIO/STATUS` 路由及运行时 setter。UART/USB 其他参数配置入口保留，串口日志不承担输出模式控制。
- 主循环使用预处理分支，只调用所选数据发送路径。USB 编译分支不分配 SDIO 测试帧、不调用 SD1 初始化或 DMA。
- SDIO 上电自动握手并等待网页触发，仍不自动启动连续传输或真实成像。
- 规范源与当前 IDE 源同步；网页和 Python 接收工具提示同步改为宏选择。
- 两个硬件构建脚本、BOOT 打包及校验脚本统一使用 `ultrasound_B_mode_imaging.bit`。

## 构建和自检查结果

- `esp32c61/tests/run_tests.ps1` 成功。C 原始 UDP/配置事务/带头传输、ESP bridge 和实际 Zynq bridge 模型通过；Zynq 分别编译为 SDIO、USB 并执行。USB 检查零 SD1 调用、零测试帧 cache flush、无 `test_frame` 符号；非法宏值 99 必须编译失败。
- Python **61 项通过**，含新源码同步、无串口切换入口、主循环编译互斥检查。网页 **7 项通过**。负面用例的 `Soak FAILED` 为预期输出，整体退出码为 0。
- C 覆盖率目录：`esp32c61/tests/output/20261011-155325244`。Zynq SDIO bridge 行执行 100%，分支取值覆盖 95.69%；USB stub 行执行 100%。SDK 与硬件由模型替代，不代表电气或无线板测。
- ESP-IDF default、throughput 两配置构建成功。只更改网页/提示，不改变 SDIO 或 UDP 线协议，没有烧录。
- 新 Vitis 平台、BSP、FSBL 和应用构建成功。首次平台服务意外退出；在新独立目录重试成功，不删除当前用户 `workspace`。
- 成功平台目录：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\sdio_vitis_20261011_155051580`。XSA 仍为已启用 SD1 的 `build/output/ultrasound_B_mode_imaging.xsa`。
- 在该独立平台临时增加 USB 编译定义，完成实际 ARM 编译并保存 `build/output/B_mode_focus_imaging_USB_verify.elf`。随后移除临时定义、重新编译 SDIO、复制最终 ELF、重新生成 BOOT.BIN。最终宏和应用均为 **SDIO**。
- ARM `objdump --disassemble=main`：SDIO 主循环调用 `sdio_bridge_poll`、不调用 `usb_send_adc_data`；USB 主循环相反。

| ARM 应用 | text | data | bss | SDIO 测试帧 |
|---|---:|---:|---:|---|
| 最终 SDIO | 173963 | 140976 | 952944 | `[0x00151900, 0x00211d00)`，787456 B，64 字节对齐 |
| USB 验证 | 170283 | 140968 | 164560 | 无 |

`check_sdio_build.ps1 -Workspace build/sdio_vitis_20261011_155051580` 成功：源副本一致、XSA 保留 SD0/SPI1 且启用 SD1、BSP 两个 SD 控制器、DDR 不重叠、BOOT 分区顺序、统一 bit 与当前实现哈希一致。

日志：`build/reports/macro-mode-vitis-retry.log`、`macro-mode-usb-build.log`、`macro-mode-sdio-build.log`；ESP 日志位于 `esp32c61/tests/output/macro-mode-*.log`。

## bit 来源与 BOOT.BIN 绝对输入

本次只修改 PS 软件和导出命名，不修改 RTL/BD/XDC，也未重跑 Vivado 实现。将 **2026-10-09 已生成的 SD1 实现文件**：

`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\vivado\ultrasound_B_mode_imaging.runs\impl_1\heart_top.bit`

同步到统一输出：

`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.bit`

两者 SHA256 均为 `61A87A9BCA70A24AB1E4BAC7B1F418B8E4BE83D1F96F2391EDE8E882E9060DC1`，统一名称不再指向此前 9 月的旧 bit。

最终 BOOT 打包按顺序使用以下三个文件：

1. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\zynq_fsbl.elf`
2. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\ultrasound_B_mode_imaging.bit`
3. `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging.elf`

清单：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\boot.bif`。

最终镜像：`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\BOOT.BIN`。

`boot_inputs.sha256` 在相同目录记录三个输入的绝对路径及 SHA256；打包脚本每次重新生成该记录。新 FSBL 与成功平台内 `ubmi_p/zynq_fsbl/build/fsbl.elf` 哈希相同。

最终 SHA256（不是 2026-10-09 镜像）：

```text
0D5D3D432CC43372E10D0C2E9D8D989BB7C3D58F1CF1EBBBCC4D13253454868C  BOOT.BIN
CCE710CF0A8863BC206C03353422A96714EDB858958528F218FA200F179264DA  zynq_fsbl.elf
70581BFA77608C4B1ACE8E00EAB8F49CB548DC084B42392649B24E0E2917C814  B_mode_focus_imaging.elf
61A87A9BCA70A24AB1E4BAC7B1F418B8E4BE83D1F96F2391EDE8E882E9060DC1  ultrasound_B_mode_imaging.bit
C338F209B521B9CA0E002C31A4BC27AF18D769F989A870FCBBB601155070D9E0  esp32c61/build/ultrasound_psram_udp.bin
5835BAE111BEB240FD82BDC341200696F040271ACD5C1C493C52E498C6494C82  esp32c61/build-throughput/ultrasound_psram_udp.bin
```

## 未执行与风险

没有烧录 ESP32、没有更新 Zynq 启动介质、没有做 USB/SDIO 物理板测或 48 小时测试。切换镜像前先停止网页测试，建议两端重启，清除上一模式的会话；再按 `SDIO_BRIDGE.md` 验收。

PL 时序和 CDC 结论仍以 2026-10-09 的 `SDIO_VERIFICATION.md` 为准。既有 Unsafe/Unknown CDC 和 I/O delay 警告尚未关闭，不能把本次软件/构建通过视为 CDC clean 或整工程板级发布通过。
