# 前端子系统：取指 · 预译码 · 译码 · 分支预测

> **2026-10-18 AXIFetch容量收敛**：两软件按当前RTL将行运输16→8，对齐I$8请求预算，
> pending AR保持、错路R丢弃、顺序返回及取指时刻不变。RAS通过ROB四槽窄窗重放，四拍
> 游标及同步基线读不变。验收见父工作区`sync/RTL-TO-SW-SYNC-20261018.md`。

> **2026-10-17 四拍恢复**：已提交RAS改为`SRAM<1,(RAS_CAP<<5),32>`、独立valid与深度；
> 广播拍同步读完整宽字，随后四拍从旧提交基线重放存活ROB的call/return（含head和边界）恰好一次。
> 投机栈仍为寄存器，pop后push规则、空满行为保持。恢复期冻结取指/FQ/IQ及早RAS事件，
> ICache和AXIFetch仍处理在途事务；GHR/checkpoint在广播拍按原规则恢复。没有新增取指流水级。
> 验收见父工作区`sync/RECOVERY4-SYNC-20261017.md`，下方恢复拍数/周期表为历史。

> **2026-10-17 AXIFetch第二轮优化**：16行容量和前端请求预算不变，三种队列扫描改为
> 占用计数/epoch游标，payload不做复位或分配清零；pending AR、恢复丢弃和整行返回边界
> 保持，不新增流水级。详见[memory.md](memory.md)。

> **2026-10-17 AXIData**：仅数据侧改为游标/共享payload；AXIFetch容量、I$、AR仲裁与
> 恢复运输规则均未修改，不通过缩减取指预算获得面积下降；见[memory.md](memory.md)。

> **2026-10-16 两路CDB**：ALU路径及BPU/FlushArbiter训练/恢复不变；仅MUL/DIV/LQ
> 共享第二路结果总线并按完整ROB年龄仲裁。前端容量和取指时序不改，详见[backend.md](backend.md)。

## 2026-10-16 双树AXI同步（当前合同）

I$仍1 KiB/64 B直映，请求预算随共同`REQUEST_CAP`调整为8，checkpoint上限为ROB16+I$8+FQ2+IQ2=28≤32。原IMEM供数改为AXIFetch逐32-bit AXI读，I/D共享AR与返回owner队列；恢复丢弃旧运输响应，不撤销已断言但未握手AR，计数器覆盖完整在途范围。FQ/IQ、BPU及RAS语义不改。
`addi a0,x0,255`在课程镜像中是正常指令，只有`LEGACY_HALT=1`使用旧预译码停机规则；默认在MMIO退出Store按序退休后冻结取指。旧IMEM20拍/历史周期表不适用于当前AXI。

> **2026-10-16主树数据侧升级边界**：只新增D$四MSHR/同行合并和DMEM读写共享16项队列，
> I$/IMEM、取指请求预算、原顺序交付、预测/checkpoint、boot及HALT规则保持。
> 没有在I$实现MSHR或同行合并；整核周期因数据侧变化而改变，见主树活动表。

> **历史共同默认（2026-10-14）**：D$按RTL同步为4 KiB/32 B直映、4拍回填、首拍Load
> 早返回；I$仍1 KiB/64 B，取指/预测/checkpoint及boot1拍/立即HALT规则不变。
> 当前验证见[cache.md](cache.md)，以下8拍回填为旧配置。

> **2026-10-14 D$几何扫描**：数据行32/64/128 B与1/2/4路PLRU独立参数化；
> I$仍64 B/1 KiB，取指、BPU/checkpoint及停取规则不变，见[cache.md](cache.md)。

> **2026-10-13 D$ early restart**：仅提前缺失Load供数，I$/IMEM、预测、取指、checkpoint
> 与停取规则不变；HALT可在D$仍回填时立即结束，不新增drain，详见[cache.md](cache.md)。

> **2026-10-13 valid寄存器同步**：D$ tag/dirty SRAM为20bit，valid独立寄存；查询采样对齐、8拍回填和boot1拍保持。I$/预测/取指不变，结构及最新验证见[cache.md](cache.md)。

