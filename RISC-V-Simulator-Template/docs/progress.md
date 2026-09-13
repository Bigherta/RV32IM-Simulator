# 迁移进度：当前框架 → RISC-V-Simulator-Template

> 目标：把 `src/`（memcpy 快照 + comb()/tick()）逐模块迁移到模板框架
> （`Register`/`Wire`/`Bit` + `Module<In,Out,Inner>` + `sync_member`）。
> 配套文档：`docs/output.md`（全模块 Output 清单 / 总线归属表，已含 RS 去值化修订）。
> 每步完成 = **golden(x10+clock，取自 `docs/benchmarks.md`) 严格一致 + reorder 全排列一致**；pi 约 8 分钟可放里程碑跑。

## 核心等价（迁移前提）

| 当前框架 | 模板 |
|---|---|
| `CPUstate.X`（tick 写，new） | `Register::_M_new`（`field <= v`） |
| `XModule` 快照成员（comb() 开头 memcpy） | `Register::_M_old`（读取默认值） |
| `comb()` 的 memcpy（时钟上升沿提交） | 模块内 `sync()`（`_M_old = _M_new`） |
| `input.YModule.getFoo()`（跨模块读） | Input `Wire` lambda 读 Y 的 `_M_old` |
| tick()（读 Input、写 CPUstate.self） | work() |
| 17 阶段 reorder_test | `run_once_shuffle` |

## 混跑可行性：并存，但它是"有严格边界条件的迁移中间态"

**为什么成立（时序等价）**：转换模块 X = 删 CPU 里快照成员 `XModule`、Input 引用重指单一实例 `CPUstate.X`。

| | 未转换（memcpy 双缓冲） | 已转换（Register 自同步） |
|---|---|---|
| 周期内可见 | 快照成员（comb 开头 memcpy） | `_M_old`（上一周期末 sync 提交） |
| tick 写 | `CPUstate.X` | `field <= v`（写 `_M_new`） |
| 提交 | 下一周期 comb() memcpy | 周期末 sync() |

两者"周期内可见 = 上一周期末"一致；跨模块读：未转换消费者读已转换生产者走 `input.X` 访问器（=`_M_old`），已转换消费者读未转换生产者走快照成员——都等于上一周期末。**机制可靠，混跑行为等价**。通信层不必立刻用 Wire：转换模块保留 const 访问器作为"桥"，未转换消费者照常调用；Wire 只在**生产者与消费者双方都转换**后引入。

**揉进有 6 条硬约束（每条都真实存在）**：

1. **模块粒度全有或全无**：Register 禁拷贝，含 Register 的模块被 memcpy 即 UB。不能半转换。
2. **每次转换动三处**：`CPU.hpp`（删快照成员、重指 Input 初始化）、`CPU.cpp`（comb() 删该模块 memcpy 行、run() 末尾加该模块 sync）、`test/reorder_test.cpp`（`runStage` 引用改单实例 + 周期末补 sync + 状态比较不能用 memcmp 对含 Register 的模块）。
3. **Register 初值问题**：Register 只有默认构造（`_M_old=0`），无法构造时设非零初值——RAT `RAT_PRF[i]=i`、BPU 表 `=1` 这类初值需"复位周期"或显式 reset 逻辑（模板本身空缺，BPU/RAT 转换时要专门设计）。
4. **算术摩擦**：Register 只有 explicit 转换，`(tail+1)&0xF`、`(head^next_tag)==0x40`、`ROB::isOlder` 等 tag/指针运算都要 `static_cast`，ROB/SQ/LQ/PRF 热路径改动量大。
5. **memset 构造器必须重写**：FQ/LQ/SQ/ICache/FetchUnit/IMEM 的 `memset(this,...)` 对含 Register 的类是 UB，改逐成员显式初始化。
6. **单赋值是硬性纪律**：Register `_DEBUG` 对同周期双写 assert，SQ push+flush 同写 tail、ALU push+flush 同写 slotValid 这类须先重构。

**结论**：并存可行且是逐模块迁移的唯一安全路径（每步 golden+reorder 独立验证）；但它本质是**过渡态**而非长期架构——同时维护两套状态机制/同步路径/混合 Input 引用的成本长期看比任何单一架构都差；"揉进"不免费，每揉一个模块付 1–6 的代价并保持全绿。若最终不走全量模板，揉进几个模块只会背上两套机制复杂度，不建议。

## 总原则

