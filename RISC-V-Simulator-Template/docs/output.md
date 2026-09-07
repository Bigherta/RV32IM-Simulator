# Output 清单：当前框架 → RISC-V-Simulator-Template 映射

> 目标：把当前 `src/` 中"模块的 const 访问器 + 集中式 comb() 总线"逐一映射为模板的 **Output Wire**，
> 为按模板迁移提供接线清单。依据：`src/include/*.hpp`、`src/*/*.cpp`、`CPU.cpp::comb()`（2026-08-22）。

> **修订（2026-08-22，RS 去值化 / PRF 统一管值）**：RS 槽不再缓存寄存器值——条目改存
> `Operand {int tag; int32_t imm;}`（`src1/src2`，storeValueRS 为 `data`）：寄存器源只存物理寄存器号
> `tag`，立即数/PC/x0 存 `{tag:-1, imm}`；**就绪/取值一律派生自 PRF**
> （`PRF::isOperandReady(op)` / `PRF::getOperandValue(op)`）。连带：`RSUnit::broadcast()` 与
> `RSInput.cdbBus` 删除；`CDBBus` 收敛为 `{lsqSetCDB, memIndex}`（仅 LQ 消费）；
> `CDBBypass` 删除（issue 拍生产者同拍广播 → PRF.tick 同拍写 PRF，dispatch(N+1) 见 isReady 即就绪）；
> `IssueArbiterInput.cdbOut` 删除；`DispatchArbiter` 就绪判 `PRF.isOperandReady`；
> ALU/AGU/BRU/SQ dispatch 从 PRF 取值；FlushArbiter `loadViolations` 计数器删除。
> 本清单已按此更新（RS/PRF/§2 总线/§3 注意点）。

## 0. 映射规则（读本文前先看）

模板三件套 ↔ 当前框架：

| 模板 | 当前框架 |
|---|---|
| **Output Wire**（本模块对外组合值） | 模块的 **const 访问器**（组合视图），以及 comb() 中基于本模块状态派生的总线 |
| **Input Wire** | 当前 `XInput` 结构体（引用成员 + 值字段） |
| **Inner Register** | `CPUstate.XModule` 的状态字段 |
| Module::work() | tick() |

规则要点：

- **组合求值 = Wire 套 Wire**：消费者 Input 的 Wire lambda 引用生产者 Output 的 Wire，懒求值 + per-cycle 缓存。
- **Wire 位宽上限 32bit**：多字段结构体（SquashInfo/CDBOutput/FetchDecision/...）必须拆成**逐字段 Wire 聚合体**（聚合体 sync 上限 ≤14 成员，超了再嵌套）。
- **纯函数/静态工具不迁移**：`ROB::isOlder/isYounger/idx`、`Decoder::decode/signExtend`、`Memory` 读写等保持普通函数。
- **每索引组合数组**（如 `getValue(i)`）：生产端暴露 `std::array<Wire<N>, CAP>`，或消费端重复组合（前者推荐，单点求值）。
- **统计/诊断字段不迁移**：`branchTotal/branchCorrect` 不属于组合输出。
- **Memory 不进 sync**：IMEM/DMEM 的存储数组是外部数据，不属于模块 Register/Wire。
- **时钟边沿语义**：当前 `memcpy(&XModule,&CPUstate.X,...)` 等价于模板每条 `Register` 的 `_M_old=_M_new`（`<=` 写、`sync()` 提交）；跨模块读 = Wire 读 `_M_old`，拍内恒定。

---

## 1. 各模块 Output 清单

### 1.1 FetchUnit（Inner: `programCounter:32`, `haltFetched:1`）

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `getPC()` | 32 | CPU.comb（FetchDecision::build） | `Wire<32> pc` |
| `isHaltFetched()` | 1 | CPU.comb（fetchDecision gate、`fqInput.haltFetched`、`icacheInput.popConsume`） | `Wire<1> haltFetched` |