> **2026-10-13 D$8拍回填**：仅调整数据缓存及其接线，16个32bit bank每拍写一对。
> I$/IMEM、取指/预测、boot及HALT规则不变；整核周期受数据缺失延迟影响，见活动表。

> **当前双树HALT边界（2026-10-13）**：取指停取及boot保持，HALT提交后立即结束，
> 不等待SQ/Cache/DMEM；clock与core相同、drain=0。D$改为data/metadata同步SRAM，
> 两软件两构建各18＋6与当前周期表严格一致。RTL未同步，下面全dirty/排空/三端等价记录为历史。

> **CPU顶层初始化（SPEC §66）**：initialized直接归CPU，保留boot隔离与原取指
> 门控；不增加独立控制模块。四构建18＋6周期/统计保持。

> **完整活动整核（SPEC §63/64）**：15真实CPU补验收口七池/LSQ；完整18 ISA＋6 IPC
> 在共同20拍逻辑Memory下12667040拍2872观察word无损逐拍、退休/最终128KiB/
> 分段周期三端全等。真实返回错误路径非法指令PC/CID/异常就绪已补软件，未屏蔽
> 活条目。golden11973998/693042保持，课程AXI/未纳入异常继续，README §55/56。

> **boot/HALT边界（SPEC §62）**：初始化1拍禁止正常取指/CID/Issue/提交，之后PC0/
> CID0进入正常周期0；不再用实际第一取指掩盖boot。HALT提交冻结core/IPC，最终
> DONE等待全部dirty写回。16程序9629/2872与最终内存核验/7负向通过；两树两构建
> 18＋6 strict新total11973998/693042，core保持。第19当前域完成，README §54。

> **全流水恢复验收（SPEC §61）**：真实误预测→请求队列→最老广播与前端/BPU/
> 全后端67程序9001拍2872字段严格三端一致，11源码负向有效。恢复拍取消I$/FQ/IQ
> 并核验GHR/CID/RAS完整内容，旧请求与新候选不组合穿透。第18当前对齐/共同逻辑
> 存储域完成，第19复位/boot/HALT/drain计时继续，入口sync/README §53。

> **BTB竞争/参数验收（SPEC §60）**：tag/类型与target分别按fetch > jump CDB >
> taken BRU逐槽仲裁；JALR/RET早训练无静态target不能屏蔽解析target，无写者保旧值。
> 26真实三CPU程序6690拍1848字段严格同值，三类fetch/三源五种槽分组齐全，七实际
> 软件/RTL负向有效。13合法/9非法RTL参数及22软件编译约束通过；第17当前域完成，
> 所有FU/LSQ全流水恢复继续第18步。默认参数/活动表保持，入口sync/README §52。

> **用户RAS规则与真实CPU闭环（SPEC §59）**：JALR push/pop独立由rd/rs1是否为
> x1/x5决定，双link含同寄存器先pop再push；JAL只按rd。33真实程序13,041拍1848
> 字段三端严格同值，恢复核验完整栈内容而非仅depth，7实际源码功能负向有效。
> Checkpoint仍GHR8/32槽，一次接受取指一次CID，恢复arch基线＋存活ROB含边界；
> 18 ISA11967396保持，六IPC新691640（+826），实测表已更新。入口README §51。

> **前端当前验收（SPEC §58）**：RTL I$采用公共同步SRAM data＋寄存器tag，按本树
> 队首读旁路/保存、同行回填及真正FQ消费处理。实际I$/IMEM2324/160、取指至实际
> decoded IQ2259/216四方严格全等，HALT/恢复/满槽背压/给定JAL预测/checkpoint回绕
> 通过；源码仅加只读探针友元，Release/_DEBUG各18＋6/golden保持。入口README §50，
> 给定预测/恢复边界之后的BPU内部闭环继续第17步。

> **当前后端边界（SPEC §57/67）**：SQ组合输出Store槽位图/epoch，ROB批量校验完成，历史实际Cache后端
> 764字段严格门禁见sync/README §49。退休仍读旧ROB ready；本轮取指/译码周期保持，
> 两软件Release/_DEBUG各18＋6严格golden一致，前端专项仍按第16步推进。