1. **先简单后复杂、生产者先于消费者、总线最后**。
2. **每模块转换天然含单赋值重构**：Register 调试态（`_DEBUG`）对同周期双写 assert，强制把 SQ push+flush、ALU push+flush 等"同字段两写"改成"算一次写一次"（RTL 纪律）。
3. **transition 期 tick 签名不变**（`tick(const XInput&, systemState&)`），reorder_test 结构沿用；但每转换一个模块，reorder_test 需同步适配（`runStage` 引用改单实例、周期末补该模块 sync、状态比较避开含 Register 模块的 memcmp）。仅收尾阶段切换为 work()。
4. **Checkpoint 机制不迁移为 Wire**：ckpt_id + RAT/PRF/BPU 本地快照数组是"状态复制"，用 Register 数组存、恢复时逐槽 `<=`。
5. **`getIndexByTag` 存活守卫原样保留**（headTag 守卫 + squash 检查），不因改 Wire 放宽。

## 阶段列表与进度

### 阶段 0 — 基础设施（一次性）
- [x] vendor 模板头文件（`bit/concept/debug/register/wire/synchronize/reflect/module/cpu` 等）到 `src/include/` 或接入 include 路径；确认 C++20 + g++-12+
- [x] 确立"混跑"存储模型（已转换单实例 + 未转换双缓冲）
- [x] `CPU::run()` 末尾加 `sync_all()`（先手写已转换模块列表）
- [x] 建立每步验证门禁：`cmake --build build` + 短用例 + reorder；里程碑跑全量 18 用例（另建 `-D_DEBUG` 配置压单赋值 assert）
- 验证：基线 `./test.sh` 18/18 + `reorder_test` 通过后才开工

### 阶段 1 — 定版模块：FetchUnit
- [x] 状态字段 `programCounter:32`/`haltFetched:1` → `Register`
- [x] Output Wire：`getPC`/`isHaltFetched`；删 comb() 的 memcpy 行；Input 重指（实际定版：Output 直接暴露 Register，消费方 explicit cast 读）
- [x] tick 改单赋值 + `<=`；run() 末尾 sync
- 验证：golden + reorder 通过（确立全部转换手法）

### 阶段 2 — 取指侧生产模块
- [x] FetchQueue（entries/head/tail → Register；headRaw/pc/PredictedPC/CkptId/full/empty → Output Wire）
- [x] DecodeUnit / InstructQueue（head* 字段 → Output Wire；消费方 FQ/IssueArbiter；实际定版：合并单一模块，InstructQueue 类删除，head* 以 const 桥接访问器输出，IssueArbiter 零改动）
- [x] ICache（blocks 保持普通数组或 Register 数组？；requestBuffer/head/count → Register；hit/return* → Output Wire）
- [x] IMEM（IMEMreqs/head → Register；getReturn/isReturnReady/isRequestFull → Output Wire；Memory 保持外部；实际定版：data 全 Register、head:4bit、remainCycle:2bit；**count 寄存器删除**（手法 #10）——occupancy 由 valid[] popcount 派生、full=AND、isReturnReady 用首槽位，squash 清全槽 valid，周期首 `assert(windowContiguous())` 兜底）
- 验证：每模块 golden + reorder

### 阶段 3 — 执行单元
- [x] ALU（outputBuffer/slotValid → Register 数组；headValue/headRobTag/headIsControl/isFull/isEmpty/isValid → Output Wire）
- [x] AGU（同 ALU；headMemIndex → Output Wire）
- [x] BRU（同 ALU；headPCFrom/headPCResult → Output Wire）
- 验证：每模块 golden + reorder（CDB 候选/转发源就绪）

### 阶段 4 — 值通路核心
- [x] PRF（PhysicalRegs/freeList/headSeq/tailSeq → Register；isReady/getValue/isOperandReady/getOperandValue/freeList* → Output Wire，扇出最广）
- [x] RS（槽数组 → Register；src1/src2/data（Operand）字段；tryAlloc* → Output Wire；RS 去值化后值一律经 PRF）
- 验证：每模块 golden + reorder

### 阶段 5 — 存储顺序
- [x] DMEM（busy/bufferValid/MemExecution/MemOutputBuffer → Register；isBusy/isReady/LoadReturn → Output Wire；Memory 外部）
- [x] LQ（LQqueue/head/tail → Register；CDBDetect/LoadDetect/isReadyToCommit/getAddress/getValue/... → Output Wire/数组）
- [x] SQ（SQqueue/head/tail → Register；planDataForward/planAddressForward/replyToLoadRequest/canDispatchLoad/hasOlderUnresolvedAddressStore → Output Wire）
- 验证：每模块 golden + reorder