### 1.2 ICache（Inner: `blocks[1024]`、`requestBuffer[4]`、`head:8`、`count:8`）

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `hit(addr)` | 1 | CPU.comb（icacheHit→门控 imemFetch）、ICache.tick（isHit） | `Wire<1> hit(addr)`（addr 为入参 → 组合函数入参） |
| `isRequestFull()` | 1 | CPU.comb（fetchDecision 门控的 imemReqFull） | `Wire<1> requestFull` |
| `isReturnReady()` | 1 | CPU.comb（haltSignal、popConsume）、FQ.tick | `Wire<1> returnReady` |
| `returnRaw()` | 32 | CPU.comb（haltSignal 判 `0x0ff00513`）、FQ.tick | `Wire<32> returnRaw` |
| `returnPC()` | 32 | FQ.tick | `Wire<32> returnPC` |
| `returnPredictPC()` | 32 | FQ.tick | `Wire<32> returnPredictPC` |
| `returnCkptId()` | 8 | FQ.tick | `Wire<8> returnCkptId` |

### 1.3 IMEM（Inner: `IMEMreqs[16]`、`head:8`、`count:8`、Memory）——✅ 已落模板（2026-08-23）

实际定版：`IMEM : Memory, Module<IMEMInput, IMEMOutput, IMEMInner>`（Memory 存储外部不进 sync）；条目=`std::array<IMEMRequest,16>`{Data:`std::array<Register<8>,16>` 全寄存器化、lineAddr:32、remainCycle:**2**bit(0..3)、valid:1}；head→`Register<4>`；**count 寄存器删除（手法 #10）**：窗口不变式 valid[]⟺[head,head+occ) 下 occupancy=popcount(valid[]) 组合派生（push 槽位=head+occ）、isRequestFull=AND 归约、isReturnReady 只看首槽位；Input=4 条 Wire（needSquash/fetchValid←`fetchDecision.valid&&!ICache.hit(pc)` 门控/fetchPC←`pc&~0xF` 行对齐/lineConsumed←自读 getReturn().valid）；squash 清全槽 valid；周期首 `assert(windowContiguous())` 校验不变式。

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `isReturnReady()` | 1 | CPU.comb（`imemInput.lineConsumed`） | `Wire<1> returnReady` |
| `isRequestFull()` | 1 | CPU.comb（fetchDecision 门控） | `Wire<1> requestFull` |
| `getReturn()` | 1+32+4×32 | CPU.comb（`icacheInput.lineReturn`） | `struct LineReturnWire { Wire<1> valid; Wire<32> lineAddr; std::array<Wire<32>,4> data; }` |

### 1.4 FetchQueue（Inner: `FetchQueueEntries[8]`、`head:8`、`tail:8`）——✅ 已落模板（2026-08-23）

实际定版：`FetchQueue : Module<FQInput, FQOutput, FQInner>`；条目=`std::array<FetchQueueEntry,8>`（raw/pc/predictedPC:32 + ckptId:**6**bit 各一 Register，FQInner）；head/tail→`Register<3>`（FQOutput，=log2(FQ_CAP)）；Input=8 条 Wire（needSquash/haltFetched/icacheReturn×5[ckptId:6]/decodeFull，wire() 接模块实例）；`full/empty/head*` 未做 Output Wire，以 const 桥接访问器读 `_M_old` 输出（消费方 CPU.comb/run、DecodeUnit 的 fq* Wire 零改动）。

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `isFull()` | 1 | CPU.comb（fetchDecision 门控） | `Wire<1> full` |
| `isEmpty()` | 1 | Decoder.tick、CPU.run(finish) | `Wire<1> empty` |
| `headRaw()` | 32 | Decoder.tick | `Wire<32> headRaw` |
| `headpc()` | 32 | Decoder.tick | `Wire<32> headPc` |
| `headPredictedPC()` | 32 | Decoder.tick | `Wire<32> headPredictedPC` |
| `headCkptId()` | 8 | Decoder.tick | `Wire<8> headCkptId` |

### 1.5 DecodeUnit / InstructQueue（Inner: `instructQueueEntries[16]`、`head:8`、`tail:8`）——✅ 已落模板（2026-08-23）

