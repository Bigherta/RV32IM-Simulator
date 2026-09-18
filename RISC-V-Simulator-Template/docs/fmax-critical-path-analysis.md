# fmax 关键路径分析（当前模板基线）

> 分析对象：`RISC-V-Simulator-Template` 当前源码（2026-09-18）
> 分析口径：将 `Register` 视为周期边界，将 `Wire`、桥接访问器、无状态 Module 和 `work()` 中写入
> `Register` 前的计算视为组合逻辑。

## 1. 结论与证据边界

当前**没有**综合网表、目标器件/标准单元库、时钟与 I/O 约束，也没有 STA 报告。因此本文只给出
**结构性候选路径**，不再给出未经 STA 支持的定量换算或确定的关键路径排名。真正的 fmax 结论必须来自指定目标、
综合选项和 SDC 下的 post-synthesis/post-route STA。

当前最值得送入 STA 逐条确认的结构是：

1. `DispatchArbiter` 的串行最老者选择，随后同拍读取 RS/PRF 并进入 ALU/AGU/BRU/MUL/DIV。
2. MUL 第三级 `sOld + cOld` 的 64 位最终进位传播加法器，以及其后的半字选择和结果槽写入。
3. SQ/LQ/BPU 多个字段分别调用同一组合扫描或预测函数，综合器是否共享公共子表达式尚不确定。
4. `needSquash`/`SquashTag` 跨前端、后端、CDB、缓存和仲裁器的高扇出与布线压力。
5. 若纯组合重构仍不够，是否增加流水站；这必须联合评估 fmax 提升和 IPC/周期数损失。

## 2. 当前模块与数据通路

`CPU` 构造器当前有 **27 次** `dcpu.add_module(...)` 注册，与 `CPU.hpp` 中的模块成员一致：

| 分组 | 已注册模块 | 数量 |
|---|---|---:|
| 状态、队列、执行与存储 | FetchUnit、IMEM、ICache、InstructBuffer、LQ、SQ、ROB、PRF、ALU、MUL、DIV、AGU、BRU、BPU、DCache、DMEM、RS、RAT | 18 |
| 独立结果总线 | AluCDB、LqCDB、MulCDB、DivCDB | 4 |
| 控制与仲裁 | MemArbiter、DispatchArbiter、IssueArbiter、FlushArbiter、DecodeUnit | 5 |
| **合计** |  | **27** |

M 扩展已进入真实流水线：MUL 使用 Booth radix-4、CSA 压缩和最终 CPA；DIV 是带状态寄存器和有界计数器的
多周期 SRT radix-4 单元，不是把除法循环组合展开。两者都有独立 RS 派发通道和独立 CDB。当前四条 CDB
路径为 ALU、LQ、MUL、DIV，均经过 squash 年龄门控，并分别送入 ROB/PRF；ALU 控制结果还参与 BPU/flush
相关路径，LQ 总线另携带 `memIndex`。

核心组合拓扑可概括为：

```text
RS/PRF registers
  -> DispatchArbiter oldest-ready selection
  -> selected RS payload and PRF value read
  -> ALU / AGU / BRU / MUL stage 1 / DIV receive
  -> destination registers

FU/LQ result registers
  -> four independent CDB squash guards
  -> ROB ready aggregation + PRF write ports (+ control consumers)

Fetch PC + folded-history registers + predictor tables
  -> BPU prediction / target selection
  -> FetchUnit, ICache and IMEM request decisions
```

## 3. 未解决的结构优先级

### 3.1 Dispatch 最老者选择与同拍执行

`src/StaticArbiter/StaticArbiter.cpp` 的 `selectOldest()` 逐槽维护 `WinResult w`。每次比较都依赖上一槽选出的
`valid/tag/index`，所以源码表达的是串行优先/年龄折链，而不是天然平衡的归约树。`aguSelect()` 使用同样
形态跨 load 与 store-address 池选择。

当前容量下：

| 通道 | 候选槽数 |
|---|---:|
| ALU / integer RS | 8 |
| AGU / load + store-address RS | 4 + 4 = 8 |
| BRU | 4 |
| MUL | 4 |
| DIV | 4 |

