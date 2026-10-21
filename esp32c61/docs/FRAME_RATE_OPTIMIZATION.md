# 完整帧传输与显示帧率优化（2026-10-20）

## 基线与范围

修改前工作区干净，已提交基线为 `4087dc2`；未创建空的检查点提交。
相关源码、原固件及本次4倍测速日志另行备份至：
`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\verification\frame_rate_opt_20261020_203755`。
本轮改动未自动提交，也未操作板卡。

原4倍下采样日志：197632 B/帧，242帧/60.48秒，整体6.33 Mbit/s、4.00 FPS；
PL发布约13.62 FPS。PC有效载荷读取区间累计36.45秒，对应10.50 Mbit/s，
剩余约24.03秒包含帧头等待、回执、源端准备及调度等开销。
这些日志不能将全部剩余时间精确归因于PS CRC，更不能把读取区间速率当作物理Wi-Fi带宽。

本轮不修改PL、扫描周期、AFE配置、帧长/线头、SDIO协议、TCP/UDP线协议或高压控制。
不增加第三个PSRAM帧缓冲，不取消CRC或应用回执，不用重复图像充帧率。

## 修改内容

### Zynq PS：减少整帧准备开销

- 新增 `data_transfer/frame_crc.c/.h`，IEEE CRC32及种子/返回值与原协议和Python zlib兼容。
- 原实现每字节两次4位查表；现在每字节一次256项表查找，并对已对齐输入使用32位读取。
  未对齐首部、尾部仍逐字节处理；使用memcpy和对齐提示，避免强转引入严格别名问题。
- 只对纯CRC函数使用GCC局部 `O3`，保留Vitis其余应用 `O0` 调试设置；不启用全局DCache，
  不改变DDR缓存策略、租约或DMA读取顺序。CRC与发送仍持有同一个不可覆盖的源帧。
- 同步规范源和当前Vitis源，登记新C文件；生成脚本按源文件扫描也会包含它。
- 将正常逐帧串口打印限为最多每秒一次，错误日志不隐藏。新行：

```text
@SDIO PERF frames=... id=... bytes=... crc_us=... source_ms=... (ESP receive, not PC ACK)
```

`crc_us`是PS计算本次整帧CRC的耗时；`source_ms`从该帧准备轮次开始到ESP接收完成，
包含CRC、控制交互及SDIO，不是纯DMA耗时，也不是PC确认耗时。

### ESP32：同样内存预算下自适应分块

- 仍为23040 B上传发送缓冲、5760 B staging、16384 B入队余量（性能fast档）。
- 内存容不下整个staging块时，按剩余预算发送较小的完整MSS块；最后短尾保持精确长度。
  若连一个MSS都容不下，仍有限等待，受原30秒截止时间、断网及停止检查约束。
- 不降低余量阈值，仍按两份数据拷贝预算，不关闭资源保护。
  余量是入队准入门槛，并非系统级内存预留，不能保证运行时堆最低值一定大于16 KiB。
- 部分写入继续由原exact发送引擎处理。只有PC完整收帧且CRC回执匹配，才释放SENDING槽位。
- 新增最后完成帧的状态字段：

| 字段 | 含义 |
|---|---|
| `tcp_adaptive_writes` | 因堆预算缩小发送请求的次数，不是重传次数 |
| `tcp_queue_us` | submit成功到发送任务开始的时间 |
| `tcp_idle_us` | 同一会话上一帧确认完成到本帧发送任务开始的时间；首帧为0 |

`sdio_benchmark.py`将这些字段加入抽样完成帧均值。它只看到轮询采到的帧，
不是逐帧全量统计；各等待/阶段计时存在重叠，不能简单相加。

### PC：显示节流与测速诊断

- 图像发布上限从10提高到20 FPS；按开始到开始计时，渲染时间不再叠加一份固定等待。
- 浏览器取图目标周期50 ms，包含请求耗时；仍只允许一个在途图像请求。
- 保留最多两份待处理载荷、慢处理丢旧帧、冻结后台继续接收、完整帧校验及会话隔离。
  20 FPS只是软件上限，不代表本板已达到20 FPS。
- TCP接收summary新增 `valid_payload_read_s`、`outside_valid_payload_s`、`payload_read_mbit_s`。
  后者只度量校验通过帧的载荷读取区间；主要有效吞吐仍看 `payload_mbit_s`，不能用新字段替代。

## 离线验证

- C自检查：新旧CRC逐字节一致，覆盖黄金向量、非零种子、16种地址偏移、短尾、五档帧长、
  1 MiB上限、512 B分段及输入不变。原SDIO协议测试仍覆盖CRC失败、租约、超时、复位和禁止重放。
- 自适应预算检查覆盖边界、零/大整数、MSS及短尾；用真实exact引擎验证小预算下整帧、包头跨
  部分写入、回执一致性，预算不足时仍超时而不越过准入门槛。