实际定版：合并为单一 `DecodeUnit : Module<DecodeInput, IQOutput, IQInner>`（InstructQueue 类删除）；条目=`std::array<UopEntry,16>`，Uop 13 字段全 Register（type:3/opcode:7/funct3:3/funct7:7/rd,rs1,rs2:5/imm,pc,predictedPC:32/isHalt,allocDest:1/ckptId:**6**bit，≤14 成员上限）；head/tail→`Register<4>`（IQOutput，=log2(IQ_CAP)）；Input=7 条 Wire（needSquash/issueValid/fqEmpty/fqHeadRaw/Pc/PredictedPC[32]/CkptId[6]）；`head*` 全部保留 const 桥接访问器读 `_M_old` → **IssueArbiter 零改动**。

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `isFull()` | 1 | FQ.tick | `Wire<1> full` |
| `isEmpty()` | 1 | CPU.run(finish) | `Wire<1> empty` |
| `headType()` | 3 | IssueArbiter.build | `Wire<3> headType` |
| `headOpcode()` | 7 | IssueArbiter.build | `Wire<7> headOpcode` |
| `headFunct3()` | 3 | IssueArbiter.build | `Wire<3> headFunct3` |
| `headFunct7()` | 7 | IssueArbiter.build | `Wire<7> headFunct7` |
| `headRd()` | 5 | IssueArbiter.build | `Wire<5> headRd` |
| `headRs1()` | 5 | IssueArbiter.build | `Wire<5> headRs1` |
| `headRs2()` | 5 | IssueArbiter.build | `Wire<5> headRs2` |
| `headImm()` | 32 | IssueArbiter.build | `Wire<32> headImm` |
| `headPc()` | 32 | IssueArbiter.build | `Wire<32> headPc` |
| `headIsHalt()` | 1 | IssueArbiter.build | `Wire<1> headIsHalt` |
| `headAllocDest()` | 1 | IssueArbiter.build | `Wire<1> headAllocDest` |
| `headPredictedPC()` | 32 | IssueArbiter.build | `Wire<32> headPredictedPC` |
| `headCkptId()` | 8 | IssueArbiter.build | `Wire<8> headCkptId` |

### 1.6 ROB（Inner: `ROBqueue[64]`、`head:7`、`next_tag:7`、`haltCommitted:1`、`haltRd:6`）

注意 `getIndexByTag` 带参数反查（调用点须保证条目存活），模板中做成分模块的 Wire 组合函数：

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `isFull()` | 1 | IssueArbiter.build | `Wire<1> full` |
| `isEmpty()` | 1 | 多处 tick 守卫、BPU.tick、PRF.tick、CPU.run(finish) | `Wire<1> empty` |
| `isHaltCommitted()` | 1 | CPU.run(finish) | `Wire<1> haltCommitted` |
| `getHaltRd()` | 6 | CPU.run（输出 x10） | `Wire<6> haltRd` |
| `isHeadCommitReady()` | 1 | PRF.tick、BPU.tick | `Wire<1> headCommitReady` |
| `isHeadHalt()` | 1 | PRF.tick | `Wire<1> headHalt` |
| `headType()` | 2 | PRF.tick | `Wire<2> headType` |
| `headDest()` | 5 | PRF.tick | `Wire<5> headDest` |
| `getNextTag()` | 7 | IssueArbiter.build | `Wire<7> nextTag` |
| `getHead()` | 7 | 多处年龄守卫 | `Wire<7> head` |
| `getIndexByTag(tag)` | 7 | RS/PRF/FlushArbiter/BPU/CPU.comb | `Wire<7> indexByTag(tag)` |
| `isCommitReadyAt(i)` | 1 | PRF.tick | `std::array<Wire<1>,64> commitReadyAt` |
| `getType/getDest/getPC/isHalt/getCkptId/getPredictedPC/getLqtTailSnapshot/getSqtTailSnapshot/getNewPhy/getOldPhy/isCall/isRet` (i) | 各 ≤32 | 各 tick / FlushArbiter / IssueArbiter / BPU | `std::array<Wire<..>,64>` 或按需组合 |
| `ROB::isOlder/isYounger/idx` | — | 全项目 | **不迁移**（纯比较/切片工具） |

### 1.7 RSUnit（Inner: `integerRS[16]`、`loadRS[4]`、`storeAddressRS[8]`、`storeValueRS[8]`、`branchRS[4]`）