选择结果同拍继续索引 RS payload、读取 PRF value，再进入功能单元组合逻辑。这条“选择 -> 读操作数 -> 执行”
链是最强的结构性关键路径候选。`valid`、`rsIndex`、`robTag` 等输出 Wire 又分别调用选择函数；工具可能做
CSE，也可能因 lowering 方式保留重复逻辑，不能在 STA 前假定共享。

首选的零拍数方案是把 oldest-ready 选择写成保持旧槽 tie-break 的平衡锦标赛树，并将一次选择结果共享给
该通道全部输出。任何改写都必须保持相同 tag 时“早槽优先”和 squash 只压 `valid` 的现有语义。

### 3.2 MUL 最终 CPA 是综合问题

`src/MUL/MUL.cpp` 已有清晰的三级边界：

1. Booth radix-4 生成 19 行。
2. CSA 树将 19 行压缩为 `S/C` 两行并寄存。
3. `sOld + cOld` 完成 64 位最终进位传播，再按 MUL/MULH 类操作选择低/高 32 位写结果槽。

源码中的 `+` 只说明需要一个 64 位 CPA，**不能证明**目标实现是行波、前缀、FPGA carry chain，或某个固定
延迟。需要从综合报告确认该加法器是否落在最差路径、工具选择了什么结构，以及结果槽 first-free 选择和
动态写索引是否被串到其后。只有 STA 证实它主导时，才应比较显式前缀加法、分段 carry-select 或再加一级
流水的收益。

DIV 的 SRT 循环每拍只执行一轮并由 `loopTimes`/stage valid 寄存器隔开；最终结果阶段也有
`oldRegS + oldRegC`，应纳入同一份 STA 检查，但不能从 C++ 源码直接断言它比 dispatch 或 MUL 更慢。

### 3.3 SQ/LQ/BPU 的重复组合求值与 CSE

当前 Wire 只缓存自身 lambda 的值，不会自动让不同 Wire 共享 helper 调用：

| 位置 | 当前重复形态 | 风险与机会 |
|---|---|---|
| SQ data notify | 每个 store-value 槽的 8 个输出字段分别调用 `planDataForward()`；4 槽合计 32 次固定 16 项扫描 | C++ 仿真确定重复；RTL 是否 CSE 取决于 lowering/综合器 |
| SQ address notify | 8 个字段分别调用 `planAddressForward()` | 同一 CAM/优先语义可能被复制 |
| SQ load reply | `valid` 与 `value` 分别调用 `replyToLoadRequest()` | 可共享一个结构化结果 |
| LQ dispatch/CDB | `LoadDetect()` 在接线层调用 7 次，`CDBDetect()` 调用 4 次 | 固定 16 项 first-found 扫描可能重复 |
| BPU fetch output | `mid.predPC` 与 `mid.packed` 各调用一次 `predict()` | 当前最多两次完整预测；是否共享表读和命中逻辑不确定 |

这些函数内部均含固定容量扫描，其中 first-found/年龄选择带优先语义。合理方向是在所属 Module 内建立一个
组合 bundle/Inner Wire，一拍求值一次，再拆字段输出。收益首先是确定的仿真 CSE；面积与时序收益必须看
综合报告，不能预报倍率。

### 3.4 Squash 高扇出

`flushArbiter.needSquash` 与 `SquashTag` 直接接入取指、队列、RS、ROB、RAT、PRF、各执行单元、四条 CDB、
内存仲裁、DCache 和 BPU。逻辑本身通常只是门控或年龄比较，但全局扇出、跨区布线和复制后的年龄比较器
可能成为物理实现瓶颈。

优先让综合/布局工具报告 fanout、buffer tree、replication 和 routing delay。局部复制组合比较通常不改
周期语义；给 squash 加寄存器会改变精确 flush 时序，不能作为“免费”优化。

### 3.5 流水化与 IPC 的交换

若平衡选择树、共享组合结果和物理扇出优化后仍不满足目标，可评估：

