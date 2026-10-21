# TCP 数据路径分项优化与回退（2026-10-21）

后续经用户另行授权完成的四档实际板测、异常轮和最终回退状态见
[2026-10-21 板测报告](TCP_DATA_PATH_BOARD_TEST_20261021.md)。下面“不烧录”描述的是最初离线实现阶段的边界。

## 范围与检查点

修改前已提交上一轮全部相关改动：`220869c`，
`perf: checkpoint fused SDIO CRC copy and cooperative TCP scheduling`。
本轮只改ESP32代码、构建配置、PC测速诊断和测试；不修改Zynq/PL、SDIO及TCP/UDP线协议、
扫描/高压控制。不下载、不烧录、不覆盖BOOT.BIN，不排查交接停滞或改动AFE采样时钟。

## 四个独立实验档

在`esp32c61`目录使用`build.ps1 -Profile throughput -Experiment <档位>`。
上传TCP缓冲仍为23040 B，发送准入余量仍为16384 B，PSRAM帧槽仍是两份最大帧。

| Experiment | 核心锁及输入锁 | TCP应用载荷路径 | PSRAM时钟 |
|---|---|---|---|
| `none`（默认） | 保留现有sdkconfig | 保留现有sdkconfig，原性能档为暂存 | 原配置40 MHz |
| `core` | 开启，附线程安全检查 | SRAM暂存 | 40 MHz |
| `direct` | 关闭 | PSRAM直接交给复制型socket | 40 MHz |
| `combined` | 开启，附线程安全检查 | PSRAM直接交给复制型socket | 40 MHz |
| `psram80` | 同combined | 同combined | **80 MHz，仅实验** |

`core/direct`分别与none对照，combined检验组合效果，psram80与combined对照，
不要把不同抽取档位、网络环境或测试时长混在一起比较。

每个实验使用`build-throughput-fast-socket-exp-<档位>`及自己的sdkconfig；
显式`-Transport udp`则使用`build-throughput-udp-fast-socket-exp-<档位>`。
不覆盖原`build`、`build-throughput-fast-socket`的sdkconfig。
脚本会检查实际生成的配置：若实验目录被menuconfig等改成不匹配的参数，会拒绝继续，
不会静默覆盖用户配置或带着错误标签烧录。未指定Experiment的行为保持兼容。

## TCP直接提交的边界

原路径：`PSRAM → 应用staging → lwIP → Wi-Fi`。
新路径：首段仍合并24 B帧头和最多1416 B数据；后续为`PSRAM → lwIP → Wi-Fi`。
这**不是零拷贝DMA**，不修改lwIP源码，不直接调用不带COPY的raw tcp_write。
已核对本机IDF 6.1 `lwip/src/api/sockets.c`：send()通过NETCONN_COPY调用
netconn_write_partly；数据复制入协议栈后才返回接受的字节数。
升级IDF时必须重新审查这个假设，现有版本适配器仍限制为6.1。

- 后续每次提交上限仍为5760 B，不一次提交整帧；保留部分写入、堆准入及停止/超时检查。
- 同一个发送任务拥有socket。整帧内容保持不变，PC会话/帧号/CRC回执通过后才释放SENDING槽位。
  失败、取消或断网仍先退出发送、关闭异常连接，再执行原唯一完成回调。
- direct档共享内部staging实际分配1472 B，仍容纳原始UDP及32 B头+1440 B载荷的带头UDP；
  UDP始终走内部暂存，不因直接TCP路径而改成PSRAM DMA。
- 相对原fast档5760 B应用staging，少申请4288 B内部堆；不是保证运行时最低堆增加同样数值。
- 不关闭CRC、PC确认，不增加在途帧数量或第三份PSRAM帧。

核心锁使部分协议栈工作在调用任务中持锁执行，减少消息传递与切换，但也可能增加锁竞争和
调用任务栈压力。未盲目扩大4096 B发送任务栈，新增栈低水位诊断供板测观察。
持有本应用状态锁时不执行send/recv/select或TCP PCB适配器操作；PCB调整仍由tcpip_api_call序列化。

## 新状态与测速字段

`/api/status`及测速.device.json提供：

- `tcp_direct_psram`：编译时是否选用直接路径。
- `tcp_stage_bytes`：单次TCP载荷提交上限；不再等同于真实分配量。
- `tcp_staging_alloc_bytes`：实际共享内部staging分配大小。
- `lwip_core_locking`、`lwip_core_locking_input`：编译配置。
- `psram_speed_mhz`：编译的PSRAM工作频率；不代表板级时序已经验收。
- `tcp_staged_bytes`：最近TCP请求复制到应用staging的载荷字节数，不含帧头。
- `tcp_direct_bytes`：最近请求通过直接路径交给socket接受的载荷字节数。
- `tcp_task_stack_min_bytes`：发送任务历史最小空闲栈，IDF单位为字节；首个请求结束前为0。

成功的197632 B帧，direct档应为staged=1416、direct=196216；
787456 B帧应为staged=1416、direct=786040。取消时已复制量可能大于实际发送量，不能当成有效吞吐。
`tcp_copy_us`只测应用复制；直接路径仍有协议栈复制开销，会体现在发送阶段，而非凭空消失。
网页同步显示路径、核心锁、真实staging分配和PSRAM频率；融合接收计时正确标注为“复制＋CRC”。

## 简短操作

编译不会烧录，建议先逐项构建：

