# TX7332 独立待机：构建与交付记录

状态：软件回归、Vivado 2026.1 综合/实现、最终实现审查、配套 Vitis 平台/应用和 ESP32 固件构建已完成；交付已核对哈希。尚未进行板级验收。

源分支 `feature/tx7332-power-control`，基于 AFE 已验证源码 `092cad8`。
不整体合入 `wip/safe-standby-uart-wake-20261015`，不加入 CPU/WFI、TX 全局掉电或时钟切换。
本次未连接板卡、未下载/烧录、未开启扫描或高压。

## 离线验证

- TX C 状态机：77 行代码覆盖率 100%，114 个分支方向覆盖率 94.74%，调用覆盖率 100%。脚本要求行 >95%、分支方向 >90%。
- TX RTL：保持数据握手、重复/零编号、部分写入、忙时元数据保护、停钟、在途复位；四路真实 SPI 读写排空；配置重就绪、寄存器保留、PI 极性及原读回 CDC 均通过。
- AFE 原有状态机与停钟/恢复仿真通过，未改变 AFE PLL/MMCM 参数。
- 采集与 DDR 回归通过：五档长度、AXI 背压、末尾短突发、溢出、租约和帧槽位保护。
- 完整主机回归：Python 214 项，213 通过、1 项因历史 TXT 样本缺失跳过；网页 28 项和 B-mode 网页 5 项通过。
- Vitis 源生成/旧代码清理检查 9 项通过；保留现有 Vitis main.c 的日志清理差异，仅同步本次 TX 功能。
- ESP-IDF 6.1：TCP/UDP × 默认/性能 combined 配置均构建通过；日常默认仍为 combined、TCP、PSRAM 40 MHz、发送缓冲 23040 B。

日志位于 `build/tx_power_*.log` 和 `esp32c61/logs/tx_power_*.log`。
主机回归里的 `Soak FAILED` 是故障注入用例的预期输出，应看最终 unittest/脚本结果，不能将其当作板测日志。

## 资源、内存与 CDC

- 独立 TX 控制模块综合：约 91 LUT、158 FF，0 BRAM、0 DSP；不是整个功能的最终实现增量。
- 独立、未使用旧整域例外的 TX CDC：0 Critical，68 项 CDC-15 与 2 项 CDC-17 为保持数据/寄存使能结构提示，结合握手时序、自检查及路由数据路径约束审查。
- Zynq 最终新 BSP 重编：SDIO text 212631 B、data 140604 B、bss 165872 B；USB text 206359 B、data 140604 B、bss 164816 B。
- 链接检查：USB 区 `0x1C000000`，三帧区 `0x20000000..0x202FFFFF`；应用 end 分别约 `0x00180FD0`（SDIO）、`0x00180BB0`（USB），无交叠。
- 默认 ESP32 镜像 979867 B（bin 可有对齐填充）；静态 DIRAM 136095 B/256624 B（53.03%），不是运行时空闲堆或低水位测量。

最终全设计路由后 WNS **+0.688 ns**、WHS **+0.037 ns**，TNS/THS 均为 0；满足已约束路径及本项目 WNS >0.5 ns 目标。内部无未约束端点，但既有外部 I/O 延迟缺失仍需板级约束补全，不能据此宣称所有外部接口时序均已验证。

|资源|最终使用|占器件比例|相对实施前磁盘报告|
|---|---:|---:|---:|
|LUT|31341|39.87%|+122|
|FF|62937|40.04%|+159|
|BRAM tile|188.5|71.13%|不变|
|DSP|293|73.25%|不变|

TX 控制实例在最终实现中为 81 LUT、158 FF；全设计增量还包含 SPI 空闲检测等改动，不等同于单实例资源。

最终 TX 实现审查通过：前向 34 条、返回 36 条保持数据路径均有不超过 10 ns 的约束且 slack 非负，六级握手/空闲同步寄存器保留 ASYNC_REG。新增 TX 控制实例无 Critical CDC；保持数据的 CDC-15/17 警告按握手结构审查，不靠整域 false path 隐藏。
现有 AFE 实现审查通过：8 路 Gray 源/目的约束、32 位恢复代次握手、局部复位同步链及总设计正 setup/hold 均保持有效；使用 AFE 原有两项局部复位结构豁免，未新增 TX 豁免。

全设计默认 CDC 报告仍有 56 项 Critical（CDC-1:32、CDC-4:4、CDC-10:9、CDC-13:11）；实施前磁盘报告为 55 项。本次比较发现多出的一项位于 `ila_bf_i/inst/<hidden>/...` 调试核内部，原六个行头运算端点的报告代表源位由 bit1 变为 bit0，相关数据通路未修改。不可把这些计数当作全设计 CDC clean；新增 TX 控制的检查与原有风险分开记录，不修改原 ILA 探针来消除告警。
旧设计的时序例外、AFE AXIS 时钟关联提示及 CDC 风险不据本次新模块通过而宣称解决。
增量布局曾因旧 LUTRAM 形状位置复用失败；最终采用相同最新综合结果的全新布局布线，不以旧 netlist 代替最终 RTL。
`ila_bf` 必须通过实现检查，保持五组原探针 `17/2/17/32/7`。

