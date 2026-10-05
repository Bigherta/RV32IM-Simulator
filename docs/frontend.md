# 前端子系统：取指 · 预译码 · 译码 · 分支预测

> **2026-10-05 3A+3B-1**：FQ/IQ使用3-bit epoch，各4项；IQ满时actualIssue
> 可接收旧FQ队首，FQ pop与IQ push相同。isFull仍是占用满，canAccept表达本拍能力。
> FQ上游/ICache消费/HALT/新取指lookahead留3B-2，主树tick保持读快照写目标。
> 下方 2026-10-01 的验收数值为历史，当前 D$8 KB、总 clock12,246,363，见活动 benchmark。

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
                   │ InstructBuffer(FQ, 4槽/4可用) → IQ(4槽/4可用)│
                   └──────────────┬──────────────────────────────┘
                                  ▼（进入后端发射）
```

| 模块 | 职责 | 备注 |
|------|------|------|
| `FetchUnit` | PC 寄存器与 halt 闩锁（`programCounter` / `haltFetched`） | 每周期一个 `FetchDecision` 有效即推进 PC |
| `InstructBuffer`（FQ） | 4 个物理槽、4 条有效指令；epoch 环形指针，条目为 `{raw, pc, predictedPC, ckptId}` | 预译码为 RAS/BTB 提供精确跳转类型 |
| `Decoder` / `DecodeUnit` | 指令译码 + epoch 队列 IQ（4 个物理槽、4 条有效 Uop） | `Uop` 携带执行与恢复元数据 |
| `BPU` | 方向预测（Tournament）+ 目标预测（BTB/RAS/SARAS） | 见 §4 |

---

## 2. 取指数据流

每个周期由组合逻辑求值一次取指决策（主树位于 `comb()`，模板树由
`FetchDecision` 输出 `Wire` 表达）：

```
fillFire = IMEM.return.valid && ICache.selectRefill(return).valid && !needSquash
readBlocked = fillFire && ICache.hit(PC)
FetchDecision = build(BPU, PC, squashDetect, haltFetched, FQ.isFull(),
                      ICache.isRequestFull() || IMEM.isRequestFull() || readBlocked)
