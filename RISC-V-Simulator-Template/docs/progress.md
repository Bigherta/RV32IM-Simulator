# 模板迁移回顾与当前状态

> `src/` 到 RISC-V-Simulator-Template 的阶段 0--9 迁移已于 2026-08-30 完成。
> 本文不再充当逐日任务清单，而记录终态架构、迁移成立的时序依据、仍有约束力的经验和当前唯一的数据通路遗留项。

## 文档入口

- 当前前端架构：[前端](frontend.md)。
- 当前乱序后端与 RV32M 实现：[后端](backend.md)。
- 当前 LQ/SQ、访存仲裁与内存一致性：[访存](memory.md)。
- 当前 ICache/DCache/IMEM/DMEM 层次：[缓存](cache.md)。
- 模板运行、框架 API 与常见错误：[使用说明](help.md)。
- 回归结果与性能口径的唯一基线：仓库根目录 [`../../docs/benchmarks.md`](../../docs/benchmarks.md)。
- 可综合循环的完整审计与分类：仓库根目录
  [`../../docs/non-synthesizable-loops.md`](../../docs/non-synthesizable-loops.md)。

## 当前结论

| 项目 | 终态 |
|---|---|
| 迁移进度 | 阶段 0--9 全部完成；没有仍在进行的“混跑阶段” |
| 状态更新 | 所有时序模块使用 `work()` 写 `Register::_M_new`，周期末统一 `sync()` |
| 组合通信 | 跨模块端口和中央总线使用 `Wire`；无状态仲裁器本身也是 `Module` |
| CPU 运行时 | 模块按值归 `CPU` 所有，以非拥有指针注册到 `dark::CPU`，每拍调用 `run_once()` |
| 旧机制 | `comb()`、`tick()`、模块快照成员和周期性 `memcpy` 已退出模板树 |
| 执行/写回 | ALU、LQ、MUL、DIV 四路独立结果总线；MUL 与 DIV 有专用 RS 和派发通道 |
| 恢复 | ROB tag、checkpoint、FlushArbiter 和各模块本地恢复状态共同完成整窗 squash |
| 验证 | 双树 `x10` 与等价改动的 `clock` 对拍，加 Release、`_DEBUG` 单写断言和基准表核验 |

迁移收官后的 DCache、MUL、DIV 等扩展都直接按同一 `Module + Register + Wire`
模型接入，不再引入第二套快照机制。

## 阶段 0--9 回顾

| 阶段 | 完成内容 |
|---|---|
| 0 | 引入模板基础设施，建立 `Register` 自同步和混跑验证方法 |
| 1 | 以 FetchUnit 定版状态、输入接线、桥接访问器和单写口手法 |
| 2 | 迁移 FetchQueue、DecodeUnit、ICache、IMEM 等取指生产模块 |
| 3 | 迁移 ALU、AGU、BRU 执行单元 |
| 4 | 迁移 PRF、RS，值与就绪状态统一由 PRF 派生 |
| 5 | 迁移 DMEM、LQ、SQ，保留访存顺序与前向语义 |
| 6 | 迁移 ROB，统一最广的 tag、提交、checkpoint 和恢复读口 |
| 7 | 迁移 FlushArbiter 与 BPU 两个状态 hub |
| 8 | Fetch/CDB/Mem/Dispatch/Issue 等组合决策及中央总线全部 Wire 化 |
| 9 | 删除快照运行时，切换到 `dark::CPU`，完成全模块 `work()+sync()` 收口 |

## 快照与 Register 的时序等价

迁移能逐模块进行的根本原因不是接口相似，而是两套机制表达同一个同步时序：

| 旧快照框架 | 模板框架 |
|---|---|
| 活体模块由 `tick()` 写 next-state | `work()` 用 `<=` 写 `Register::_M_new` |
| 周期开始把活体模块复制到只读快照 | 周期开始读取 `Register::_M_old` |
| 本周期所有模块只读快照 | 本周期所有模块只读已提交状态和由其派生的 `Wire` |
| 下一周期 `comb()` 的复制使写入可见 | 本周期末 `sync()` 令 `_M_new` 成为新的 `_M_old` |

所以两者都满足：本周期组合和时序决策只能观察上一拍已提交状态，本拍写入只能在下一拍被观察。
`Wire` 是组合函数，不是寄存器；它按拍懒求值并缓存，`sync()` 清缓存，不会凭空增加一拍。

最终 `dark::CPU::run_once()` 先调用全部模块的 `work()`，再统一 `sync_all()`。
正常注册顺序沿用了旧手写循环，正确性则不应依赖这个顺序：模块不能读取其他模块本拍的
`_M_new`。迁移期曾用打乱 `work()` 顺序验证这一性质；它现在是历史证据，不是现行回归项目。