### 阶段 6 — 核心：ROB
- [x] ROBEntry[64]（多字段）→ Register 数组；head/next_tag/haltCommitted/haltRd → Register
- [x] 全部 get* / isHead* / headType / getIndexByTag → Output Wire/数组；**一次性重指所有消费方 Input**
- 验证：golden + reorder（读方最广，改动面最大）

### 阶段 7 — 双 hub
- [x] FlushArbiter（requests → Register；arbitResult → SquashInfoWire 广播总线；receive/clear 保持状态内部）
- [x] BPU（大表 globalPHT/LHT/localPHT/selector/BTB/RAS/alignQueue → Register 数组；predict/getNextCkptId → Output Wire；GHR/RAS 推进与恢复保持状态内部）
- 验证：每模块 golden + reorder

### 阶段 8 — 无状态仲裁/总线 Wire 化
- [x] FetchDecision（BPU.predict + FetchUnit/FQ/ICache/IMEM Output 组合）
- [x] CDBArbiter → cdbOut（ALU/LQ Output 组合）
- [x] CDBBus（cdbOut 派生，`{lsqSetCDB, memIndex}` 仅 LQ）
- [x] DispatchArbiter（RS/ALU/AGU/BRU/ROB/PRF Output 组合，PRF.isOperandReady 判就绪）
- [x] MemRequestArbiter → MemDispatchDecision（LQ/SQ/ROB/DMEM Output 组合）
- [x] IssueArbiter → issuePacket（Decode/ROB/RS/RAT/PRF/LQ/SQ Output 组合；多字段超 32bit 拆逐字段 Wire）
- 验证：golden + reorder；comb() 总线从"读快照"整体切到"Wire 引用"

### 阶段 9 — 收尾
- [x] 删 comb()；删全部快照成员；Input 引用全部重指单一实例
- [x] tick() → work()；CPU::run() → `run_once`/`run_once_shuffle` 风格
- [x] reorder_test 切换为模板乱序验证
- [x] 全量 18 用例 + reorder；更新 `AGENTS.md` / `docs/output.md` / README
- 验证：18/18 golden + reorder 全排列一致

> ⚠️ 上列阶段 2–9 的"reorder / reorder_test"验证项已随 2026-09-10 `test/` 清理退役；
> 现行验证闭环 = 双树 x10+clock 逐位一致 + `docs/benchmarks.md`（`result`/`cycles`）。

## 关键风险与注意

- **ROB/FlushArbiter 的 `getIndexByTag` 存活守卫**：headTag 守卫 + squash 检查在 Wire 组合函数里原样保留。
- **Checkpoint（ckpt_id）**：RAT/PRF/BPU 本地快照数组是状态复制，用 Register 数组、恢复逐槽 `<=`，不走 Wire。
- **Memory（IMEM/DMEM）**：128KB 存储数组保持外部普通数据，不进 sync（Register 数组 sync 开销大且语义不符）。
- **单赋值**：Register `_DEBUG` 双写 assert；迁移时先重构同周期双写（SQ push+flush 同写 tail、ALU push+flush 同写 slotValid 等）。
- **宽结构体拆分**：SquashInfo/CDBOutput/CDBBus/FetchDecision/MemRequest/IssuePacket/StoreNotify/LoadResponse/PredictInfo/OperandInfo/LineReturn 拆逐字段 Wire 聚合体；聚合体 sync 上限 ≤14 成员，超了嵌套。
- **混跑期读一致性**：comb() 已转换模块读 `CPUstate.X`（=`_M_old`）、未转换读快照成员，值一致；每步转换后立即 golden 兜底。

## 进度记录

- 2026-08-22：`docs/output.md` 完成（全模块 Output 清单 + 总线归属表，含 RS 去值化修订）；本文件建立迁移顺序与进度跟踪。
- 2026-08-22：补入"混跑的边界与代价"（并存 = 过渡态；6 条硬约束：全有全无/三处联动/Register 初值/算术摩擦/memset 重写/单赋值）；修订总原则第 3 条（reorder_test 每模块需同步适配）。
- 2026-08-23：阶段 0/1 收口。`sync_all()` 手写列表落地（FetchUnit）；基线验证 17 用例 x10+clock 与参考/golden 严格一致（pi 留里程碑）。
- 2026-08-23：阶段 2 前两项落地——**FetchQueue**、**DecodeUnit/InstructQueue** 转模板：
  - 形态照搬 FetchUnit 定版：Input=Wire 组合体（wire() 接线在模块实例上）、状态 Register、work() 单赋值 `<=`、squash 后 return 保留、周期末 sync_all()；
  - 条目数组换 `std::array`：FQ=`std::array<FQEntry,8>`{raw/pc/predictedPC:32, ckptId:8}，IQ=`std::array<UopEntry,16>`（13 字段 ≤14 成员上限）；
  - 对外组合值保留 **const 桥接访问器**读 `_M_old`（签名不变）→ 未转换消费者零改动：IssueArbiter 13 个 head* 原样、CPU.comb/run 原样、Decoder.cpp 随 DecodeUnit 合并重写；
  - 新坑记录：Wire/Register 的 `operator bool` 是 explicit，`&&/||` 里必须 `static_cast<bool>`；
  - 验证：17 用例 release 与 `-D_DEBUG` 双门禁 x10+clock 严格一致（快 9 例每子步骤 + 中档 8 例收尾；pi 留里程碑）。
