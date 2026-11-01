# Cache与AXI存储层次

## 2026-11-01 当前生产合同（优先于下方历史）

D$8 KiB/128组/2路/32 B、4 MSHR/8请求：Load不再因同组缺失一律停止探测，可命中其他way；deferred miss仍受组锁/旧信用控制，Store保持锁约束。匹配MSHR请求行的返回word可供owner提前返回，也可写入其他旧活动Load的raw/ready，检查Store风险及字内范围。请求行数据返回不等待脏victim写完成；victim保留、完整行安装及owner/MSHR释放仍受原完整事务条件约束。AXIData关键字起始轮转、接受沿寄存字、整行单拍填充保持。两树四构建原六项全匹配正式采用第17轮，见[基线与证据](hardware-baseline-prf-microbank.md)。

**2026-10-31生产RTL软件移植（本页以下旧合同均为历史）**：两软件树现将AXIData读行从owner请求word开始轮转，接受R沿保存寄存字响应、按原word位置收集整行；D$在字响应有效且身份/脏行完成/冲突/字内范围合格时提前响应Load，完整行在同一拍填充所有word bank、安装metadata并释放owner。补齐 StoreValue 非控制 ALU 旁路及真实离站复用后，原六项答案、实际退休和课程总周期全部匹配正式采用RTL，见[验收](hardware-baseline-word-production.md)。专用入口`../../sync/check_current_production_software.py`使用正式采用记录；r01四项周期差报告保留为修正前证据。

**2026-10-30当前合同**：双取指默认I$2包、AXIFetch4行；D$8 KiB、4 MSHR/8请求、AXIData8信用及Slave保持。未增加critical-word或访存口。仅原六perf验收，当前状态见[基线](hardware-baseline-fetch2.md)，不将软件计时当硬件PPA。

**2026-10-29当前合同**：主树已同步模板生产版8 KiB D$及空闲Load接受沿同步读，未采用critical-word部分行返回。
下面旧阶段的“主树保持4 KiB/未同步”仅为历史；当前验证见[同步记录](tree-sync-20261029.md)。

## 历史阶段记录

2026-10-27双向同步r02只改模板Load RS的WB派发/值和真实离站复用，D$/AXI/Slave源码与r01相同。
不把Load请求更早到达解释成Cache端口扩宽或critical-word已采用；验证及生产复验见
[双向记录](../../sync/MUTUAL-OPT-SYNC-20261027.md)。参考主树及RTL源码保持。

2026-10-27双向同步r01：生产模板D$移植RTL已有的`acceptLookupInner`合同：旧count=0、
readValid=0、真正接受非MMIO Load时，当沿使能同步SRAM并正常分配请求槽；下一拍才能取数。
旧valid-way、读身份、offset5/size2一起快照，Fill > Store > lookup及容量/运输不变。
本轮只改模板DCache两文件；参考主树/RTL保持。验收范围与生产复验见[双向记录](../../sync/MUTUAL-OPT-SYNC-20261027.md)。

**2026-10-27 分树配置**：参考主树保持下文 4 KiB；生产模板与 RTL 已采用
8 KiB＝128组×2路×32 B，index7/tag20/metadata21，每路 data bank 与 metadata SRAM 深度均为128。
请求槽仍8（含owner）、MSHR仍4，端口、AXI/Slave与既有整行 early restart 不变；
尚未采用 critical-word 部分行早返回。隔离验收及生产复验均已通过，见[采用记录](../../sync/ADOPT-BPU-DCACHE-20261027.md)。
本轮不覆盖参考主树周期表；模板使用自己的实测默认表。

2026-10-26模板四译码/四发射仅增加分配宽度及配套ROB/PRF/checkpoint/FQ/IQ容量，
不改Cache几何、MSHR、请求/返回端口、AXI信用或外部Slave。主树和默认配置保持。
四宽验证与尚未证明的RTL逐拍/PPA边界见[同步记录](../../sync/TEMPLATE-WIDE4-20261026.md)。

2026-10-25模板及随后RTL的可选双Load/双Store仅扩宽重命名时的LSQ分配，不扩宽AGU、Cache请求、返回或AXI信用，也不修改Slave服务及MMIO退出；主树仍单宽。见[模板接线](../../sync/DUAL-RENAME-WIRING-20261025.md)和[RTL验收](../../sync/RTL-DUAL-RENAME-20261025.md)。