> **当前存储结构区分（SPEC §55）**：用户确定RTL D$寄存器化并映射软件数据缓存
> 正常时序，6378/14模块三端全等；I$仍为同步SRAM，取指FSM/前端完整同步属于
> 第16步，不能用数据缓存通过替代取指验收。具体合同sync/README §47。

> SQ退休/缓冲/发送已按软件同步RTL（SPEC §45），Store退休不释放SQ额度，满时实际
> 发送的下一拍才恢复issue。RTL HALT提交停issue、SQ请求全部接受后才输出halted；
> 真实分支恢复保留已提交Store。RTL14/14及两软件18＋6严格活动基线保持。

> LQ满队列预算已统一为真实ROB提交拍末腾位、下一拍受理（SPEC §44）；新RS仍下拍
> 派发，LQ1432步一致。两软件18＋6严格新golden clock11,967,396/690,814，旧值保留历史。

> 主树LQ第一项竞争修复采用本拍新槽初始化最后写（SPEC §43），真实新分配保持NOTREADY，
> 不改变新RS下拍派发/旧态读取边界。两软件18＋6严格结果/退休/周期/统计一致，golden保持。

> 第10步DIV消费同拍接收/严格年轻flush及真实返回后端通过（SPEC §40）；18 ISA新clock
> 总和11,967,406，六IPC仍690,826。解码后返回链5000步500字段一致，完整前端仍依第16步验收。

> 第9步MUL解码后返回链已验收：三级弹性/单槽结果，n+3 CDB/n+4依赖派发；真实两池/
> 重命名链2302步508字段四方一致。前端接收周期依第16步验证，详见SPEC §38。

> **第8步已修正方向训练**：BRU→BPU携带真实actualTaken，条件真且offset=4训练1；
> 方向预测错误与next-PC错误分开，后者才产生地址恢复。原准确率统计仍按nextPC，不改称方向正确率。
> 真BPU控制链1518步/2230字段含全表/GHR/checkpoint一致；完整前端与RAS策略另验（SPEC §35）。

> 第8步现状观察：条件真且目标为PC+4时，软件BPU训练taken=0，RTL真实条件taken=1；
> nextPC相同仍不代表方向表/GHR训练语义相同。表状态尚未对拍，训练输入对齐目标见SPEC §34。

> 第7步地址结果级已与RTL统一为单槽；AGU旧结果取消与更老新派发可同拍发生。
> 前端交付的访存指令仍先分配LSQ再入RS；18+6严格回归总clock12,246,204保持。
> 后续地址整链1195步四方一致，LSQ接收及Store广播已匹配生产者tag；实际decoder/BPU触发另验。

> 第6步decoder之后的普通整数整链已验收：真实重命名/RS/ALU/CDB/PRF/ROB在同一
> 时钟域逐拍一致；实际前端输入与BPU恢复触发仍在前端专门步骤验证。

> 第5步flush依旧禁止新分配；已在RS中的严格更老项可在执行端ready时继续派发。
> Integer三端正常域1131步/2262点通过，完整前端/执行恢复周期继续按对应阶段核对。

> 第4步后端FIFO基础域已对齐：前端交付指令由三端一致的ROB/PRF准入及FIFO分配
> 处理。1293步/2586点模块对拍通过，软件18+6严格周期保持12,246,204。

> **2026-10-06 第3步收官**：FQ/IQ使用3-bit epoch，各4项；IQ满时actualIssue
> 可接收旧FQ队首，FQ满且本拍弹出时可接收ICache返回；FQ入队与ICache.popConsume
> 是同一事件。拍初haltFetched阻止后续接收，组合haltSignal仅在本拍真实接收HALT
> 时抑制新取指并锁存停取；flush优先。ICache请求满时pop可同拍补入，空队列不直通。
> 主树tick保持读快照写目标。下方2026-10-01的数值为历史，当前D$8 KB、总
> clock12,246,204，见活动benchmark。同行回填供数可完成同拍新接受的ICache槽，
> 拍末ready、下一拍按序交付；取指/IMEM准入与SRAM读口门控统一识别该旁路。