```powershell
.\build.ps1 -Profile throughput -Experiment core
.\build.ps1 -Profile throughput -Experiment direct
.\build.ps1 -Profile throughput -Experiment combined
```

下面是供用户手动执行的更新说明，不表示本轮已经获准或执行板测。
先停止扫描、关闭高压并退出占用串口的monitor；COM3仅为示例。
更新与monitor必须选择同一个实验目录，例如：

```powershell
.\build.ps1 -Profile throughput -Experiment combined -Action flash -Port COM3
.\build.ps1 -Profile throughput -Experiment combined -Action monitor -Port COM3
```

温度/时钟正常且已通过现有入口恢复所需扫描后，关闭其他接收程序释放50001端口，
保持相同4倍抽取配置，分别测量：

```powershell
python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 60 --log tcp_combined_40_60s_01.jsonl
```

该命令只启停上传，不改变扫描、高压或下采样。原PC图像显示命令及接收协议不变。
分别使用新日志名记录none/core/direct/combined，不覆盖旧记录。
除有效吞吐/完整帧率外，还需检查CRC、回执、堆/栈低水位、网页响应及温度日志，
不能只比较`tcp_copy_us`或瞬时峰值。异常中止不能算正常60秒测速。

## 80 MHz与回退

80 MHz构建使用`-Experiment psram80`，**不默认启用**。
当前只确认IDF提供该配置，尚未确认本板实际PSRAM料号、额定频率和信号完整性余量；
因此不能将编译通过视为允许80 MHz稳定运行的证明。先核对器件及PCB，之后再单独授权板测，
验证完整内存、CRC、温度范围、反复启动和长时间上传；不要直接尝试120 MHz。

需要回退运行行为时，重新更新原性能档（不带Experiment），并使用同目录monitor：

```powershell
.\build.ps1 -Profile throughput -Action flash -Port COM3
```

原性能档sdkconfig未被本轮实验覆盖；网页应显示暂存路径、核心锁关闭、40 MHz。
若用户曾自行修改该sdkconfig，请先核对状态，不假定none能强制恢复任意手工修改。
精确源码基线为`220869c`；如需重建旧版，应从该提交建立独立工作区，避免硬回退覆盖当前编辑。

本次无需更换Zynq ELF、bit、XSA、BSP、FSBL或BOOT.BIN。板级提速幅度及48小时稳定性尚未验证。

## 离线验证与交付产物

- 原生C测试通过：暂存/直接两路径均覆盖部分写入、堆准入、非对齐尾部、五档动态长度、
  CRC回执、超时、发送中取消/断网。直接路径另以`-O2`测试1472/4096/5760/11520 B提交上限，
  检查PSRAM源指针、实际staging范围及末端哨兵；原UDP、SDIO槽位与唯一回调回归保留。
- Python全套186项：185通过、1项缺少历史样本跳过；网页26项和图像网页5项通过。
  最后修改的构建入口刷新/校验逻辑另经7项假IDF测试通过，不产生真实flash调用。
- IDF 6.1：TCP/UDP × 原默认/性能四组回归通过；core/direct/combined/psram80四个TCP实验
  通过，组合配置UDP构建也通过。只验证编译、配置及链接，不代替运行测试。
- C61反汇编确认TCP内核的应用memcpy调用点由2处减为1处，只保留首段合并复制。
  核心锁适配器仍通过tcpip_api_call串行访问PCB，不在未持锁上下文直接改动。

| 性能档 | 静态DIRAM | 共享staging堆申请 |
|---|---:|---:|
| none | 135893 B | 5760 B |
| core | 135863 B | 5760 B |
| direct | 135893 B | 1472 B |
| combined | 135863 B | 1472 B |
| psram80 | 138631 B | 1472 B |

静态DIRAM总量256624 B；表中不包含运行时网络缓冲、任务栈和动态分配。
80 MHz构建还增加静态代码占用，不能认为它与40 MHz有完全相同的内部堆余量。

四个TCP实验的应用均位于绝对目录：
`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket-exp-<档位>\ultrasound_psram_udp.bin`。

| 档位 | 应用大小 | SHA256 |
|---|---:|---|
| core | 971664 B | `17505044CF9ED94BB05AD6BB7A73BC2EF6CAC6C5EA6A202C516ECBA78DCB8C2D` |
| direct | 967152 B | `FE40C5FDBE882142ECCE49E38BD2DB423EC66366FF285F6BA4159327B2876DF7` |
| combined | 971616 B | `8191A53FCFE6F40824D675F04A2586F46730561B93EC4259314B6BAA2B6F7FA3` |
| psram80 | 974304 B | `499909AF750D52EC1622913E750051C77DE173BCB9587E6B036EDAB38F4CEB80` |

每组均使用所属目录的bootloader、分区表及flash_args，不把应用bin当整片Flash镜像。
组合UDP输出在`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-udp-fast-socket-exp-combined`；
普通配置和源码宏默认仍选TCP，没有将普通目录遗留为UDP。

日志、配置和反汇编记录位于：
`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\tcp_path_20261021`。
其中`checkpoint_220869c`另外保存了先前40 MHz性能镜像、bootloader、分区表和flash_args
（不含旧ELF调试符号），应用SHA256为
`455F9C6574C40636D16DE49DCFBC41CC8C17F468912CA98ECB7F678CB53279B0`。
这些是本机先前构建的回退镜像，不表示本轮重新执行了该版本板测。