独立译码宽度及FQ/IQ可变容量不扩大Cache/AXI。当前默认单取指I$请求4槽、AXIFetch4行，D$请求保持8槽；可选双取已采用2包/4字预算、控制流mask及最早CFI截断，2/1/2与2/2/2各25例三端周期相等。见[实施记录](../../sync/WIDTH-DECODE-SYNC-20261024.md)。

连续lookup只复用现有同步SRAM读口，不增加端口或修改主机信用；Load B/C已接入旧AGU提前请求与返回当拍CDB，详见[访存](memory.md)。双取恢复期提前取指只允许当前PC的非控制流hit，不增加端口或改变外部运输服务。

```text
core → ICache → AXIFetch ─┐
     → DCache → AXIData  ─┴→ AXIBus → AXI4-Lite → 外部Slave
```

## 当前组织

| 项目 | ICache | DCache |
|---|---|---|
| 容量 / 行长 | 1 KiB / 64 B | 8 KiB / 32 B |
| 组 / 路 | 16 / 1 | 128 / 2，tree-PLRU |
| 请求槽 | 单取4、多取2包 | 8，包含MSHR owner |
| 缺失 | AXIFetch整行运输 | 4 MSHR，同行Load合并，独立组hit-under-miss |
| 数据 | 同步SRAM，tag寄存器 | 每路8个128×32-bit字节写SRAM，metadata每路128×21 bit，valid独立 |
| 写策略 | 无CPU写口 | 写回、写分配 |

AXIFetch默认4行；AXIData读写共享8项描述符/payload，顺序环仍16项。外部Slave每通道16项、默认10拍，
不是旧IMEM/DMEM的固定20拍模型。完整参数见[规范](../../CPU-SYNC-SPEC.md)。

## DCache事件

双提交只拓宽ROB到LSQ的退休事件，不增加Cache请求、AGU、CDB或AXI带宽；SQ继续按序单路排空。

- 旧count=0、readValid=0时真正接受的非MMIO Load可在接受沿发同步读，仍占普通请求槽，最早下一拍响应；其它请求沿旧队列查询。身份/offset/size随读保存，不读同拍SRAM新写值。
- lookup候选提前计算，最终授权按**Fill > Store > lookup**；各way/bank和metadata独立选本地地址。
- 旧Load hit被实际响应消费时可同拍启动下一lookup，排除当前readIndex防止重复读取；无MSHR时也考虑移除响应后的最老请求。Fill/Store仍优先。
- 命中数据先选相邻word，再四路字节旋转并截取；一次read grant只提取一次raw，响应与缓存保存共享。
- 最低编号invalid way优先，否则按PLRU选victim；缺失冻结victim身份，保存dirty数据，锁住该组。
- 同行Load在首拍回填前可合并；Store保持接受顺序，Load不能绕过有冲突的旧Store。
- 每拍回填2个word，共4拍。Load首个合格回填拍从完整旧回复早返回一次；末拍才安装metadata/valid、
  消费行回复并释放owner。脏victim必须满足实际写完成资格。

请求控制、raw结果和victim行缓冲分离。payload在捕获后才可见，不因分配清无效宽数据；SRAM复位不清内容，
以独立valid屏蔽旧值。无效端口地址和无效行数据不裸比。

## 恢复与退出

错路Load持久标取消；已接受运输继续完成但不得形成有效架构写回。核心原存活窗口重放不阻断Cache/AXI。
MMIO退出绕过Cache，不污染行；正常课程结束不扫描全部dirty行，详见[访存](memory.md)。

## 验证

当前Cache/AXIData与RTL及独立字节参考通过60,000拍，并比较实际使能SRAM端口；
完整CPU逐拍、早返回、合并、脏逐出、满请求和恢复检查见[验收记录](../../sync/RTL-TO-SW-SYNC-20261018.md)。
这不是全输入或门级等价证明；PPA按[硬件评估快照](../../RISC-V-CPU-2026/docs/assessment-results.md)另报。

[历史原文](_archive/20261018/docs/cache.md)