> 负责"把指令送进乱序核心"：预测下一个 PC、沿预测路径取指、按序预译码并排队。前端只产生**顺序的指令流**——乱序执行由后端处理。
> 相关实现：
> `FetchUnit`(取指控制流)
> `InstructBuffer`(32位指令缓冲队列)
>  `Decoder`+`DecodeUnit`(解码器+微指令缓冲队列)
>  `BPU`(Tournament 分支预测器)。

存储部件（ICache/IMEM）的行为在 [缓存与存储层次](cache.md) 中描述，本文只说明取指逻辑如何使用它们。

> 实现词汇：主树用周期初快照与 `tick()` 表达时序更新，模板树用 `Wire`、
> `Register` 与 `work()/sync()` 表达同一硬件语义。下文统一使用“组合决策”和
> “周期更新”，只在映射源码时区分这两套词汇。
>
> **当前活动取指版本（2026-10-01）**：两树 ICache 均为 1 KB、64 B 行、单端口同步 SRAM，
> 队首命中最早 n+1 拍旁路进入 FQ。主树、模板 Release 与 `_DEBUG` 均通过 18/18 x10+cycles 门禁。
> 当前测试结果见 [benchmarks.md](benchmarks.md)，阶段记录见 [process.md](process.md)。
> 同日数据侧采用 16 KB/64 B 行直接映射 DCache，活动总 clock 12,209,929；缓存几何参数
> 统一由组数与行长派生，取指端口与 n+1 队首交付契约见 [cache.md](cache.md)。

---

## 1. 边界与职责

```
                 ┌──────────────── 前端 ─────────────────────────┐
  FetchDecision ─►│ FetchUnit(PC/halt) → ICache* → IMEM*         │
  （BPU 预测）     │        ▼                                    │
                    │ InstructBuffer(FQ, 2槽/2可用) → IQ(2槽/2可用)│
                   └──────────────┬──────────────────────────────┘
                                  ▼（进入后端发射）
```

| 模块 | 职责 | 备注 |
|------|------|------|
| `FetchUnit` | PC 寄存器与 halt 闩锁（`programCounter` / `haltFetched`） | 每周期一个 `FetchDecision` 有效即推进 PC |
| `InstructBuffer`（FQ） | 2 个物理槽、2 条有效指令；epoch 环形指针，条目为 `{raw, pc, predictedPC, ckptId}` | 预译码为 RAS/BTB 提供精确跳转类型 |
| `Decoder` / `DecodeUnit` | 指令译码 + epoch 队列 IQ（2 个物理槽、2 条有效 Uop） | `Uop` 携带执行与恢复元数据 |
| `BPU` | 方向预测（Tournament）+ 目标预测（BTB/RAS/SARAS） | 见 §4 |

---

## 2. 取指数据流

每个周期由组合逻辑求值一次取指决策（主树位于 `comb()`，模板树由
`FetchDecision` 输出 `Wire` 表达）：

```
fillFire = IMEM.return.valid && ICache.selectRefill(return).valid && !needSquash
refillHit = fillFire && sameLine(PC, IMEM.return.lineAddr)
readBlocked = fillFire && ICache.hit(PC) && !refillHit
decodeCanAccept = IQ.canAccept(actualIssue)
fqPop = !needSquash && !FQ.isEmpty() && decodeCanAccept
fqCanAccept = !needSquash && FQ.canAccept(fqPop)
popConsume = fqCanAccept && ICache.isReturnReady() && !haltFetched
haltSignal = haltFetched || (popConsume && ICache.returnRaw() == HALT)
memCanAccept = (!ICache.isRequestFull() || popConsume) &&
               (!IMEM.isRequestFull() || refillHit) && !readBlocked
FetchDecision = build(BPU, PC, squashDetect, haltSignal, fqCanAccept, memCanAccept)
```

`isFull()/isRequestFull()` 表示拍初实际占用已满；`CanAccept` 表示考虑本拍真实出队、
回填旁路和端口仲裁后的接收能力。模板使用正向 `fetchCtx.fqCanAccept`、
`fetchCtx.memCanAccept` 与 FQ 输入 `decodeCanAccept`，主树 `build` 的两个准入参数
同样为正向语义；最终取指授权为无 squash、无 halt 且两项准入均成立。