| 候选切分 | 可能收益 | 架构代价 |
|---|---|---|
| Dispatch 选择 / RF 读 / EX 间加站 | 缩短当前最长的跨模块组合链 | ALU/AGU/BRU/MUL/DIV 可见延迟增加，旁路与 flush 需重定时 |
| BPU 预测结果寄存 | 隔离预测表读和取指决策 | 分支恢复与冷启动取指代价变化 |
| SQ 转发或 LQ 选择寄存 | 缩短 CAM/优先扫描路径 | load-use、违例检测和转发延迟变化 |
| MUL/DIV 最终 CPA 后移或再切分 | 降低单级算术深度 | M 指令结果延迟和 CDB 占用变化 |

最终指标应使用 `execution_time = cycles / achieved_frequency`，而不是只看模拟器的 `clock` 或只看 fmax。
加站属于架构改动；必须重新验证 x10、周期行为、分支/缓存统计和工作负载总执行时间。

## 4. 已解决项

| 项 | 当前源码状态 | 时序含义 |
|---|---|---|
| ROB ready 去重 | **2026-09-18 已完成。**旧 `seen[]/nSeen` 已删除。BRU、SQ 扫描窗口以及 ALU/LQ/MUL/DIV 四条 CDB 的请求 OR 入零初始化的 `std::array<bool, ROB_CAP> readyBits{}`，最后固定遍历 ROB，每槽至多写一次 `isCommitReady`。 | 删除动态长度线性去重链和经验数组上界；保留 Register 单写纪律。 |
| BPU folded history | 稳态预测直接读取 `fhIdx/fhTag8/fhTag7`；GHR shift 时增量更新，squash 时用编译期定界的 `refoldViewT` 重建。 | 预测热路径不再现场遍历 48 位 GHR。当前 tagged TAGE 表为 **4 x 512**（9 位索引）；T0 是独立的 1024 项 local base table。 |
| dead unresolved-store Wire | `sqHasOlderUnresolvedAddressStore` 已从模板源码消失，当前全树无定义或消费者。 | 不再存在误接后展开大规模冗余 CAM 的风险。 |
| 固定边界循环 | DCache 字节装配/写入均为固定 4 lane 加条件使能；FlushArbiter 插入定位和搬移均以 `FLUSHARBITER_CAP` 为常量边界；fold rebuild 使用模板常量边界。 | 循环可以展开为有限组合网络，不再由运行期长度决定 trip count。 |

完整的循环综合性分类、host-only 边界和仍待处理的 BPU `Plan::nTab` 可变边界见
[不可综合循环审计](../../docs/non-synthesizable-loops.md)。该问题是 RTL 生成前的综合性门禁，不应与“已由
STA 证明的 fmax 瓶颈”混为一谈。

## 5. 建议的测量顺序

1. 先确定 RTL/lowering 流程、目标 FPGA 或标准单元库、memory inference 策略和 SDC。
2. 生成 baseline 的 synth 与 place-and-route timing report，保存最差若干条 reg-to-reg 路径，而非只看摘要 fmax。
3. 单独报告 dispatch、MUL/DIV final adder、BPU fetch、LSQ forwarding、四 CDB 和 squash fanout path group。
4. 检查综合网表中 `selectOldest`、SQ/LQ helper 和两次 `predict()` 是共享、平衡还是复制。
5. 先做不增拍的平衡树/CSE/扇出优化；行为门禁使用双树 x10 与 clock 严格对齐及 18 用例回归。
6. 只有 measured STA 仍失败时再引入流水站，并同时报告新 fmax、cycles、IPC 和总执行时间。

## 6. 当前不确定性

- 尚无 measured STA；本文所有优先级都是结构判断，不是实测排名。
- C++ `Wire` lambda、动态数组索引和普通 `+` 如何映射到 RTL，取决于尚未固定的生成/综合流程。
- BPU、PRF、缓存和队列会被推断成寄存器、分布式 RAM、BRAM 还是 SRAM 宏，可能改变关键路径归属。
- 综合器可能对重复 helper 调用做 CSE、平衡归约或逻辑复制，也可能因模块边界/调试逻辑而不做；必须看网表。
- `StaticArbiter.cpp` 的注释仍写 AGU “12-slot”，但实际循环上界是 `LOADRS_CAP + STORERS_CAP`，当前常量为
  `4 + 4 = 8`。本文按可编译常量记为 8，注释视为源码中的待清理不一致。