- 2026-08-23：位宽收紧（手法 #9 确立）：FQ ckptId 8→**6**bit（=log2(CKPT_CAP)）、head/tail 8→**3**bit（=log2(FQ_CAP)）；IQ ckptId 8→**6**bit、head/tail 8→**4**bit；对应 Wire 同步。纯头文件模板参数改动，.cpp 零改动（int_type 写入裁剪 + static_cast 读取链保证域内无损）；旁置 `static_assert(CAP==...)` 守卫。11 用例（快 9 + hanoi/superloop）release 与 `-D_DEBUG` 双门禁严格一致。
- 2026-08-23：阶段 2 第三项落地——**IMEM** 转模板（data 全 Register 方案）。审查修正 5 处：count 位宽 4→**5**bit（0..16 溢出 bug）、squash 补清全槽 valid（残留倒计时槽与回绕 push 双写/脏 data 风险）、wire() 补 icacheHit 门控+`&~0xF` 行对齐、`(head+count)` 等长运算改显式 uint32、sync_all 补 IMEMModule.sync()。17 用例 release+`-D_DEBUG` 双门禁严格一致（pi 留里程碑）。
- 2026-08-23：IMEM **count 寄存器删除**（手法 #10 确立）：valid[]⟺窗口不变式下 occupancy=popcount 派生、isRequestFull=AND、isReturnReady 缩为首槽位，pop/push 对 valid 的写即增减计数；同拍 pop+push 的槽位恒等式与连续性逐路径归纳证明，并加周期首 `assert(windowContiguous())` 运行时兜底（_DEBUG 下 11 用例每拍校验零触发）。净效果 -5FF、消掉 pop+push 同拍净写。release 复验 gcd/superloop/basicopt1 严格一致。

## 2026-08-31 双树 clock 逐位对齐战役（17/17 收官，pi 待重验）

**结果**：17/17 用例 x10 + clock 双树逐位一致（gcd 880 / array_test1 342 / array_test2 379 /
multiarray 2031 / lvalue2 141 / naive 73 / expr 1011 / manyarguments 149 / statement_test 1706 /
hanoi 176279 / basicopt1 532885 / bulgarian 369515 / qsort 1226385 / magic 779059 /
queens 843711 / superloop 636510 / tak 1967314）；build-assert 零触发；reorder_test
gcd/lvalue2 各 20 轮全排列一致（tak 未入窗，机制同证）。多数 clock 较旧基线**变优**
（gcd 885→880、basicopt1 533486→532885、bulgarian 371044→369515、magic 787282→779059 等）。
golden 双树统一重写为新基线；pi.golden 保留旧值暂判 FAIL（轨迹变更待重验 ~44min）。

### 根因链（探针 → trace diff → 逐 hop 归账，共六项）

方法：对称插桩（PR/TRAIN/FI/ALLOC/SQRES/GA/DISP/SQF 走 util.hpp debug 主题），
跑 gcd/hanoi/bulgarian 找**首个分歧拍**，逐 hop 回溯到状态写入点。验证后全部拆除。

1. **模板 LFSR boot seed 被 commit 覆盖**（gcd ALLOC 探针首分配选表不同锁死）：
   BPU commit 块 `dir.lfsr <= lfsr` 每拍无条件执行，cycle-0 读 _M_old=0 与 boot 块
   `dir.lfsr <= LFSR_SEED` 同拍双写（Release 第二写胜出→seed 丢失→victim-pick 全偏；
   _DEBUG 即 register.h:38 断言真身）。改使能写（仅 lfsr_steps>0 拍写回）。
2. **主树 FI 早训练 BTB 写死快照**（ret 位 PR 探针差异定位）：tick() 在 comb 快照副本上
   执行，对 BTB 的九行赋值从未持久化，主树 BTB 早训练（JAL target/RET type）
   一直无效；改写 CPUstate.BPUModule.tgt.BTB。属性能缺陷修复（早训练本为设计意图）。
