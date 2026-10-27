# CPU1 网页关闭：2026-10-27 离线交付

基线为 `4e7d159`，相关源及旧应用/FSBL 备份在 `build/backups/cpu1_web_4e7d159/`。保留用户的 VS Code 设置改动，不纳入此次功能提交。

## 验证结果

- 实际 CPU1 控制核心：行与分支方向覆盖均为 100%；实际启动/运行适配层：行与分支方向覆盖均为 100%；FSBL 只读钩子行覆盖 100%。测试确认首次启动、部分控制位和已关闭状态均不被启动逻辑改写。
- 原生 C 回归通过：Wi-Fi 封装、重复请求、配置冲突、明确失败、双待机控制、只读状态刷新，以及 USB、SDIO、温度、AFE/TX、CPU0 WFI 与帧 TCP/UDP。
- conda `esp32c61` Python 回归共 237 项：236 通过、1 项历史 TXT 缺失跳过。构建与 PS 集成检查另 32 项通过。
- ESP32 页面 35 项、PC 图像页面 5 项通过。页面加载及自动刷新不会发送 CPU1 关闭命令；失败不自动重试，不发送扫描或高压操作。
- Vitis 2026.1：USB/SDIO × WFI 开关四种构建及当前 IDE 默认应用均通过；链接检查确认与 USB、帧槽位 DDR 区域无重叠。原有 USB 等编译告警仍存在。
- FSBL 重编及平台导出一致性通过；ELF 不含 `cpu1_disable` / `cpu1_platform_disable` 符号。SDK 对单核 eFuse 器件的启动保护不作修改；本任务删除的是工程自己的自动关闭动作。
- ESP-IDF 6.1 默认与 combined 构建通过；本次两者应用大小均为 993648 B，应用分区尚余 542352 B。保留现有用户生成配置，不调整网络性能参数或设备凭据。
- 本轮未连接板卡、烧录、复位、启动扫描或使能高压。实际启动及功耗不以离线测试代替。

## 产物

下表目录为：

`D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/cpu1_web_20261027/`

|文件|SHA256|
|---|---|
|`B_mode_focus_imaging.elf`（当前 IDE 默认应用）|`5ebc2bb9c533748e38ff111360baec10dbd5f4333f59dec781341d4f825a15d3`|
|`zynq_fsbl.elf`|`cc2b82b7d14557b0db8ab34ce8601d2c37bd5f516a3aabd22a2b72f7bde59c85`|
|`esp32_default.bin`|`60966adecdffc2f35763c8de69ce3f16793469d4b4f7b17f121505c7f8a6e580`|
|`esp32_combined.bin`|`7c52981f9aa9410e28f1e003a617c74c474eecf43f6e6037db59f0586d5d735d`|

同目录含四种 USB/SDIO、WFI 开关组合的应用 ELF。完整绝对路径、长度和哈希记录在 `esp32c61/logs/cpu1_web_build_manifest_20261027.json`；测试与构建日志在同一 logs 目录的 `cpu1_web_*` 文件。

同时更新下列常用输出：

- `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/B_mode_focus_imaging.elf`
- `D:/MyProjects/zynq_prj/ultrasound_B_mode_imaging/build/output/zynq_fsbl.elf`
- 当前 Vitis 应用与平台导出 FSBL：见 [操作说明](CPU1_WEB_CONTROL.md)。

bit/XSA/BOOT.BIN 已与修改前备份哈希核对一致。**BOOT.BIN 未重新打包，仍含旧启动软件**；若以 SD 卡启动，需后续使用新 FSBL 和应用重新打包。仅热下载新应用不会主动恢复已关闭的 CPU1。