- Python/网页回归覆盖新统计字段、源文件同步、新完整帧发布及不重复旧图；原温度、控制、
  原始UDP、TCP/UDP完整性测试继续运行。
- 最终回归Python共178项（177通过，1项缺少历史样本跳过），网页25项加图像网页5项通过，
  原生C自检查通过。未将功能测试当作完整覆盖率签核或板级稳定性结果。
- 主机16次197632 B CRC对照约旧21～23 ms、新6 ms；仅为PC测试，不是Zynq耗时或板级提速结果。
- Vitis 2026.1 USB/SDIO及当前workspace构建通过。实际ARM反汇编确认CRC对齐主循环使用LDR，
  未退化为memcpy函数调用；DDR ALLOC检查通过，SDIO应用末地址0x00178610。
- ESP-IDF 6.1 TCP/UDP × 默认/性能四组构建通过；两档TCP应用分区均剩余约37%。

| 构建 | text | data | bss |
|---|---:|---:|---:|
| Zynq USB | 182139 B | 140604 B | 162320 B |
| Zynq SDIO | 188571 B | 140604 B | 163376 B |

ESP32默认/性能静态DIRAM分别123631/135593 B，总量256624 B；这不是运行时堆余量。
本次没有改变RTL或XDC，无需重新生成bit/XSA；原时序/CDC风险及温度相关时钟问题仍独立保留。
没有执行硬件测速、成像/高压控制、烧录、JTAG下载或48小时验证，不承诺具体FPS增幅。

## 更新与简短复测

要包含全部优化，更新PS应用和ESP32固件，并重启PC图像服务；原PL bit继续使用。

PS应用（已同步到Vitis当前构建目录）：
`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\workspace\B_mode_focus_imaging\build\B_mode_focus_imaging.elf`。
同步输出为 `D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\build\output\B_mode_focus_imaging.elf`。
SHA256均为 `F8BD46283EC62C860D947A54EBBE5A0AD634A6D048E31F7532D612AFE23C2062`。

ESP32性能应用：
`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket\ultrasound_psram_udp.bin`，
SHA256 `7319D7279790E2ECE69575B1FEF423DFF405ADFB59F0B32E8EF246D2AD1A2DC1`。
使用对应目录的bootloader、分区表及flash_args，不把应用bin当完整Flash镜像。

以下命令在esp32c61目录执行；只有当前端口确为COM3时才使用该端口，自行确认停扫/高压关闭后更新：

```powershell
.\build.ps1 -Profile throughput -Action flash -Port COM3
```

保持与原测速相同的4倍下采样、扫描配置、网络和供电。独立测速时先停止图像页接收，
释放50001端口，手动恢复扫描；不必为数据通路测速使能高压。

```powershell
python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 60 --log throughput_4x_opt_60s_01.jsonl
```

工具自动启停上传，不自动启停扫描/高压。不要覆盖原日志。比较 `payload_mbit_s`、
`valid_fps`、CRC/ACK错误及堆低水位，同时比较 `tcp_idle_us`、`tcp_queue_us`、
自适应发送/保护等待和PS `crc_us`，区分哪一段真的缩短。

测速结束、端口释放后，重启显示服务并刷新浏览器：

```powershell
python .\pc_tools\bmode_web_viewer.py --device-ip 192.168.3.27
```

保留相同完整新帧口径，不与USB滚动行缓存的刷新次数直接混比。
本轮 **BOOT.BIN未更新**；从旧SD卡启动不会自动得到新PS应用。

## 第二轮：ESP32 接收热路径及 TCP 调度（2026-10-21）

修改前已将上一轮全部相关改动提交为 `e0a2e68`：
`perf: checkpoint CRC and frame transport scheduling optimizations`。
本节是其后的独立工作区改动，不表示已经上板验证。

### 原因与改动

最近一次不抽取测速为787456 B/帧、106个完整有效帧、10.98 Mbit/s、1.74 FPS。
不能与4倍抽取197632 B/帧的5.84 FPS直接比较；比较优化前后时必须保持抽取档位、
采集参数和网络条件一致。不自动改变用户的下采样系数或显示深度。

- SDIO接收原先先`memcpy`到PSRAM，再读取DMA数据计算CRC；新增
  `bridge_crc32_copy()`，在SRAM切片表路径一次读取DMA字，同时计算CRC并存入PSRAM。
  对齐检查放在循环外，非对齐输入/输出及短尾走精确字节路径；不读写范围以外的数据。
- 保留启动时ROM/切片表测速选择。表分配失败或ROM更快时仍使用原复制+ROM CRC路径；
  新内核在接受SDIO数据前进行小块复制/CRC自检，失败则保持SDIO不可用，网页继续运行。