RS 去值化后条目只存 `Operand`（寄存器源 = 物理号 `tag`，立即数/PC/x0 = `{tag:-1,imm}`），**不再缓存值/依赖位**；
就绪与取值由消费方调 `PRF::isOperandReady/getOperandValue` 派生。`RSInput` 现持 `const PRF &`（不再持 ROB、不再有 `cdbBus`/`broadcast()`）。

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `tryAllocInteger()` 等 5 个 | 4 | IssueArbiter.build | `Wire<4> allocInteger` + `Wire<1> hasSlot`（或暴露空闲标志位） |
| `integerRS/loadRS/storeAddressRS/branchRS[i].src1/src2`（Operand 直读） | tag:7 + imm:32 | ALU/AGU/BRU dispatch（经 `PRF.getOperandValue` 取值）、DispatchArbiter（经 `PRF.isOperandReady` 判就绪） | RS 是少数跨模块直读状态的模块；模板改为 `std::array` Inner + 消费者经 Output Wire/Register 读 `_M_old`，再组合 PRF Output |
| `storeValueRS[i].data`（Operand 直读） | tag:7 + imm:32 | CPU.comb（storeNotifies 扫描，`PRF.isOperandReady(data)` 判就绪）、SQ.tick（同判据，值取 `PRF.getOperandValue`）、RS.tick（同判据释放槽位） | 同上；`data` 就绪即 RS 槽可释放（值在 PRF） |

### 1.8 ALU（Inner: `outputBuffer[4]`、`slotValid[4]`）

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `headValue()` | 32 | CPU.comb（CDBArbiter::build） | `Wire<32> headValue` |
| `headRobTag()` | 7 | CDBArbiter | `Wire<7> headRobTag` |
| `headIsControl()` | 1 | CDBArbiter | `Wire<1> headIsControl` |
| `isFull()` / `isEmpty()` | 1 | DispatchArbiter、CDBArbiter | `Wire<1> full` / `Wire<1> empty` |
| `isValid(i)` | 1 | CDBArbiter（槽扫描） | `std::array<Wire<1>,4> slotValid` |

### 1.9 AGU（Inner: `outputBuffer[4]`、`slotValid[4]`）

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `headValue()` | 32 | CPU.comb（storeAddrNotify）、SQ.tick、LQ.tick、FlushArbiter.tick | `Wire<32> headValue` |
| `headRobTag()` | 7 | 同上 | `Wire<7> headRobTag` |
| `headMemIndex()` | 7 | 同上 | `Wire<7> headMemIndex` |
| `isFull()` / `isEmpty()` | 1 | DispatchArbiter、LQ.tick | `Wire<1> full` / `Wire<1> empty` |

### 1.10 BRU（Inner: `outputBuffer[4]`、`slotValid[4]`）

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `headPCFrom()` | 32 | FlushArbiter.tick、BPU.tick | `Wire<32> headPCFrom` |
| `headPCResult()` | 32 | FlushArbiter.tick、BPU.tick | `Wire<32> headPCResult` |
| `headRobTag()` | 7 | FlushArbiter.tick、BPU.tick、ROB.tick | `Wire<7> headRobTag` |
| `isFull()` / `isEmpty()` | 1 | DispatchArbiter、FlushArbiter、BPU | `Wire<1> full` / `Wire<1> empty` |

### 1.11 BPU（Inner: `globalPHT/LHT/localPHT/selector[4096]`、`BTB[512]`、`RAS[16]`、`alignQueue[32]`、`GHR:16`、`RAS_top/alignHead/alignTail/nextCkptId:8`）

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `predict(pc)` | PredictInfo 多字段 | CPU.comb（FetchDecision::build） | `struct PredictWire { Wire<1> taken; Wire<32> predictPC; Wire<1> btbHit; Wire<1> unconditional; Wire<1> isCall; Wire<1> isRet; }`（带 pc 入参） |
| `getNextCkptId()` | 8 | FetchDecision::build | `Wire<8> nextCkptId` |
| `getBranchTotal/getBranchCorrect` | 64 | 仅诊断打印 | **不迁移** |