3. **模板 updateJumpPlan 硬编码 T_BTB_IND=1**（hanoi PR ind 位差异定位）：主树写
   isIndirect 参数（CDB 口 Cand 恒不设 = false）；TC 训练门同步补 req.isIndirect
   （主树 CDB 口恒 false → TC 永不训练，模板对齐）。
4. **模板 BPU 无差别 needSquash 抹除 trBru**（bulgarian TRAIN 事件缺失定位）：
   主树仅有 !needSquash 与 isOlder(brTag, squashTag) 解码门；无差别抑制把广播拍上
   更老分支的训练一并抹掉（比 squash 点老的训练本应保留）。删除，仅留 isOlder 门。
5. **模板 squash 回卷两处偏差**（hanoi FI tm=5/4 与 bulgarian SQRES dist=-255 定位）：
   a. 回卷重放范围基准用 post-fi 镜像 alignTail（本拍 FI 日志项被误回卷）→ 改 pre-fi
      基准 alignTailPreFi（主树用快照，语义 = 本拍 FI 事件不被回卷）；
   b. dist = curTail - base 用 uint32 减法，0-255 下溢为 0xFFFFFF01 → 重放 32 条垃圾
      日志（主树 uint8 模 256 = 1）→ 改 (curTail - base) 掩码 0xFF。
6. **模板 IssueArbiter RET 门控删除（D3，已批准）**：ra=0 野取指真因是 BTB/RAS 守卫缺失，
   门控属多余队头阻塞（bulgarian MDP 时序偏移实证）。

### 旧结论废弃

- "squash 广播 +3 拍"：DISP 149/152 周期戳取自野取指（BTB target=0）未修时代，全在下游
  污染区；修复后两树同拍（N 执行 → N+1 检测入队 → N+2 广播）。FlushArbiter 保持 3 拍队列，
  **B 方案拍平取消**。
- "TAGE ctr +2 vs +1 同拍双口分歧"：现端口切分下 update（bru）与 updateJump（cdb）写表
  集合不相交，BHT/BTB 双口碰撞两边等价（BHT 融合 din / BTB cdb 胜出整字写），零发生。
- "clock 漂移为预期"：作废，clock 逐位对齐恢复为硬门禁。

### 主树同轮改动（已报备）

- src/BPU/BPU.cpp：① predict() 加 isRet 且 RAS_top==0 → btbHit=false、taken=false
  （空栈 RET 回退 pc+4，堵 BTB target=0 野取指，两树同款）；② tick() FI 块 BTB 早训练
  改写 CPUstate（见根因 2）。
- ROB tag 域单一化（参照模板树）：ROB::idx / ROB::getIndexByTag / SquashInfo.SquashIndex /
  IssuePacket.robIndex 全删，tag & 0x3F 直取；涉及 ROB/Arbiter/BPU/PRF/LQ/SQ/RAT/
  IssueArbiter 及 IssueArbiter.hpp、ROB.hpp、common.hpp 死线删除。
- 上述主树行为变化均已过 x10+clock 门禁。

## 2026-09-02 DCache 迁移收官：multiarray 死循环根因修复（AGU 违例 squash 目标错误）

### 现象与定位链
- 现象：DCache Step 1 迁移完成后 17/18 用例（非 pi）x10+clock 与主树逐位一致，
  唯 multiarray 死循环（程序跳回 .rom 重启，输出 0，永不 halt）。
- 排除 BPU：双树 1178（j 循环退出分支 bge）的 predict/update 在 cycle 序列上只差
  1 拍时钟偏移，t0idx/t0v/lht 演化序列完全一致（94→95→93→89→81→65），BPU 非根因。
- 决定性 trace：提交 PC 轨迹 + squash 轨迹，捕获 `SQ cyc=1637 ->0`——一次重定向到
  **PC 0** 的 squash，机器从 .rom 重启无限循环。

### 根因
FlushArbiter（DynamicArbiter）AGU store-load 违例处理段，重定向目标误用了
`rob.robPredictPC[violTag]`：load 类 ROB 表项只有 INT/BR/UJ 发射者会写 predictedPC，
**load 项恒为 0** → squash 到地址 0。主树用 `ROB::getPC()`（真实 fetch PC）。

### 修复
- `FlushArbiterInputROB` 新增 `robPC` Wire 数组（真实 PC 输入线，与 robPredictPC 并列）；
- CPU.cpp 接线 `flushArbiter.rob.robPC[i] = ROBModule.entry.pc[i]`（ROB 已有该
  Output Wire，读 ROBqueue[i].pc 寄存器）；