`CPU::run()` 仍是 host-only 仿真外壳。它必须在 `run_once()` 前采样停机与排空条件，
保持旧框架“周期开始判停”的语义；HALT 后还要等 SQ、DCache 和 DMEM 排空，不能截断已提交 store。

## 历史：混跑期六条硬约束

以下六条只描述阶段 0--9 期间的过渡架构，**当前模板树已经没有混跑边界**。
保留它们是为了说明迁移为何安全，以及未来若重做类似迁移不能省略哪些步骤。

1. **模块必须全有或全无地转换。** `Register` 不可按普通对象 `memcpy`；包含它的模块若仍进入快照复制就是未定义行为，不能只改半个模块。
2. **状态所有权、快照提交和验证适配必须同步修改。** 当时每迁移一个模块，都要同时把 CPU 中的双实例收成单实例、删除对应快照复制、加入周期末 `sync()`，并让验证驱动比较已提交状态而非对象字节。
3. **非零初值必须显式设计。** `Register` 默认只产生零态；RAT 恒等映射、BPU 计数器和 LFSR seed 等不能依赖构造器偷偷写 `_M_old`，必须有复位/boot 周期及必要的 cycle-0 读视图。
4. **位宽与显式转换是接口的一部分。** `Register`/`Wire` 的布尔和整数转换是 explicit；环形指针、tag 距离、掩码与年龄比较必须先转成明确宽度，不能依赖宿主整型提升。
5. **整对象清零必须删除。** 对含 `Register`、`Wire` 或 lambda 的对象执行 `memset(this, ...)` 是未定义行为；状态要逐成员初始化，固定数组使用自身零初始化。
6. **每个 Register 每拍只能写一次。** 即使两次写入同值，`_DEBUG` 也视为两个物理驱动；push/flush、恢复/发射、写回/直写等冲突必须先归约成优先级或每槽 mux，再从唯一写点提交。

## 终态模块模型

- `Module<Input, Output, Inner>` 同时承载输入 Wire、输出 Wire 和内部 Register；模块实例自身就是 Input 子对象，接线必须落在实例上。
- `work()` 对应 `always_ff`：读 `_M_old`，计算所有 next-state，每个 Register 保持单写口；不使用提前 `return` 隐藏部分写集。
- `Wire` 对应 `always_comb`：构造/接线阶段赋一次 lambda，按拍懒求值；组合模块的 `work()` 可以为空。
- CDB、Mem、Dispatch、Issue 等无状态仲裁器没有 Register。把它们建成 Module 的原因是让 `sync()` 统一清 Wire 缓存，而不是给组合逻辑增加状态。
- checkpoint 是状态复制，不是普通通信总线。RAT、PRF、BPU 的 checkpoint 数组保留为本地 Register 状态，恢复时逐槽写回。
- IMEM/DMEM 的大存储阵列和缓存数据阵列不因模板化而机械变成 Register 阵列；端口、控制状态和拍级握手才进入模块同步模型。
- 位宽按值域收紧，并以 `static_assert` 守住容量假设；环形指针和 checkpoint id 不使用宿主 `int` 充当隐式硬件位宽。

## 现行验证政策

`reorder_test` 及其仓库内脚手架已经退役，不再是构建或验收依赖；不要在本仓库重建一份临时替代品。
现行闭环是：

1. 主树与模板树对同一镜像比较 `x10`；行为等价改动还必须逐拍比较 `clock`，差一拍就是回归。
2. `x10 & 0xFF` 与性能基线只认 [`../../docs/benchmarks.md`](../../docs/benchmarks.md)；文档内不复制易过期的总拍数。
3. Release 验证功能与时序，`_DEBUG` 验证 Register 单写、索引和结构不变量。
4. 纯表示变换不得改变 clock；有意的微架构改动必须单独说明原因、验证结果和基线更新，不能用“模板天然漂移”解释差异。
5. RV32I 课程镜像和含真实 `mul/div/rem` 的 RV32IM 镜像分别覆盖基本路径与扩展单元；长用例只作为里程碑，不把临时耗时写入本文。

`run_once_shuffle()` 仍是框架能力，也曾证明 Wire/Register 结构没有隐藏的模块执行顺序依赖；
但在测试目录清理后，它不再属于日常验证政策。

## BPU 双树 clock 对齐复盘

2026-08-31 的逐拍对齐最终确认了六项差异，其中五项是真 bug，一项是批准的冗余门控删除。
修复后双树恢复 `x10 + clock` 逐位一致，“clock 漂移是双端口训练的预期结果”这一旧判断作废。