### 1.12 LQ（Inner: `LQqueue[16]`、`head:8`、`tail:8`）

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `isEmpty()` / `isFull()` | 1 | IssueArbiter、LQ 内部 | `Wire<1> empty` / `Wire<1> full` |
| `getHead()` / `getTail()` | 4 | IssueArbiter、FlushArbiter、ROB.tick、PRF.tick | `Wire<4> head` / `Wire<4> tail` |
| `isActive(i)` | 1 | 各窗口扫描 | `std::array<Wire<1>,16> active` |
| `isReadyToCommit(i)` | 1 | ROB.tick、PRF.tick | `std::array<Wire<1>,16> readyToCommit` |
| `getAddress(i)` / `getValue(i)` | 32 | PRF.tick、FlushArbiter | `std::array<Wire<32>,16>` |
| `headRobTag()` / `getRobTag(i)` | 7 | 多处 | `std::array<Wire<7>,16>` |
| `getIsUnsigned(i)` / `getNBytes(i)` | 1 / 3 | MemRequestArbiter | `std::array<Wire<1>,16>` / `std::array<Wire<3>,16>` |
| `isAddressReady(i)` | 1 | FlushArbiter | `std::array<Wire<1>,16>` |
| `getValueState(i)` | 2 | FlushArbiter | `std::array<Wire<2>,16>` |
| `getIsCDBBroadcast(i)` | 1 | LQ 内部（CDBDetect 扫描）~~CDBBus::build~~ | `std::array<Wire<1>,16>` |
| `CDBDetect()` | 7 | CPU.comb（CDBArbiter::build） | `Wire<7> cdbDetect` + `Wire<1> cdbDetectValid` |
| `LoadDetect()` | 7 | LQ.tick（内存依赖检查） | `Wire<7> loadDetect` + `Wire<1> loadDetectValid` |

### 1.13 SQ（Inner: `SQqueue[16]`、`head:8`、`tail:8`）

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `isEmpty()` / `isFull()` | 1 | IssueArbiter、LQ.tick | `Wire<1> empty` / `Wire<1> full` |
| `getHead()` / `getTail()` | 4 | IssueArbiter、ROB.tick | `Wire<4> head` / `Wire<4> tail` |
| `isActive(i)` | 1 | 窗口扫描 | `std::array<Wire<1>,16>` |
| `isReadyToCommit(i)` | 1 | ROB.tick | `std::array<Wire<1>,16>` |
| `headRobTag()` / `getRobTag(i)` | 7 | ROB.tick | `std::array<Wire<7>,16>` |
| `planDataForward(i,value)` | StoreNotify 多字段 | CPU.comb（storeNotifies） | `struct StoreNotifyWire { Wire<1> valid; Wire<7> storeTag; Wire<32> addr; Wire<32> value; Wire<1> foundKnownSame; Wire<7> knownSameAddressOldestTag; Wire<1> foundUnknown; Wire<7> unknownOldestTag; }` |
| `planAddressForward(i,addr)` | StoreNotify | CPU.comb（storeAddrNotify） | 同上 |
| `replyToLoadRequest(addr,loadTag)` | StoreResponse | LQ.tick（load 中心查询，主转发路径③） | `struct StoreResponseWire { Wire<1> valid; Wire<32> value; }` |
| `canDispatchLoad(addr,loadTag)` | 1 | LQ.tick / MemRequestArbiter（LoadDetect 准入） | `Wire<1> canDispatchLoad(addr,loadTag)` |
| `hasOlderUnresolvedAddressStore(loadTag)` | 1 | PRF.tick、ROB.tick（commit 守卫） | `Wire<1> hasOlderUnresolved(loadTag)` |

### 1.14 PRF（Inner: `PhysicalRegs[128]`、`freeList[128]`、`headSeq:32`、`tailSeq:32`）

RS 去值化后 PRF 成为**唯一取值点**：`isOperandReady/getOperandValue` 是 RS 源操作数就绪/取值的统一出口，
被 DispatchArbiter、ALU/AGU/BRU、SQ、CPU.comb（storeNotifies）广泛消费。

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `isFreeListEmpty()` | 1 | IssueArbiter.build | `Wire<1> freeListEmpty` |
| `getHeadSeq()` | 32 | IssueArbiter.build | `Wire<32> headSeq` |
| `getFreeListSlot(seq)` | 7 | IssueArbiter.build | `Wire<7> freeListSlot(seq)` |
| `isReady(i)` | 1 | DispatchArbiter、ALU/AGU/BRU/SQ（经 getOperandValue 链路）、CPU.run | `std::array<Wire<1>,128>` |
| `getValue(i)` | 32 | ALU/AGU/BRU/SQ dispatch、CPU.comb（storeNotifies 值）、CPU.run（x10） | `std::array<Wire<32>,128>` |
| `isOperandReady(op)` | 1 | DispatchArbiter（RS 就绪）、CPU.comb（storeNotifies）、SQ.tick、RS.tick | `Wire<1> operandReady(op)`（op 为入参，`tag==-1‖isReady(tag)` 组合） |
| `getOperandValue(op)` | 32 | ALU/AGU/BRU dispatch、SQ.tick（store 值）、CPU.comb（storeNotifies 值） | `Wire<32> operandValue(op)`（`tag==-1 ? imm : getValue(tag)` 组合） |
| `head()/tail()/size()` | 32 | 诊断 | 可选 |