- 违例段 `viol.SquashPC = rob.robPC[violTag & 0x3F]`。
- 语义：违例重放从**违例 load 自身 PC** 重新取指（与主树一致）。

### 诊断代码清理（同轮）
- 模板树：CPU.cpp（FT/SQ/TRC/DIAG/g_diagCycle/g_trcN/8000 拍提前终止）、BPU.cpp
  （RAS/PRD/TBRU/TCDB/UP/g_rasSeq）、DynamicArbiter.cpp（BRU/JLR/AGU）全部移除；
  DCache 临时全局计数器改为正式 `statHits/statMisses/statWritebacks` 成员，
  CPU.cpp 结尾按 `TOPIC_DCACHE` 打印命中率汇总（对齐主树 debug 体系）。
-   主树：BPU.cpp/CPU.cpp 诊断 git checkout 还原；另删除 CPU.cpp 中引用已删除 API
  （DCache getHitCount/getMissCount/getWritebackCount）的死调试块（该块编译必炸）。

## 2026-09-02 收官：官方判题 19/19 全过 + 最终清理

### 判题结果（用户 WSL Release 构建，官方判题表）
- **19/19 passed，x10 全部 Golden OK**，含 **pi = 137**（墙钟 5355.6s，≈25.7K 拍/s）。
- 非 pi 17 例 + wb_test 的 x10+clock 与主树逐位一致（详前表）。
- pi 的 clock 与主树（137590156）逐拍比对为低风险遗留项：判题表 pi 行 Clock 显示 0
  属解析显示问题；确定性模拟 + 其余 18 例 clock 全对支撑高置信。

### 最终清理（判题通过后）
- 移除 3 处**无门控 DBG 探针**（multiarray 调试遗留，硬编码地址/阈值）：
  LQ.cpp `LQsn fwd`（addr==0x12E4）、LQ.cpp `LQresp`（addr==0x12E4）、
  ROB.cpp `robwrite`（pc>0x2000）。CPU.cpp 结尾 `std::cout` 为 x10 结果输出，保留。
- 清理后等价性冒烟：multiarray 115/1887、gcd 178/869、statement_test 50/1669、
  wb_test 1/55 逐位一致，stderr 恢复纯净（无 VERBOSE 时仅 0 字节）。
- 根目录 `code`：用户 WSL 判题构建的 **ELF Release**（构建于全部语义改动之后；
  与清理后源码唯一差异 = 3 处 stderr 探针，无语义影响），下次判题重建自然同步。
  Windows 侧等价性验证二进制：`.cache/tmpl_final.exe`（mingw -O2）。

### 环境经验（选测试环境必读）
- WSL Release（判题配置）：≈25.7K 拍/s；MSYS g++ -O2 手动构建：≈2.5K 拍/s。
  同一代码 **10× 差距**（Wire lambda 密集代码的平台代码gen差异）。
- pi（137.59M 拍）在 WSL ≈1.5h、MSYS 需 ~15h——**长用例一律 WSL 判**；
  MSYS 侧只做快速冒烟与 -D_DEBUG 断言验证。

## MUL 迁移落地（2026-09-07，接线收官）
- **64 位中间结果定版**：`max_size_t=uint32_t` 只约束存储类型；work() 内 uint64_t
  局部量做全部组合运算（Booth 行/CSA 树/CPA），跨拍状态拆 lo/hi `Row64`
  双 `Register<32>`（19 行 + S/C；`expected` 自检链按决议删除）。
- **work() 六段**：采样 / stage3(CPA+槽扫描，remove-先行复用语义) / stage2(CSA树)
  / stage1(Booth) / valid 收敛 / slotValid 收敛（flush>fill>remove>hold）；
  calculate* 辅助函数删除（用户要求无引用接口），重建全部进 work()。
- **转录 bug 一例（LCG 门禁捕获）**：`A` 误写零扩展
  `static_cast<uint64_t>(static_cast<uint32_t>(op1))`，主树为符号扩展——
  (0x80000000,1,MULH) 高半错。修后注释立碑。
- **接线**：common.h（RISC_V::M 对齐主树序号 + MULTIPLYRS_CAP=4；dec.type/UopEntry.type
  Wire/Register 3→4b）、Decoder 0x33+funct7==1 分类、RS multiplyRS 池（复用 IntRS）、
  IssueArbiter win=8（Wire<4>；DIV 族 opDec-invalid 门控 stall）+ mulP/select、
  DispatchArbiter mul grant（mulSelect 复用 selectOldest；rsType 恒 Integer 未消费）、
  PRF/ROB cdbOfMUL 第三口、CPU（MULModule/MulCDBModule + add_module ALU→MUL→AGU；
  顺手修 CDB 改名残留 AluCDBArbiter/LqCDBArbiter→AluCDB/LqCDB）、
  MulCDB 补 `mul cdb broadcast` 打印（VERBOSE=exec 计数法恢复）。