```

主树先求回填接受事件与读口冲突，再构建本拍 `FetchDecision`，最后分发给 FetchUnit、
ICache、IMEM、BPU 和 SRAM 端口。`valid` 表示已获准的取指请求；所有消费者必须使用同一拍
的决定。若在输入分发后才构建，消费者会收到上一拍决定，造成 PC、容量背压与端口使能错拍。
模板由 `refillValid()` / `refillSlot()` 和 ICache Output `fillFire/readBlocked` 表达同一选择与仲裁；
`readBlocked` 必须读取未门控的 FetchUnit PC，不能依赖 `BPU.outPC` 的取指准入门控。

取指被**门控停止**当且仅当以下任一成立：

- `squashDetect.needSquash`（有恢复在途，前端整窗清空后从目标 PC 重启）；
- halt 已被闩锁（`haltFetched`，见 §2.2）；
- FQ 满（背压）；
- ICache/IMEM 请求队列满（回填在途）；
- 同拍有获准回填写，且当前 PC 命中 ICache，无法获得单端口 SRAM 读口。

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

ICache 结果进入 FQ 的握手是组合谓词：`ICache 行返回就绪 ∧ ¬haltFetched ∧ ¬FQ满`；
`popConsume` 表示该结果已被 FQ 接收，ICache 随后清除自己的持有状态。FQ 到 IQ
是另一组握手：FQ非空且IQ未满或本拍实际Issue时，旧FQ队首译码入IQ，同时弹出FQ头。

### 2.2 halt 闩锁

两树使用 `!FQ.full && ICache.isReturnReady() && returnRaw()==0x0ff00513` 生成
`haltSignal`。在尚未停取时，这使 HALT 进入 FQ 与 `haltFetched` 锁存发生在同一拍；
FQ 满时保持队首并等待，不能先锁存停取而把 HALT 自己挡在队列之外。该标记随后进入 ROB，
提交并完成访存排空后停机（程序出口 = 停机时 `x10` 低 8 位）。

### 2.3 预译码（pre-decode, `scanJump`）

取指结果推入 FQ 时，FQ 在 `lastPush` 缓存里对**新入队的那条指令**做一次静态
扫描，只关心无条件跳转族（`jal`/`jalr`），**条件分支刻意排除**：

- RISC-V 非特权 ISA 通过 `jal`/`jalr` 的 `rd`、`rs1` 是否为链接寄存器
  `x1/ra` 或 `x5/t0` 隐式编码 RAS hint [[1]](#front-ref-1)；
- `jal`：`rd∈{x1,x5}` ⇒ `isCall`，并静态解出 `jalTarget`；
- `jalr`：`rd` 为链接寄存器 ⇒ `isCall`；`rs1` 为链接寄存器且 `rd` 非链接 ⇒
  `isRet`（函数指针/PLT/vtable 调用因 `rd` 为链接寄存器而被归为调用）。

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
| RAS | 32 条目返回地址；`specRAS` 与 `archRAS` 各一份 | call 压入返回地址、return 弹栈并预测目标 [[7]](#front-ref-7)；恢复从提交基线重放存活 ROB，处理投机错位 [[8]](#front-ref-8) |

当前源码不使用 `times` 去重或 SARAS 日志；旧方案的面积与实验数据保存在 benchmark 历史记录中。

> 目标侧不设间接目标缓存（Target Cache）与提交级 BHT：方向侧改用 Tournament 后，BHT 仅服务
> 间接目标哈希，二者构成闭环；活动语料中唯一的真间接站点为单目标，收益不可观测，遂按
> 面积/效率权衡删除（见根 `docs/benchmarks.md` 复核记录）。

> 活动 RV32IM 镜像无压缩指令，PC 和跳转目标均为 4-byte 对齐；BTB 使用 `PC[7:2]` 索引、
> `PC[31:8]` tag 与 `target[31:2]`，每项为 56 bit。参考树可保留等价的全宽宿主载体，模板
> `Register` 实现以此作为物理状态位宽。

### 4.3 GHR 与 checkpoint

- **GHR 移位**：8-bit GHR 在取指侧于 `btbHit ∨ condSeen` 时随预测结果移位（`FetchDecision`
  携带 `shift/shiftValue`）；条件分支的解析结果也回填历史——历史成员资格不依赖
  BTB 驻留。
- **checkpoint**：每次获准取指消耗一个 `ckptId`。活动池 `CKPT_CAP=32`，大于
  `CKPT_LIVE_MAX = ROB16 + ICache request4 + FQ4 + IQ4 = 28`，由 `static_assert`
  守住不会在仍存活时复用 ID；逻辑 ID 与模板运输载体均按派生宽度收紧为 5 bit
  （`CKPT_ID_WIDTH`）。
  当前 BPU checkpoint 仅保存 **8-bit GHR**。RAS 从旧 `archRAS` 提交基线恢复，按 ROB
  从 head 到 `SquashTag`（含自身）的存活前缀重放 call/ret；不保存 per-checkpoint RAS 阵列。
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
| FQ / IQ | 各4项，3-bit epoch；IQ满时actualIssue可替换，FQ上游暂按满停收 |
| 方向预测 | Tournament：localPHT 256×2b · globalPHT 256×2b · selector 256×2b · GHR 8b |
| 目标预测与身份状态 | BTB 64 · specRAS/archRAS 各 32 · condSeen 512b |
| checkpoint | ckptId 池 32（存活上界 28；逻辑与模板运输载体均 5 bit） |
| 预译码 | FQ 尾 jal/jalr 静态分类（call/ret/indirect + 静态 jal 目标） |
| halt | ready 队首 = `0x0ff00513` 且 FQ 不满 ⇒ 交付并 latch haltFetched 停取 |

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