主树先求回填接受事件与读口冲突，再构建本拍 `FetchDecision`，最后分发给 FetchUnit、
ICache、IMEM、BPU 和 SRAM 端口。`valid` 表示已获准的取指请求；所有消费者必须使用同一拍
的决定。若在输入分发后才构建，消费者会收到上一拍决定，造成 PC、容量背压与端口使能错拍。
模板由 `refillValid()` / `refillSlot()` 和 ICache Output `fillFire/readBlocked` 表达同一选择与仲裁；
`readBlocked` 必须读取未门控的 FetchUnit PC，不能依赖 `BPU.outPC` 的取指准入门控。

取指被**门控停止**当且仅当以下任一成立：

- `squashDetect.needSquash`（有恢复在途，前端整窗清空后从目标 PC 重启）；
- halt 已被闩锁或本拍真实接收 HALT（见 §2.2）；
- FQ 满且本拍没有向 IQ 转移队首（背压）；
- ICache 请求队列满且本拍没有向 FQ 交付队首；
- IMEM 请求队列满且当前 PC 不能由本拍同行回填直接供数；
- 同拍有获准回填写，当前 PC 命中 ICache 且不属于回填行，无法获得单端口 SRAM 读口。

这些条件反压取指级，后端和数据访存继续推进；新的 miss 不需要 ICache 读口，可与回填写同拍准入。

### 2.1 命中 / 缺失路径

- **命中**：无需访问 IMEM；第 n 拍接受请求并读取 SRAM。第 n+1 拍若保存的读身份
  对应存活、未就绪队首，`headReadReady()` 让 `isReturnReady()/returnRaw()` 直接旁路
  旧 SRAM 输出，FQ 无背压时当拍接收，PC/预测 PC/ckptId 仍来自队首请求槽。
  已消费的读跳过落槽；非队首或未消费的读落槽保存，再按序交付。
- **缺失**：以**64 B 行对齐地址**（`pc & ~0x3F`）向 IMEM 发起整行请求；20 周期主存
  服务后返回 `LineReturn` 16-word 总线。组合逻辑从旧队首开始逐一检查 4 槽，将 critical word
  写入第一个存活、未就绪、行地址匹配的 miss 槽。年轻请求可先完成，交付仍保持队首顺序。
- **回填握手**：`fillFire` 同时确认 IMEM 返回被消费、ICache 请求槽完成及 SRAM 行写入；
  没有匹配请求时 IMEM 保持返回。等待 SRAM 结果的 hit 不作为 miss 匹配。

ICache 结果进入 FQ 的握手是组合谓词：`¬needSquash ∧ ICache返回就绪 ∧ ¬haltFetched ∧ FQ.canAccept(fqPop)`；
`popConsume` 表示该结果已被 FQ 接收，ICache 随后清除自己的持有状态。FQ 到 IQ
是另一组握手：FQ非空且IQ未满或本拍实际Issue时，旧FQ队首译码入IQ，同时弹出FQ头。

### 2.2 halt 闩锁

两树使用 `popConsume && returnRaw()==0x0ff00513` 生成当拍 HALT 接收事件
`haltSignal`。在尚未停取时，这使 HALT 进入 FQ 与 `haltFetched` 锁存发生在同一拍；
FQ 满时保持队首并等待，不能先锁存停取而把 HALT 自己挡在队列之外。该标记随后进入 ROB，
提交并完成访存排空后停机（程序出口 = 停机时 `x10` 低 8 位）。

### 2.3 预译码（pre-decode, `scanJump`）

取指结果推入 FQ 时，FQ 在 `lastPush` 缓存里对**新入队的那条指令**做一次静态
扫描，只关心无条件跳转族（`jal`/`jalr`），**条件分支刻意排除**：