- **门禁全绿**：LCG 直驱 100267 例 ×Release/-D_DEBUG；8 快 + queens/magic/superloop/
  basicopt1(634k) I-用例双树逐位；rv32im 双臂 I/M bulgarian 258082/254483、
  statement 3559/2645、multiarray 2236/2236 双树逐位；广播计数 31/16 双树一致；
  build-assert 零触发；reorder gcd 20/20 + M/bulgarian 5/5。
- **rsType 对齐补丁**：`mul.rsType` 恒 Integer 近似值撤回，改驱 `RSType::Multiply`
 （主树 StaticArbiter.cpp:65）。双树读者-写者审计结论：`rsType` 写四处、读仅 AGU
  通道（选 Load/SA 池）；alu/bru/mul 三家双树皆纯写无读（专用总线身份即类型，
  非死代码，不可单删）；`RSType` 入枚举致 `StoreAddr=4`，`rsType` 线同步扩 3 位。
  改后 9 用例双树逐位零漂移 + build-assert 零触发（符合“无人消费故零行为差异”预期）。


## DIV 迁移落地（2026-09-12，模板树接线 + 全量验证）

**背景**：`src/DIV/DIV.cpp` + `src/include/DIV.hpp` 此前只是**孤立编译**——CMakeLists 收了源文件，
但 CPU 里没有 `DIVModule` / `DivCDB` / `divideRS`，Stage A 把 `funct3 4..7` 解码成 `OP_INVALID`
后直接 stall head。后果：M 语料里任何含 div/rem 的程序**卡死**，`register.h:38` 的双写断言也
永远触发不到（模块不跑）。本轮照 MUL 范式 + 主树参考实现把 DIV 接进模板流水线，并完成全量验证。

### 接线范围（12 文件）

| 文件 | 改动 |
|---|---|
| `include/common.h` | `DIVIDERS_CAP=4`；`RSType` 插入 `Divide`（`StoreAddr` 4→5，`rsType` 仍 3bit） |
| `src/include/RS.hpp` `src/RS/RS.cpp` | `divideRS` 池（`std::array<IntRS,4>`）+ `tryAllocDivide` + `isDivFree/getDiv{Op,Src1,Src2,RobTag}`；`sel.hasDivide/divideSlot`、`data.divP.*`、`dispatch.divValid/divIdx`；push/release/flush 单写口循环 |
| `src/include/StaticArbiter.hpp` `.cpp` | DispatchArbiter：`divAccept`（= `DIV::canAccept()`，**非 isFull**）+ `divBusy/divSrc1Tag/divSrc2Tag/divRobTag` + `divSelect()` + `div` 通道；IssueArbiter：`divBusy` 扫描、`divFree/divSlot`、win=**9**、`select.hasDivide/divideSlot`、`divP` 载荷组；`decodeOp` 的 M 族补齐 funct3 4..7；`issueClass` 按 op 拆 MUL(8)/DIV(9)；win=9 与 MUL 同构，在 robEntry 全部 9 处门控里登记 |
| `src/include/CDB.hpp` `src/CDB/CDB.cpp` | 新增 `DivCDB`：`divEmpty = !isReady`，无输出缓冲 ⇒ 直接抽结果寄存器；`divValue` 在接线层用 `isReady` 门控（`getValue()` 对陈旧 `operationType` 会 throw）；**不加** `VERBOSE=exec` 打印（参考实现的 `divCDB::build` 也没有，避免双树 exec 计数漂移） |
| `src/include/PRF.hpp` `src/PRF/PRF.cpp`、`src/include/ROB.hpp` `src/ROB/ROB.cpp` | 第四路写口/提交就绪口 `cdbOfDIV`（三路 → 四路）；PRF 侧保留 `prf div-write` exec 打印（与模板既有 `prf mul-write` 同风格） |
| `src/include/CPU.hpp` `src/CPU/CPU.cpp` | `DIVModule`/`DivCDBModule` 成员 + `add_module` + 全部 Input Wire 接线（DIV 六项入线、DispatchArbiter div 通道、IssueArbiter `divBusy`、PRF/ROB `cdbOfDIV`、RS `sel/data/dispatch`） |
| `test/CMakeLists.txt` | **补 `MUL.cpp`/`CDB.cpp`/`DIV.cpp`**：该列表自 MUL 落地起就漏了 `MUL.cpp` 与 `CDB.cpp`，reorder_test 一直链接不过（本轮实测确认） |

