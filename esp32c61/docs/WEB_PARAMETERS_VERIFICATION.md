# 网页参数配置：离线验证与产物

日期：2026-10-29；修改前基线 `2d6edfb`。

## 验证结果

- C 原生回归通过：包括 USB 完整帧、SDIO、TCP/UDP、配置所有权、AFE/TX、CPU、温度等现有测试。
- Python 完整回归：254 项，全部通过，1 项历史数据测试因文件不存在跳过。
- 原控制网页 49 项、原图像网页 5 项、新参数核心 9 项 Node 测试通过。
- 新参数核心覆盖率：行 100%，分支 92.16%，函数 100%；这不是整个工程覆盖率。
- 浏览器命令生成与只读参考 JSON/Python 打包对照通过；旧 2048 点时序仅在测试副本改为合法 6144 点进行编码对照。
- 本机 TCP/UDP 回环均验证五档完整帧接收和实际点数；部分帧/CRC/顺序检查继续保留。
- 默认灰度与参考纯计算输出差异不超过 1；新增 DR/亮度/对比度/噪声底/LUT 组合对照逐像素一致。
- 显示设置并发、修订冲突、旧图像不发布、同源及请求限制测试通过；显示设置测试单独复核 7 项通过。
- 本机离线浏览器检查参数页、时序表、AFE 非法组合拒绝、PC 显示设置及 TGC/LUT 区域，未访问设备。

## 构建与内存

- ESP-IDF 6.1：工程默认目录与日常 combined 目录均构建成功。项目默认配置本身已是 combined 40 MHz / 23040 B 上传缓冲；没有修改既有性能参数。
- Vitis 2026.1：USB、SDIO 两种宏构建通过。两者分配段均通过 DDR 保留区域检查。
- USB ELF 分配段末端 `0x00184CB0`，SDIO 为 `0x001850D0`；均低于应用上限 `0x1C000000`，不占用三帧槽位。
- combined 静态内存报告 DIRAM 使用 136407 B，余 120217 B；这是链接期余量，不是运行期可用堆，不代表已修复 HTTP SRAM 紧张。
- 新编辑器 HTML/JS 置于 Flash；ESP32 不保留大型 JSON、系数表或完整命令列表。
- 既有 SDK 私有 include、PS USB 未使用参数等警告仍存在，本次没有当作已经清零。

## 产物绝对路径与 SHA-256

### ESP32 默认工程

`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build\ultrasound_psram_udp.bin`

- 大小：1050880 B
- SHA-256：`201b518a4966d254b84ffc7e88768ac97fe1f3000fb82087ea2f963f40285d40`
- 应用分区剩余：485120 B（31.6%）

### ESP32 combined

`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket-exp-combined\ultrasound_psram_udp.bin`

- 大小：1050880 B
- SHA-256：`e8750938825c73d71d444c3bdb310a99d9be45079aaf74ee9f7da91fdbd41fe4`
- 应用分区剩余：485120 B（31.6%）

### Zynq SDIO

`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\vitis_frame_store_sdio\B_mode_focus_imaging.elf`

- 大小：1305124 B
- SHA-256：`683c916a961247cbea15769783c4351a85ceee2295461b01a827278121a6b43e`

### Zynq USB

`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\vitis_frame_store_usb\B_mode_focus_imaging.elf`

- 大小：1286796 B
- SHA-256：`dfd91b2f605afa559ed0047566fb1f1242e6b85ebbb3bd25cae98f5fdd24ef67`

## 更新和验收边界

- 配套更新 ESP32 应用、Zynq PS 应用及 PC 显示代码；bit/XSA/FSBL 不变，BOOT.BIN 未打包或覆盖。
- 当前 Vitis 应用源与规范 PS 源只同步新增点数检查，未整文件覆盖其他模块。
- 参考工程未修改；保留其原有 `.gitignore` 编辑和测试文件删除。
- 修改前源码 ZIP 和基线记录在 `logs/parameter_config_checkpoint_20261029/`，运行记录在 `logs/parameter_*.log`，均不进入 Git。
- 未连接真实板卡，未烧录、下载、复位、启动扫描或使能高压；真实参数正确性、成像效果及长时间吞吐需另行板测。
- 已知问题 `ESP-HTTP-001` 保留，未宣称解决。

操作说明：[网页参数配置](WEB_PARAMETERS.md)。

## 快捷参数迁移补充（2026-10-29）

首页删除独立参数面板，入口放入 Zynq 控制卡片标题栏。数字增益及图像下采样迁入高级参数页顶部。
本次仅修改 ESP32 网页资源及浏览器逻辑；PS、PL、PC 图像处理不变，不需要重新下载 Zynq。
迁移后的首页/参数页 56 项 Node 测试通过，包含五档映射、独立状态、准入、精确回执、停止后状态、错误及防重复提交。默认与 combined 两套固件离线构建通过，并完成模拟接口浏览器布局检查。
上面的完整 C/Python 回归为此前功能交付记录；本次未操作板卡。