- RISC-V 非特权 ISA 通过 `jal`/`jalr` 的 `rd`、`rs1` 是否为链接寄存器
  `x1/ra` 或 `x5/t0` 隐式编码 RAS hint [[1]](#front-ref-1)；
- `jal`：`rd∈{x1,x5}` ⇒ `isCall`，并静态解出 `jalTarget`；
- `jalr`（用户指定策略）：`rd` 为link ⇒ push/isCall，`rs1` 为link ⇒ pop/isRet，
  两个标志独立；双link包括`rd==rs1`都先pop再push。非link/nonlink无动作。
  所有spec/arch/replay使用此顺序；JAL立即数字段不作为rs1。

预译码的意义：**RAS 维护与 BTB 类型训练不再依赖预测表命中**——跳转指令即使
从未被 BTB 记录，前端也能正确推送 call/ret 语义（修复了 RAS 冷启动漏栈问题）。

---

## 3. 译码进队

`DecodeUnit` 的周期更新从 FQ 头取原始指令字，`Decoder::decode` 生成 `Uop`
（类型/opcode/funct3/funct7/rd/rs1/rs2/imm/pc/halt/allocDest/predictedPC/
ckptId），压入IQ。FQ头能否消费由IQ拍初满状态与本拍实际Issue共同决定。发射侧
（后端 IssueArbiter）从 IQ 头取指，见
[`backend.md`](backend.md) §2。

---

## 4. 分支预测（BPU）

方向预测与目标预测分开：方向侧采用 **Tournament**，以 chooser 在局部与全局
2-bit 预测器之间选择 [[2]](#front-ref-2)[[3]](#front-ref-3)；目标侧按控制流类型拆分，
总体分工也参考香山开源处理器的公开设计 [[10]](#front-ref-10)[[11]](#front-ref-11)。
容量、哈希和单周期预测时序均为本项目配置，并不等同于引用设计的具体实现。
预测在**取指当拍**完成（**这可能带来主频问题，等待后续综合后进行评估**），
结果随 `FetchDecision` 携带。

### 4.1 方向预测：Tournament

| 部件 | 配置 | 说明 |
|------|------|------|
| localPHT | 256 × 2-bit | 索引 = `(PC >> 2) & 255`；计数器 `>=2` 预测 taken |
| globalPHT | 256 × 2-bit | 索引 = `((PC >> 2) ^ GHR[7:0]) & 255`；计数器 `>=2` 预测 taken |
| selector | 256 × 2-bit | 与 globalPHT 使用同一索引；`>=2` 选择 global，否则选择 local |
| condSeen 过滤器 | 512 × 1-bit | 条件分支解析时置位；取指侧 `btbHit ∨ condSeen` 才移位 GHR——避免"从不 taken 的分支不留历史、BTB 驻留漂移改变历史成员"两类缺口 |

三张 2-bit 表的计数器均初始化为 1（弱 not-taken）。条件分支解析后，localPHT 与
globalPHT 都按实际结果饱和更新；仅当两者预测不同，selector 才朝本次预测正确的一侧更新。
selector 选出的结果记为 `directionTaken`。最终条件分支方向还受 BTB 身份门控：
`taken = btbHit && directionTaken`；若 BTB 命中项标记为无条件跳转，则覆盖为 taken。

### 4.2 目标预测（跳去哪）

| 部件 | 配置 | 说明 |
|------|------|------|
| BTB | 64 条目 | 经典 Branch Target Buffer 的项目实现 [[5]](#front-ref-5)；语义项为 `{PC[31:8], target[31:2], state}`，`state` 编码 invalid/conditional/unconditional/return（后两者必 taken）；命中且无条件 ⇒ 必 taken |
| RAS | 16 条目返回地址；`specRAS` 与 `archRAS` 各一份 | call 压入返回地址、return 弹栈并预测目标 [[7]](#front-ref-7)；恢复从提交基线重放存活 ROB，处理投机错位 [[8]](#front-ref-8) |

当前源码不使用 `times` 去重或 SARAS 日志；旧方案的面积与实验数据保存在 benchmark 历史记录中。

> 目标侧不设间接目标缓存（Target Cache）与提交级 BHT：方向侧改用 Tournament 后，BHT 仅服务
> 间接目标哈希，二者构成闭环；活动语料中唯一的真间接站点为单目标，收益不可观测，遂按
> 面积/效率权衡删除（见根 `docs/benchmarks.md` 复核记录）。

> 活动 RV32IM 镜像无压缩指令，PC 和跳转目标均为 4-byte 对齐；BTB 使用 `PC[7:2]` 索引、
> `PC[31:8]` tag 与 `target[31:2]`，每项为 56 bit。参考树可保留等价的全宽宿主载体，模板
> `Register` 实现以此作为物理状态位宽。

### 4.3 GHR 与 checkpoint

- **GHR 移位**：8-bit GHR 在取指侧于 `btbHit ∨ condSeen` 时随预测结果移位（`FetchDecision`
  携带 `shift/shiftValue`）；BRU训练condSeen使后续历史成员资格不依赖BTB驻留，
  解析本身不追加actual方向到live GHR，恢复采用拍初取指GHR快照。
- **checkpoint**：每次获准取指消耗一个 `ckptId`。活动池 `CKPT_CAP=32`，大于
  `CKPT_LIVE_MAX = ROB16 + ICache request4 + FQ2 + IQ2 = 24`，由 `static_assert`
  守配置预算，§59真实流水检查活owner不被覆盖；逻辑 ID 与模板载体为 5 bit
  （`CKPT_ID_WIDTH`）。
  当前 BPU checkpoint 仅保存 **8-bit GHR**。RAS 从旧 `archRAS` 提交基线恢复，按 ROB
  从 head 到 `SquashTag`（含自身）的存活前缀重放 call/ret；不保存 per-checkpoint RAS 阵列。
  pop-push会在depth不变时覆盖顶，恢复复制完整arch内容再有序重放；年轻覆盖应撤销，
  边界本身双动作保留。软件与当前RTL均从旧提交基线包含同拍头项，每项恰好重放一次。
  RTL内部深度位宽为`$clog2(RAS_ENTRIES+1)`（默认5bit），恢复候选独立计算后再按flush选择；
  这是位宽/组合结构等价优化，不改变软件算法、预测及恢复周期。
- **训练**：BRU 条件分支结果更新 localPHT/globalPHT/selector、condSeen 与目标侧状态；
  CDB 的 JAL/JALR 转移只更新目标侧。两个训练口共享周期初旧快照；表更新按资源固定写口
  仲裁（`fetch > cdb > bru`，BTB 的 identity/state 与 target 分两组），每个物理 Register
  每拍至多一次写。获准取指维护投机 GHR，FQ 预译码维护 `specRAS`，ROB 提交维护
  `archRAS`；squash 优先恢复投机态，CDB 训练侧不直接更新投机 GHR/RAS。
  **方向表不被 JAL/JALR 恒跳指令污染**。

---

## 5. 与后端 / 存储层次的接口

- **误预测恢复**：解析点（BRU 出队结果、CDB 上 JAL/JALR）在后端判对错；需要
  squash 时进入 `FlushArbiter` 排队（见 [`backend.md`](backend.md) §6）。前端侧
  的恢复 = 按 `ckptId` 恢复 GHR、从 `archRAS` 基线重放存活 ROB 的 call/ret，
  并整窗清空投机取指/译码队列后从 `SquashPC` 重新取指。
- **存储层次**：ICache 命中的取指数据来自 `cache.md` 描述的 L1I 行阵；缺失回填
  由 IMEM（20 周期主存延迟）承担。

---

## 6. 关键规格

| 项 | 规格 |
|----|------|
| 取指带宽 | 每周期至多 1 条；请求队列与 FQ 有空间、无 squash/未闩锁 halt，且命中读获得 SRAM 端口 |
| 两树 ICache | 1 KB · 16×64 B · 单端口同步 SRAM · 4 项请求队列；队首命中最早 n+1 拍进入 FQ |
| FQ / IQ | 各2项，epoch指针；IQ满时actualIssue可替换，FQ满时真实出队可补入 |
| 方向预测 | Tournament：localPHT 256×2b · globalPHT 256×2b · selector 256×2b · GHR 8b |
| 目标预测与身份状态 | BTB 64 · specRAS/archRAS 各 16 · condSeen 512b |
| checkpoint | ckptId 池 32（存活上界 24；逻辑与模板运输载体均 5 bit） |
| 预译码 | FQ 尾 jal/jalr 静态分类（call/ret/indirect + 静态 jal 目标） |
| halt | 队首 = `0x0ff00513` 且实际交付 FQ ⇒ latch haltFetched 并当拍停取 |

## 相关文档

- [`../README.md`](../README.md) — 总览 / 数据通路图 / 周期模型
- [`cache.md`](cache.md) — L1I（ICache/IMEM）存储行为与主存延迟
- [`backend.md`](backend.md) — 发射、执行、写回、提交与 squash 恢复

## 参考文献

1. <a id="front-ref-1"></a>RISC-V International, *The RISC-V Instruction Set
   Manual, Volume I: Unprivileged Architecture*, RV32I Version 2.1,
   §“Unconditional Jumps,” 2026.
   https://docs.riscv.org/reference/isa/v20260120/unpriv/rv32.html#_unconditional_jumps
2. <a id="front-ref-2"></a>Scott McFarling, “Combining Branch Predictors,”
   Digital Equipment Corporation Western Research Laboratory, Technical Note
   TN-36, 1993.
   https://www.hpl.hp.com/techreports/Compaq-DEC/WRL-TN-36.pdf
3. <a id="front-ref-3"></a>R. E. Kessler, “The Alpha 21264 Microprocessor,”
   *IEEE Micro*, vol. 19, no. 2, pp. 24–36, 1999.
   https://doi.org/10.1109/40.755465
4. <a id="front-ref-4"></a>Tse-Yu Yeh and Yale N. Patt, “Two-Level Adaptive
   Training Branch Prediction,” in *Proceedings of MICRO-24*, pp. 51–61, 1991.
   https://doi.org/10.1145/123465.123475
5. <a id="front-ref-5"></a>J. K. F. Lee and A. J. Smith, “Branch Prediction
   Strategies and Branch Target Buffer Design,” *Computer*, vol. 17, no. 1,
   pp. 6–22, 1984. https://doi.org/10.1109/MC.1984.1658927
6. <a id="front-ref-6"></a>Po-Yung Chang, Eric Hao, and Yale N. Patt, “Target
   Prediction for Indirect Jumps,” in *Proceedings of ISCA ’97*, pp. 274–283,
   1997. https://doi.org/10.1145/264107.264209
7. <a id="front-ref-7"></a>David R. Kaeli and Philip G. Emma, “Branch History
   Table Prediction of Moving Target Branches Due to Subroutine Returns,” in
   *Proceedings of ISCA ’91*, pp. 34–42, 1991.
   https://doi.org/10.1145/115952.115957
8. <a id="front-ref-8"></a>Kevin Skadron, P. S. Ahuja, Margaret Martonosi,
   and Douglas W. Clark, “Improving Prediction for Procedure Returns with
   Return-Address-Stack Repair Mechanisms,” in *Proceedings of MICRO-31*,
   pp. 259–271, 1998. https://doi.org/10.1109/MICRO.1998.742787
9. <a id="front-ref-9"></a>Guopeng Wang, Xiangdong Hu, Ying Zhu, and Yingnan
   Zhang, “Self-Aligning Return Address Stack,” in *2012 IEEE Seventh
   International Conference on Networking, Architecture, and Storage*,
   pp. 278–282, 2012. https://doi.org/10.1109/NAS.2012.49
10. <a id="front-ref-10"></a>Yinan Xu et al., “Towards Developing High
    Performance RISC-V Processors Using Agile Methodology,” in *Proceedings of
    MICRO-55*, pp. 1178–1199, 2022.
    https://doi.org/10.1109/MICRO56248.2022.00080
11. <a id="front-ref-11"></a>OpenXiangShan Project, “XiangShan Bpu Design
    Document,” Kunminghu-V3 design documentation, version V3, draft, 2026.
    https://docs.xiangshan.cc/projects/design/en/kunminghu-v3/frontend/BPU/