### 1.15 RAT（Inner: `RAT_PRF[32]`、`ratCkpt[64]`）

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `readRAT_PRF(reg)` | 7 | IssueArbiter.build（oldPhy）、CPU.run（x10） | `Wire<7> ratToPrf(reg)`（带 reg 入参） |
| `readOperand(reg)` | 1+32+7 | IssueArbiter.build | `struct OperandWire { Wire<1> ready; Wire<32> value; Wire<7> phyRegIndex; }` |
| `snapshotRAT_PRF()` | 快照 | checkpoint 恢复 | **不迁移**（checkpoint 是状态复制非组合输出；模板由 `ckpt_id` + 本地快照 Register 数组处理） |

### 1.16 DMEM（Inner: `busy:1`、`bufferValid:1`、`MemExecution`、`MemOutputBuffer`、Memory）

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `isBusy()` | 1 | MemRequestArbiter | `Wire<1> busy` |
| `isReady()` | 1 | CPU.comb（LoadReturn）、DMEM.tick（MemPull） | `Wire<1> ready` |
| `LoadReturn(squash)` | LoadResponse 多字段 | CPU.comb（`lqInput.loadResp`） | `struct LoadResponseWire { Wire<1> valid; Wire<7> memIndex; Wire<7> robTag; Wire<32> value; }` |
| `load_n_bytes(addr,n,signed)` | 32 | DMEM.tick（读 Memory） | **不迁移**（纯 Memory 读函数） |

### 1.17 FlushArbiter（Inner: `requests[4]`）

| 当前访问器 | 位宽 | 消费方 | 模板 Output Wire |
|---|---|---|---|
| `arbitResult()` | SquashInfo 多字段 | **广播给所有模块的 `Input.squashDetect`** | `struct SquashInfoWire { Wire<1> needSquash; Wire<2> kind; Wire<32> squashIndex; Wire<7> squashTag; Wire<32> squashPC; Wire<8> ckptId; }`（最关键广播总线） |
| `getRequest(i)` | FlushRequest | 诊断 | 可选 |

---

## 2. 中央组合总线归属表（当前在 CPU::comb()，模板需定归属）

| 总线 | 当前来源 | 消费方 | 模板归属建议 |
|---|---|---|---|
| `squashDetect` | `FlushArbiter.arbitResult()` | 所有模块 Input | **FlushArbiter 的 Output**（SquashInfoWire），各模块 Input Wire 引用 |
| `fetchDecision` | `FetchDecision::build(BPU,FU,FQ,ICache,IMEM,squash)` | imemInput/icacheInput/fetchUnitInput/bpInput | **FetchUnit 侧接线层 / 独立 FetchDecision 无状态模块 Output**（拆逐字段 Wire） |
| `haltSignal` | ICache `isReturnReady && returnRaw==halt` | fetchUnitInput | **ICache Output**（`Wire<1> haltSignal`，lambda 里组合 returnReady+returnRaw） |
| `cdbOut` | `CDBArbiter::build(ALU,LQ,squash)` | aluInput、robInput、prfInput、flarbInput、bpInput | **独立 CDBArbiter 模块 Output**（CDBOutputWire：result{value,robTag,isControl}+valid+aluGranted+memGranted+memIndex）。~~isarbInput~~（RS 去值化后 IssueArbiter 不再消费 cdbOut） |
| `CDBBus` | `CDBBus::build(cdbOut,squash)`（~~ROB/PRF~~，不再广播值） | **仅 lqInput**（~~rsInput~~，RS 广播已删） | **接线层 Output Wire**，只含 `{Wire<1> lsqSetCDB; Wire<8> memIndex}`，lambda 内读 cdbOut Wire |
| `DispatchBus` | `DispatchArbiter::arbitrate(RS,ALU,AGU,BRU,ROB,PRF,squash)`，就绪判 `PRF.isOperandReady(src)` | alu/agu/bru/rs Input | **独立 DispatchArbiter 模块 Output**（alu/agu/bru 各 DispatchInfoWire：valid+rsIndex+robTag+rsType），lambda 读 RS Output + PRF Output |
| `MemDispatchDecision` | `MemRequestArbiter::arbitrate(LQ,SQ,ROB,DMEM,squash)` | dmem/lq/sq Input | **独立 MemRequestArbiter 模块 Output**（valid+MemRequestWire） |
| `LoadResponse` | `DMEM::LoadReturn(squash)` | lqInput | **DMEM 的 Output**（LoadResponseWire），见 1.16 |
| `storeNotifies[8]` | CPU.comb 扫描 RS `storeValueRS[i].data`（Operand）+ `PRF.isOperandReady` 判就绪 + `SQ::planDataForward`；值取 `PRF.getOperandValue` | lqInput | **接线层组合 Wire 数组**（lambda 内读 RS Output + PRF Output + SQ Output），或 SQ 侧 Output 提供 |
| `storeAddrNotify` | AGU head + `SQ::planAddressForward` | lqInput | **接线层组合 Wire**（AGU Output + SQ Output） |
| `issuePacket` | `IssueArbiter::build(...)`（7 模块 Input，多字段超宽）；**不再读 cdbOut**（CDBBypass 已删），源操作数只存 RAT tag/x0 常量（`resolveSrc`），值不在此取 | 六模块 tick apply | **独立 IssueArbiter 模块 Output**；因字段宽超 32bit，拆成多组逐字段 Wire（robTag/phy/robIndex/各 RS 槽位 Operand/标志位/…） |