- TCP因资源或socket背压已经等待至少1 ms时，清除之前累计的连续发送字节额度，
  避免随后又重复执行一次公平性休眠。无实际等待时，仍保留每约64 KiB至少一次让出CPU；
  立即就绪、短于1 ms的等待不能获得这个额度。停止、断网、30秒截止时间和回执检查不变。
- 不放大TCP发送缓冲、不降低16 KiB准入余量、不添加第三份PSRAM帧、不提高PSRAM时钟。
  TCP staging仍为5760 B（fast档），整帧CRC、PC应用回执及两槽位所有权不变。

### 新诊断字段

| 字段 | 含义 |
|---|---|
| `tcp_cooperative_waits` | 本帧发送时已等待至少1 ms、可替代额外公平性休眠的次数，不是重传次数 |
| `sdio.last_copy_crc_us` | 最近完整帧的复制+CRC累计墙钟时间，包含任务抢占，属于接收时间的子集 |
| `sdio.crc_copy_fused` | 是否使用SRAM切片表的融合复制路径；false时使用ROM回退 |
| `sdio_phase_summary` | 测速工具按会话/帧号去重抽样的接收及复制CRC均值，不是全量逐帧统计 |

`sdio.last_crc_us`为兼容保留，**本版是复制+CRC时间的别名**；不能直接与旧版纯CRC字段
比较。新工具不会把缺少`last_copy_crc_us`的旧固件日志当作融合路径测量。
以上各项仍不能与TCP发送时间简单相加：两个PSRAM槽位允许接收与发送重叠。

### 更新与复测

此次只需更新ESP32固件和PC测速脚本，**Zynq ELF、bit、XSA、BSP、FSBL及BOOT.BIN不变**。
图像显示软件本轮不改变发布上限，不靠重复图像增加计数。

在`esp32c61`目录手动更新（COM端口请自行核对，本文不是板测授权）：

```powershell
.\build.ps1 -Profile throughput -Action flash -Port COM3
```

保持与对照日志相同的下采样倍率，停止其他接收程序释放端口后测速：

```powershell
python .\pc_tools\sdio_benchmark.py --device-ip 192.168.3.27 --seconds 60 --log throughput_fused_60s_01.jsonl
```

该命令只启停上传，不启停扫描或高压。日志自动进入`esp32c61/logs`；使用新文件名，
避免覆盖基线。重点比较PC `payload_mbit_s`/`valid_fps`、CRC/ACK错误、内部堆低水位、
`tcp_yield_us`、`tcp_cooperative_waits`及`sdio_phase_summary`。
实际成像帧率还受完整源帧及图像处理影响，不能把本机单元测试当作板测提速结果。

### 本轮离线验证与产物

- 原生C回归通过，融合CRC覆盖黄金向量、非零种子、五档帧长、512 B增量、
  输入/输出不同对齐、短尾、范围哨兵、ROM选择与分配失败；同时以固件使用的`-O2`
  和严格别名规则复测。SDIO真实任务替身仍验证槽位所有权、CRC错误、停止和复位。
- TCP测试覆盖无阻塞时保留公平性休眠、有真实等待时避免重复休眠、短等待不抵扣、
  部分写入、五档长度、资源短缺、CRC回执、断网、取消及截止时间。
- Python全套181项（180通过，1项因缺少历史样本跳过），网页30项及原生C回归通过；
  最后补充的测速统计测试10项通过，覆盖旧会话残留及32位接收计数回绕。
- ESP-IDF 6.1 TCP/UDP × 默认/性能四组最终构建及size检查通过，应用分区剩余约37%。
  默认/性能静态DIRAM分别123923/135885 B，比检查点各增加292 B（其中BSS增加16 B）。
  这是静态占用，不是运行时堆余量；既有堆低水位风险仍需板测观察。
- 已检查C61反汇编：融合路径在循环外检查对齐，热循环使用32位LW/SW；
  源字加载与存储之间有CRC运算，不引入相邻加载/存储捷径；未对齐分支使用字节路径。
- 新增启动自检使用约516 B临时栈空间，不新增常驻堆缓冲；主任务栈配置仍为6144 B。

性能固件：
`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build-throughput-fast-socket\ultrasound_psram_udp.bin`，
966224 B，SHA256：`455F9C6574C40636D16DE49DCFBC41CC8C17F468912CA98ECB7F678CB53279B0`。

默认固件：
`D:\MyProjects\zynq_prj\ultrasound_B_mode_imaging\esp32c61\build\ultrasound_psram_udp.bin`，
964656 B，SHA256：`444258B90306B33D5998228A4A1D03B0E526A9B6E23FAE8E312F295BAEA49A4D`。

固件应使用所属构建目录的分区表、bootloader及flash_args；应用bin不是整片Flash镜像。
本轮构建、测试和反汇编记录位于`build/verification/transport_opt_20261021`。
未烧录、JTAG下载、覆盖BOOT.BIN、操作扫描或高压；未进行板级吞吐或48小时测试。