1. **LFSR boot seed 被覆盖。** BPU commit 每拍无条件回写 LFSR，与 cycle-0 boot seed 同拍双写；Release 丢 seed，`_DEBUG` 直接触发双写。改成仅有步进时回写。
2. **参考树 FI 早训练写到了快照。** FQ 预译码产生的 BTB 早训练没有落到活体状态，JAL target/RET type 实际未持久化；改写真正的 next-state。
3. **模板跳转训练误写间接类型。** `updateJumpPlan` 把 `T_BTB_IND` 硬编码为 1；改为请求的 `isIndirect`，Target Cache 训练也补同一门控。
4. **模板过度压制 BRU 训练。** `needSquash` 曾无条件撤销同拍 BRU 训练，连比 squash 更老的分支也被丢弃；删除总闸，只保留年龄判断。
5. **squash 回卷基准与距离都错。** 回卷使用了 post-FI 的 align tail，并让 8-bit 环形距离按 32-bit 下溢；改用 pre-FI 基准，距离显式按 256 取模。
6. **RET 队头门控被删除。** 对齐调查证明它不是正确性保护，而是冗余的队头阻塞；两树统一接受删除，空 RAS/BTB target 的真正保护放回预测侧。

这次排查还确立了方法：从首个分歧拍向前追到首个状态写入点，不从最终 clock 差反推原因；
同拍端口碰撞也必须先证明写集合真实相交，不能凭抽象结构猜测。

## AGU 访存违例：重放必须使用真实 PC

store 地址由 AGU 解析后，FlushArbiter 会扫描更年轻、已经取值或正在取值的同址 load。
发生违例时，squash 对象是该 **load**，重放地址必须是 `ROB.pc[violTag & 0x3F]`。

曾经错误地读取 `ROB.predictedPC`。load 的 ROB 项不会由 INT/BR/UJ 发射路径填写该字段，
因此值恒为 0，机器会重定向到地址 0 并从启动区重跑。修复是给 FlushArbiter 接入真实
`robPC` Wire，并从违例 load 自身 PC 重取。一般化教训是：恢复目标要按字段的生产者审计，
“字段在 ROB 里存在”不等于“所有 uop 类型都会写它”。

## 标识域与 Register 纪律

### InvalidPhy 与 tag 单域

- `InvalidPhy = 0` 是整个物理寄存器域的唯一哨兵；P0 永不分配、永不建立 RAT 映射，真实 phy 为 `1..127`。
- `Operand.tag == InvalidPhy` 表示立即数/常量路径；PRF pop/push、RAT 写映射和源操作数解析都要守住非零断言。
- `RobTag` 是 7-bit epoch+slot 身份；64 项 ROB 的物理槽只由 `tag & 0x3F` 投影得到。
- 不再维护 `robIndex`、`SquashIndex`、`getIndexByTag` 等第二身份域。年龄比较始终在 RobTag 模 128 域完成，数组索引才切低 6 位。
- 用 tag 索引前仍要先做存活与 squash 窗口检查；删除冗余 index 不等于删除生命周期守卫。

### 零初始化

`Register` 的 `_M_old/_M_new` 从 0 开始，不能用普通构造器建立非零硬件复位值。
RAT 和 BPU 使用显式 boot；BPU 对 cycle-0 预测还提供 boot 常量视图，避免首拍先读到未提交的零表。

状态编码应尽量让零态就是安全空态。例如 RS 保存 `busy` 而不是“空闲为 1”的 `free`，
否则 cycle 0 的组合仲裁会把全零槽误当有效工作。boot 只能保护写路径，不能自动保护首拍组合读者。

### 单写与优先级

同值双写仍然代表两个硬件驱动。正确收敛方式是先收集意图，再用固定优先级或每槽 enable
产生唯一一次 `<=`。flush 通常压过普通推进；若参考实现依赖“先写后覆盖”，模板必须把该顺序
显式编码进 mux，而不是依赖 C++ 语句的最后写胜出。

## RAT 写回收敛

RAT 同拍可能同时遇到 checkpoint 恢复和新指令 rename。每个 `RAT_PRF[i]` 只能有一个写点：

- 恢复值以 checkpoint 的 `_M_old` 为 base；
- 若本拍 issue 分配目的寄存器且 `i == issueDest`，新映射覆盖恢复值；
- 这精确对应参考实现的“先 restore，后 `setRAT_PRF`”优先级；
- 本拍建立的新 checkpoint 捕获的是 **恢复前** RAT 快照，但目的槽写入新 phy，复现旧框架从周期初快照复制后再覆盖目的槽的语义。

因此实现按槽计算 `destHit ? issuePhy : (restore ? checkpointValue : hold)`，并让 checkpoint
写口独立捕获 `destHit ? issuePhy : preRestoreValue`。这既保留拍级语义，也满足 Register 单写断言。