**归属原则**：仲裁类（CDB/Dispatch/Mem/Issue）做独立无状态模块；单生产者总线（squashDetect/loadResp/haltSignal）挂在生产模块 Output；跨模块派生的中间总线（CDBBus/storeNotifies）作为接线层 Wire 节点、多消费者引用（Wire 套 Wire 单点求值）。

---

## 3. 迁移注意点

1. **Wire 套 Wire 必须无环**：当前 comb() 的求值 DAG 无环，迁移保持即可；反馈会无限递归。
2. **`getIndexByTag` 的存活守卫**（headTag 守卫 / squash 检查）在模板 Wire 组合函数里同样保留，不因改 Wire 而放宽。
3. **宽结构体拆分**：所有含 32bit 以上总宽的结构（SquashInfo/CDBOutput/CDBBus/FetchDecision/MemRequest/IssuePacket/StoreNotify/LoadResponse/PredictInfo/OperandInfo/LineReturn）都拆成逐字段 Wire 聚合体；聚合体成员数超 14 时嵌套（模板 sync_member 上限）。
4. **RS 去值化后的直读与 PRF 单一取值点**：RS 槽只存 `Operand{tag,imm}`，值/就绪一律由 PRF 派生
   （`isOperandReady`/`getOperandValue`）。模板中 PRF 的 Output Wire（`getValue/isReady` 数组 + 两个
   Operand 组合出口）被 DispatchArbiter/ALU/AGU/BRU/SQ/CPU.comb **多路引用**（扇出大），宜做成共享
   Output Wire 节点（Wire 套 Wire 单点求值）而非各消费端重复组合。RS 的 `storeValueRS[i].data` 就绪即
   释放槽位，取值在 PRF——RS/SQ/CPU.comb 三处同判据（`PRF.isOperandReady(data)`）。
5. **CDBBus 已收敛为单消费者**：`{lsqSetCDB, memIndex}` 仅 LQ 消费（RS 广播删除），是模板中最窄的总线；
   `cdbOut` 也不再进入 IssueArbiter（CDBBypass 删除）。
6. **reorder_test → `run_once_shuffle`**：模板的乱序无关由 Wire 只读 `_M_old` 结构性保证，等价于当前 17 阶段全排列验证。

---

## 4. 迁移完成状态（2026-09-02）

- **官方判题 19/19 全过**（x10 Golden OK，含 pi=137）；非 pi 17 例 + wb_test 的
  x10+clock 与主树逐位一致。
- 关键架构落点：`FlushArbiterInputROB.robPC`（AGU 违例 squash 目标 = 违例 load
  **真实 PC**，robPredictPC 对 load 恒 0 不可用）；DCache 方案 B（`cacheSets` 不进
  sync）+ `statHits/statMisses/statWritebacks` 统计 + `TOPIC_DCACHE` 汇总打印。
- 诊断代码全部清零（TEMP-DIAG / DBG 探针 / 临时计数器），stderr 仅剩 debug 体系
  门控输出；主树除既有工作外无迁移遗留改动。