## 产物、备份与启动镜像

最终交付目录为 `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/tx_power_20261022/`，已由交付脚本在实现审查通过、BSP/XSA 一致且哈希匹配后写入。
校验清单由 `build/scripts/deliver_tx_power.ps1` 生成至 `esp32c61/logs/tx_power_build_manifest.json`，并核对 bit/XSA 与路由审查哈希绑定。
独立 BSP/FSBL 构建目录为 `build/tx_power_platform_20261022`，不删除当前 Vitis 工作区。

新 BSP 的绝对路径为 `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/tx_power_platform_20261022/ubmi_p/export/ubmi_p/sw/standalone_ps7_cortexa9_0/`。
XSA 内嵌 bit 与单独 bit 逐字节一致；独立平台输入/导出 XSA 哈希与该交付一致，两份应用均链接此 BSP。
下表文件名均相对于上述最终交付目录，SHA256 为完整值：

|文件|字节数|SHA256|
|---|---:|---|
|`ultrasound_B_mode_imaging.bit`|5980116|`E17F6E53FDD8C594243566DFD708E61AF2B5B41F8D94159D6867FA03FBE63239`|
|`ultrasound_B_mode_imaging.xsa`|1527197|`7A7B1223E515C772E7600B158CBDE768F122377CB7F662018F98B0F5529D3754`|
|`ultrasound_B_mode_imaging.ltx`|19694|`14C3551092C8CE1BC4DE3249568A9235D6020986ED08B74237C43DB9331FB559`|
|`B_mode_focus_imaging_sdio.elf`|1244740|`C0B11E88926F37120F6DDED0E8518EDD1D194D0EAA43433903731A44232E85D4`|
|`B_mode_focus_imaging_usb.elf`|1231728|`F890501B950A58D1D39DA5954CA944D0C070B61F11FE848F7F13B4EA06A5A2D3`|
|`zynq_fsbl.elf`|593512|`BF8FA5E8D08C0E81949B982F2D36D76129C5AAA8370AD6D30EB1DE6D483F0A82`|
|`esp32c61_combined_tcp.bin`|980208|`61A0EFD9A55218FE6249D0B556A9E9E0F9BED93135782B30F96EADB3D2C24AFD`|
|`esp32c61_bootloader.bin`|22256|`590A4367A9E520A9A74C8EF096EE8E1E09947449FECD97E68C6531EA08BCAAF9`|
|`esp32c61_partition_table.bin`|3072|`5295E8C42D6FB5F95E4E8A185932C6191D27CFC5078322475347E40E71AF815A`|
|`esp32c61_throughput_tcp.bin`|980208|`0CBECC1F11EB9B48FAE4A2B6D7A7A0991A1BEBDAF018C808611015175217A288`|
|`esp32c61_throughput_udp.bin`|980112|`C0DE7844824319EB52ABF3A01BE6C2D0D97A2E4776E1DE695208C65ECF4B3E3A`|
|`esp32c61_default_tcp.bin`|980192|`0ACFEDA3EBFE69468384161CC2B73A6D8641348B07608795C2BD7F7087D0FEEE`|
|`esp32c61_default_udp.bin`|980080|`75E292F3744E0B6B24010B160C134B262B0320A88D82CEE48722004338E9A3B9`|

常用路径已同步：

- `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/ultrasound_B_mode_imaging.bit`
- `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/ultrasound_B_mode_imaging.xsa`
- `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/B_mode_focus_imaging.elf`（SDIO）
- `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/zynq_fsbl.elf`
- `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/workspace/B_mode_focus_imaging/_ide/bitstream/ultrasound_B_mode_imaging.bit`
- 当前 `workspace/ubmi_p/hw/` 与 `workspace/ubmi_p/export/ubmi_p/hw/` 下的同名 XSA。

当前 Vitis 工作区未被重建或整体覆盖；新 BSP 完整保留在独立构建目录。ESP32 日常更新仍使用已有 `build.ps1` 的 combined 默认构建流程，不建议仅凭 app bin 猜测烧录偏移。

实施前磁盘产物备份在 `build/backups/tx_power_092cad8/`；它保存当时文件，不代表所有旧二进制都经过本次按该提交重建。
Git 切换分支不会自动回滚生成物；回退必须同时恢复配套 bit、PS ELF、ESP32 固件。

`build/output/BOOT.BIN` **不更新**，实施前 SHA256 为：

`30BE9B06D8E058C6D26B249EA960DC460CB467EE3EF7D57B3812F2DDDB256360`

## 尚未验收

本次未进行板级待机/唤醒、实测功耗、真实成像恢复、连续传输吞吐或 48 小时测试。
已有温度相关时钟问题不属于本次已解决项。GPIO 高压关闭读回也不证明储能电容已放电。
操作说明见 [TX7332 电源控制](TX7332_POWER.md)，上板另行确认设备和授权。