## 状态与容量收敛

### TAGE 1024 -> 512

四张 TAGE tagged table 的索引宽度由 10 降到 9，每表 1024 项缩为 512 项；T0 及其他结构不随之缩小。
采纳的 W=9 方案使 tagged-table SRAM 约减半（约 28.7 Kb），全量总 clock 与 pi 均略有改善，
已观测单例最差变化约为 `+0.35%`。W=8 虽继续省面积，但单例退化更明显，未采用。

折叠历史的索引掩码、移位、旋转和 squash 重算必须与 9-bit 宽度一起修改；尤其手写 rotate/fold
需要显式 mask。当前性能数值以根目录 benchmark 文档为准，不在这里固化总拍数。

### FlushArbiter needSquash

四个请求槽删除了逐槽 `needSquash` Register。BRU、CDB、AGU 三条插入路径只会插入真实 squash，
所以不变量是 `requests[i].valid => needSquash`。清除只撤 valid，压缩只搬移有效槽，广播时
“存在最老有效槽”即可组合生成 `needSquash=1`。该压缩省去 4 bit 状态和相应写驱动，不改变协议。

### DIV busy

DIV 删除独立 `busy` Register，逐拍不变量为：

`busy == prepareValid || loopValid || fullAdderValid`

普通输入进入 prepare，迭代在 loop 中推进，最后进入 full-adder；特殊值快路直接产生 result。
`canAccept()` 直接检查三个阶段 valid 与 `resultValid`，因此无需再维护一个会重复表达同一 FSM
占用状态的 bit。flush、接收和结果排空的写冲突也随之减少。

IMEM 的占用计数采用同一思想：当 `valid[]` 与环形窗口有严格不变量时，occupancy、full 和
head-ready 可由位图归约得到，不必保留容易与槽状态失配的冗余计数 Register。

## 固定归约与可综合循环

### ROB ready 位图

ROB 原先用动态长度 `seen[]` 线性查重，既有经验上界越界风险，也不符合固定硬件结构。
现在所有 BRU、SQ、四路 CDB ready 请求先幂等 OR 到 one-hot 风格的零初始化逐槽位图
`std::array<bool, ROB_CAP> readyBits{}`，再固定遍历 `ROB_CAP`，每槽至多执行一次
`isCommitReady <= true`。`{}` 零初始化是承重条件；遗漏它会产生幽灵 ready 和双写。

### 固定四 lane 字节通路

DMEM/DCache 中 `i < n_bytes` 的运行期循环全部改为固定 `i < 4`，`i < n_bytes` 只作为 lane enable。
这样 1/2/4-byte load/store 明确综合为四路字节网络，而不是数据相关回边；1B/2B 符号扩展也改为
无符号显式掩码，消除了 `1 << 31` 一类宿主 UB。

### 固定容量 FlushArbiter

有序插入不再使用 `while (pos < w)`。实现固定扫描 `FLUSHARBITER_CAP=4`，用 `scanning` 在首个
不满足年龄关系的槽后冻结决策；后移也固定遍历四槽，`i > pos` 仅作为写使能。
模板同拍请求仍按 BRU -> CDB -> AGU 的既定优先顺序合并，最后每个槽只有一个 Register 写点。
固定数组的动态索引会综合为 mux，不需要为了“消灭动态索引”再造另一套可变循环。

真正数据相关的 DIV 迭代没有强行展开，而是用有限位宽计数器和 FSM 每拍复用一次 SRT 数据通路。
这也是可综合的：不可接受的是宿主 `while` 在一拍内跑到收敛，不是多周期硬件本身。

## 当前唯一活跃关注项：BPU Plan::nTab

模板 BPU 的 `Plan` 用 `tab[64] + nTab` 打包本拍稀疏表更新，合并与提交处仍有若干
`q < nTab` / `m < merged.nTab` 的运行期边界循环。容量 64 虽然固定，但源码遍历的是
“实际用了多少项”，综合形状仍是数据相关回边；这是当前数据通路循环审计剩余的 C-7 项。

目标改写是固定 64 路遍历或固定来源端口，加逐槽 valid/one-hot enable 和明确的覆盖优先级，
复用 ROB ready 位图的“固定归约、唯一写回”原则。改动必须同时守住 BHT 同槽融合、BTB/CDB
覆盖顺序、bank tick 衰减与更新覆盖、LFSR 步进和双训练口共享旧快照等 BPU 端口语义。

在该项完成前，不应把 `nTab <= 64` 误当成“循环已经可静态展开”；详细位置、分类和验收标准见
[`../../docs/non-synthesizable-loops.md`](../../docs/non-synthesizable-loops.md)。