### 验证结果（全部当日实测）

| 门禁 | 结果 |
|---|---|
| 编译（MSYS g++ 15.2 `-O2`） | 通过；`-D_DEBUG` 版同样通过 |
| 编译（WSL gcc 13.3 + cmake 官方路径） | 通过。⚠️ 模板树 `build/` 缓存当时是 **Debug**（AGENTS 点名的坑），已按项目规则 `-DCMAKE_BUILD_TYPE=Release` 重配 + `rm code` 强制重链 |
| **M 语料 18/18**（`data/testcases_rv32im/M`） | x10 + clock 与主树**逐位一致**，并等于 `docs/benchmarks.md` M 臂表：TOTAL_CLOCK **12,150,300**（含 pi **7,844,594**，主树同值复核过） |
| **rv32i 语料 17/17（非 pi）** | x10 + clock 与主表逐位一致，TOTAL **5,628,277**；DIV 接线对 rv32i 镜像零漂移（镜像里无 M-ops ⇒ 不产生 div 事件）。rv32i pi（137.6M 拍）未重跑 |
| **WSL Release ELF 交叉复核 pi** | 根目录 code 二进制（gcc 13.3 / `-O2` / ELF）跑 M-pi：clock **7,844,594**，branch 283986/284202（=99.924%），与 MSYS g++ 15.2 构建**同值**——时钟数不是编译器产物 |
| **_DEBUG 跑 M 语料 17/17** | `register.h:38` 双写断言 **零触发**（覆盖 DIV 全部 FSM 支路：prepare / loop 五分支 / calculateResult / drain / flush） |
| **DIV 单元差分测试**（新增，直驱模块 + DivCDB） | **1,601,791 项检查 0 失败**（Release + `_DEBUG` 各一轮）= 21×21 边界集×4 op + 40 万随机向量×4 op（稀疏/大数/全随机三档）+ 15 条手写 corner（`INT_MIN/-1`、`d==0` 四种、`|x|<|d|`、`|x|==|d|`、符号组合）+ flush 三分支 + effective-tag 同拍替换子句；oracle 直接写自 RISC-V ISA 定义 |
| **reorder_test**（`run_once_shuffle`） | M/gcd 20/20、M/lvalue2 20/20 全同 |
| **DIV 动态事件计数**（`VERBOSE=exec`） | `prf div-write` gcd 7/7、bulgarian 18/18、statement_test 6/6 双树一致（`mul cdb` / `prf mul-write` 同步一致） |

### 等价性论证（模板 DIV 的"重写"不是转录）

模板 `DIV.cpp` 与参考实现**逐段对齐**（特例前段 / prepare / loop / calculateResult / flush），
差异只在两处**有证明的表示变换**：

1. **>32bit 状态拆 lo/hi**：P 域 PW = 35+shiftD ≤ 36bit，`regSLo/regSHi(4b)`、`regCLo/regCHi(4b)`、
   D_dp `unsignedDivisorLo/Hi(1b)`。消费方一律先 `& mask`（mask ≤ 36bit）⇒ 截断无损：
   `(X<<2)&mask` 只看 X 低 34 位；`Pk = regS+regC` 的低 36 位与全宽一致且随后 `Pk &= mask`；
   `Pk>>63` 的符号判据在 mask **之后**，故不依赖高位一的补垃圾。
2. **work() 单写口优先链**（flush > drain > fullAdder > loop > prepare > receive）替代参考的级联 if：
   参考 `tick` 里 `receive` 写在 `flush` 之前（flush 用**已写入的新 tag** 判 `isOlder`），
   模板用 `effTag = dispatchValid ? dispatchTag : robTagOld` 精确复现该语义（已单测覆盖两个方向）。

### 遗留 / 文档债

- `docs/output.md`（"全模块 Output 清单 + 中央总线归属表"）自 MUL 落地起未同步：缺 `MulCDB`/`DivCDB`、
  `mulP`/`divP`、`multiplyRS`/`divideRS`、win 码 8/9。它是迁移期计划文档，需一次专门的对齐 revision。
- 模板树 `code`（根目录 ELF）本轮被重编过，已按 Release 重建；旧二进制备份在 `.cache/code.bak_before_div_test`。
- 验证脚手架（未入库，均在 `.cache/`）：`div_unit_test.cpp`（DIV 单元差分测试）、`build_sim.py`（直驱 g++ 构建）、
  `simrun.py`（语料批量跑）、`reorder_run.py`（乱序一致性）。若需纳入仓库建议放 `test/`。
