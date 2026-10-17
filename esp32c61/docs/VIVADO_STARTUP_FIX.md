# Vivado “Could not open 'C' for writing” 修复

## 本次定位（2026-10-17）

界面中的 `[Common 17-354] Could not open 'C' for writing` 来自 `mult_36x16_synth_1` 保存的历史消息数据库。对应原始 `runme.log` 为 **2026-10-15 16:05:21** 启动的运行；该错误发生在用户初始化脚本和乘法器综合之前。该次综合后来仍完成，生成 DCP，校验值为 `78fb284f`，不能将启动阶段的 Common 消息直接当作乘法器 RTL 综合失败。

用户级 Vivado Tcl Store 保留了旧的持久加载项：

```tcl
::tclapp::load_app -namespace {aldec::activehdl} aldec::activehdl
```

本工程使用 XSim，不依赖此扩展。已确认的问题在启动阶段与历史消息缓存，而非本次 ILA 探针或乘法器运算。旧脚本注释曾将原因归于 Active-HDL 的路径拆分，但本次没有定位到该扩展内具体的出错语句，因此**不将 Active-HDL 认定为唯一根因**，也不能简单解释成“C 盘没有管理员权限”。`C` 是异常写入目标。Tcl 扩展在后续启动时重新加载的机制可参考 [AMD UG894](https://docs.amd.com/r/2022.2-English/ug894-vivado-tcl-scripting/Install-and-Uninstall-Hook-Procs)。

原启动脚本在每个进程启动时直接改写共享 `tclapp/manifest.tcl`。多个 OOC 工作进程、或尚未关闭的旧 GUI，可能相互覆盖设置；部分启动动作又早于该脚本，所以它不能清除已记录的错误。

## 已实施

1. 用户保存并关闭全部 Vivado 窗口，未强制结束 GUI。
2. 备份旧启动脚本、工程 XPR、旧扩展清单和该 OOC 的日志/消息库。
3. 清理迁移残留的 Active-HDL 精确加载项，保留其他扩展。诊断阶段旧脚本已经移除了该行，因此正式安装脚本显示 `removed_legacy_entries=0`。Vivado 打开工程时仍可能重新注册模拟器扩展并生成清单；**清单是否为空不是成功判据**，应以新启动和新综合日志没有 Error 为准。
4. 安装 v2 启动脚本，保留 `set_param tclapp.enableGitAccess 0`，不再从并行启动回调中改写共享清单；未降低或屏蔽任何错误消息。
5. 原 GUI 曾用旧内存状态覆盖新 XPR，导致真实 ILA 及实现前钩子登记丢失；备份后仅恢复 ILA 集成。补齐“XCI 已生成但 OOC run 缺失”时的 `create_ip_run -force`，保持全部原探针不变。
6. 运行独立内存工程，**实际执行** `mult_36x16` 综合，而非只命中 IP 缓存；随后重跑原 OOC 入口以更新 GUI 使用的消息数据库。原 `runme.log` 是追加写入，已将含旧错误的文本移入备份，再生成本轮独立日志，未删除历史证据。

部署的用户脚本为：

`C:\Users\sunbaolin\AppData\Roaming\Xilinx\Vivado\2026.1\Vivado_init.tcl`

备份位于：

- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\vivado_startup_backup_20261017_114254`
- `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\vivado_startup_repair_20261017_114457853`

未删除扩展安装目录、未修改 AMD 工具安装文件。共享清单由同一 Windows 用户的 Vivado 版本共用；不应将删除整个 Tcl Store 或反复擦除清单作为日常构建步骤。

## 结果与范围

- 新的实际综合：**0 Error、0 Critical Warning**；仍有 IP 生成 VHDL 的既有端口告警，未隐藏它们。
- 新旧综合校验值均为 `78fb284f`；本次未更改乘法器的功能、参数或流水级。
- 实际综合验证日志：`build/reports/mult_36x16_startup_verify.log`；其中应包含 `VIVADO_STARTUP_OOC_VERIFIED`。
- 原运行刷新日志：`vivado/ultrasound_B_mode_imaging.runs/mult_36x16_synth_1/runme.log`，已不再出现 Common 17-354/17-1257。
- 启动修复自检查 5 项；原 ILA 集成回归 8 项。未重新实现整个设计、未改变时序约束、未处理既有功能 CDC 告警。
- 上一轮 bit/LTX/XSA、现有 BOOT.BIN 保持不变；未下载、未烧录，也未启动扫描或高压。

现在直接重新打开当前 XPR 即可：

`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\vivado\ultrasound_B_mode_imaging.xpr`

若打开旧备份日志，仍能看到当时的错误，这是保留的历史证据，不是新的错误。后续若新运行再次报错，核对最新日志的时间和错误位置，不要通过降低 Common 消息等级处理。

## 复用修复与验证命令

本次已经部署，**无需每次构建前重复执行**。在另一用户环境需要同样修复时，先关闭所有 Vivado，再在成像工程根目录执行：

```powershell
.\build\scripts\repair_vivado_startup.ps1
```

脚本只替换本工程已知的旧启动脚本；遇到未知个人脚本会拒绝覆盖，要求人工合并。每次修复都新建备份目录，不删除原文件。

自检查（关闭 Vivado 后运行）：

```powershell
python -m unittest discover -s .\build\scripts\tests -p test_vivado_startup.py -v
```

需要单独复验真实 OOC 综合时：

```powershell
& 'D:\AMDDesignTools\2026.1\Vivado\bin\vivado.bat' -mode batch -source .\build\scripts\verify_vivado_startup.tcl -log .\build\reports\mult_36x16_startup_verify.log -journal .\build\reports\mult_36x16_startup_verify.jou
```
