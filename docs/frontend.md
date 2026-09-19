# 前端子系统：取指 · 预译码 · 译码 · 分支预测

> 负责"把指令送进乱序核心"：预测下一个 PC、沿预测路径取指、按序预译码并排队。前端只产生**顺序的指令流**——乱序执行由后端处理。
> 相关实现：
> `FetchUnit`(取指控制流)
> `InstructBuffer`(32位指令缓冲队列)
>  `Decoder`+`DecodeUnit`(解码器+微指令缓冲队列)
>  `BPU`(TAGE分支预测器)。

存储部件（ICache/IMEM）的行为在 [缓存与存储层次](cache.md) 中描述，本文只说明取指逻辑如何使用它们。

> 实现词汇：主树用周期初快照与 `tick()` 表达时序更新，模板树用 `Wire`、
> `Register` 与 `work()/sync()` 表达同一硬件语义。下文统一使用“组合决策”和
> “周期更新”，只在映射源码时区分这两套词汇。

---

## 1. 边界与职责

```
                 ┌──────────────── 前端 ─────────────────────────┐
  FetchDecision ─►│ FetchUnit(PC/halt) → ICache* → IMEM*         │
  （BPU 预测）     │        ▼                                    │
                   │ InstructBuffer(FQ, 4槽/3可用) → IQ(4槽/3可用)│
                   └──────────────┬──────────────────────────────┘
                                  ▼（进入后端发射）
```

| 模块 | 职责 | 备注 |
|------|------|------|
| `FetchUnit` | PC 寄存器与 halt 闩锁（`programCounter` / `haltFetched`） | 每周期一个 `FetchDecision` 有效即推进 PC |
| `InstructBuffer`（FQ） | 4 个物理槽、最多 3 条有效指令的环形取指队列，条目为 `{raw, pc, predictedPC, ckptId}` | 预译码为 RAS/BTB 提供精确跳转类型 |
| `Decoder` / `DecodeUnit` | 指令译码 + Uop 环形队列 IQ（4 个物理槽、最多 3 条有效 Uop） | `Uop` 携带执行与恢复元数据 |
| `BPU` | 方向预测（TAGE）+ 目标预测（BTB/TargetCache/RAS/SARAS） | 见 §4 |

---

## 2. 取指数据流

每个周期由组合逻辑求值一次取指决策（主树位于 `comb()`，模板树由
`FetchDecision` 输出 `Wire` 表达）：

```
FetchDecision = build(BPU, PC, squashDetect, haltFetched, FQ.isFull(),
                      ICache.isRequestFull() || IMEM.isRequestFull())
```

取指被**门控停止**当且仅当以下任一成立：

- `squashDetect.needSquash`（有恢复在途，前端整窗清空后从目标 PC 重启）；
- halt 已被闩锁（`haltFetched`，见 §2.2）；
- FQ 满（背压）；
- ICache/IMEM 请求队列满（回填在途）。

### 2.1 命中 / 缺失路径

- **命中**：`ICache.hit(pc)` 成立则无需访问 IMEM，命中指令当拍组包入 FQ；
- **缺失**：以**行对齐地址**（`pc & ~0xF`）向 IMEM 发起整行请求，IMEM 以 20 周期
  主存延迟回填 16 B 行（`LineReturn` 四字总线），回填到达后由 ICache 持有并
  组包供后续取指命中。

ICache 结果进入 FQ 的握手是组合谓词：`ICache 行返回就绪 ∧ ¬haltFetched ∧ ¬FQ满`；
`popConsume` 表示该结果已被 FQ 接收，ICache 随后清除自己的持有状态。FQ 到 IQ
是另一组握手：FQ 非空且 IQ 未满时，译码结果入 IQ，同时弹出 FQ 头。

### 2.2 halt 闩锁

ICache 头返回的指令字若等于停机字 `0x0ff00513`（`li a0, 255`），组合总线
`haltSignal` 置位 → `FetchUnit` latch `haltFetched`，此后停止取指；该 halt 指令
仍照常进入流水线并在后端提交时停机（程序出口 = 停机时 `x10` 低 8 位）。

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
ckptId），压入 IQ。FQ 头是否可被消费由 IQ 的周期初满状态决定。发射侧
（后端 IssueArbiter）从 IQ 头取指，见
[`backend.md`](backend.md) §2。

---

## 4. 分支预测（BPU）

方向预测与目标预测的总体分工参考香山开源处理器的公开设计
[[10]](#front-ref-10)[[11]](#front-ref-11)，方向算法采用 **TAGE 混合预测器**
（一个局部两级 2-bit 基表 + 4 张全局历史标签表）[[2]](#front-ref-2)[[3]](#front-ref-3)，
目标预测按控制流类型拆分。容量、哈希、局部历史基表和单周期预测时序均为本项目配置，
并不等同于香山的具体流水化 BPU。预测在**取指当拍**完成
（**这可能带来主频问题，等待后续综合后进行评估**），结果随 `FetchDecision` 携带
（含供解析期消费的 `TAGESCMeta`）。

### 4.1 方向预测：TAGE

| 部件 | 配置 | 说明 |
|------|------|------|
| 基表 T0 | 1024 × 2-bit | 索引 = `PC ⊕ LHT[PC]`；LHT 为 128 条目 × 12-bit **每 PC 局部历史**（非推测更新），是本项目采用的局部两级兜底预测器 [[4]](#front-ref-4) |
| condSeen 过滤器 | 512 × 1-bit | 条件分支解析时置位；取指侧 `btbHit ∨ condSeen` 才移位 GHR——避免"从不 taken 的分支不留历史、BTB 驻留漂移改变历史成员"两类缺口 |
| 标签表 T1–T4 | 每表 128 条目 × 8-bit tag | 历史长度 {6, 12, 24, 48}；7-bit 索引与 8-bit tag 使用不同宽度的折叠历史并与 `pc` 混合；几何历史长度、部分标签和折叠历史来自 TAGE [[2]](#front-ref-2)[[3]](#front-ref-3) |
| TageEntry | `{valid, tag(8b), ctr(3b), u(2b)}` | provider = 最长命中的历史表；alt = 次长命中（无次命中回退 T0），沿用 TAGE 的 provider/alternate 结构 [[2]](#front-ref-2)[[3]](#front-ref-3) |
| useAltOnNa | 128 条目 × 4-bit（初值偏 alt） | 弱 provider（`ctr==3/4`）时学习"此 PC 改用 alt 是否更准"；机制来自 TAGE，容量与索引是项目配置 [[3]](#front-ref-3) |
| 分配/老化 | 8-bit Galois LFSR（taps `0xB8`）抽签 | TAGE 用 usefulness 位约束替换并通过老化回收表项 [[2]](#front-ref-2)[[3]](#front-ref-3)；LFSR、候选扫描及每 64 次 `u >>= 1` 是项目策略 |

预测选取遵循 TAGE 的 provider/alternate 结构：provider 命中则以其 `ctr≥4` 为方向，
弱计数时按 useAltOnNa 决定是否改信 alt；无 provider 命中回退 T0
[[2]](#front-ref-2)[[3]](#front-ref-3)。

### 4.2 目标预测（跳去哪）

| 部件 | 配置 | 说明 |
|------|------|------|
| BTB | 64 条目 | 经典 Branch Target Buffer 的项目实现 [[5]](#front-ref-5)；携带 `unconditional/isCall/isRet/isIndirect` 类型，命中且无条件 ⇒ 必 taken |
| Target Cache | 32 条目 | JALR 专用，采用"同一静态间接跳转可有多个上下文相关目标"的 Target Cache 思路 [[6]](#front-ref-6)；本实现用 256×8b 提交级局部历史 BHR，按 `pc ⊕ BHR` 哈希 |
| RAS | 8 条目 `{retPC, times}` | RAS 用 call 压入的返回地址预测 return [[7]](#front-ref-7)；`times` 将连续相同返回地址压成计数项，是项目的递归去重策略；投机错位与修复机制见 [[8]](#front-ref-8) |
| SARAS | 16 条目 `{addr, index, times}` | 受 Self-Aligning Return Address Stack 启发的恢复日志 [[9]](#front-ref-9)；论文使用传统 RAS、自对齐队列与栈顶计数器，本项目字段和 call-dedup/ret 撤销规则是具体适配，不宣称逐字段等同 |

### 4.3 GHR 与 checkpoint

- **GHR 移位**：取指侧在 `btbHit ∨ condSeen` 时随预测结果移位（`FetchDecision`
  携带 `shift/shiftValue`）；条件分支的解析结果也回填历史——历史成员资格不依赖
  BTB 驻留。
- **checkpoint**：每次取指消耗一个 `ckptId`（池 `CKPT_CAP=64 ≥ ROB_CAP`，
  有 `static_assert` 守护）。`BPUSnapshot` 存 **GHR / AlignQueue 头尾 / RAS_top**
  三项（均为 uint8 环绕指针）；TAGE 折叠视图**不做 checkpoint**——它们是 GHR
  快照的纯函数，`recoverCheckPoint()` 恢复寄存器后直接 `refold` 重算。
- **元数据传递**：`TAGESCMeta{provIdx, provCtr, provU, altPred, tagePred,
  baseCnt}` 随 `PredictInfo → FetchDecision` 进入 BPU 私有 per-ckptId 池，
  分支解析时消费（训练用）。
- **训练**：两条表更新源（BRU 分支结果、CDB 跳转转移）收敛到**单点原子**训练
  入口，固定序（BRU 候选先），任意流水级调度下行为一致。BRU 侧维护投机态
  GHR/RAS/bpCkpt；CDB 侧只改表，永不触碰投机态。**方向表不被 JAL/JALR 恒跳
  指令污染**（恒跳走 BTB 身份路径，只更新目标侧）。

---

## 5. 与后端 / 存储层次的接口

- **误预测恢复**：解析点（BRU 出队结果、CDB 上 JAL/JALR）在后端判对错；需要
  squash 时进入 `FlushArbiter` 排队（见 [`backend.md`](backend.md) §6）。前端侧
  的恢复 = 按 `ckptId` 恢复 `BPUSnapshot`（GHR/Align/RAS）+ 整窗清空 FQ/IQ 后
  从 `SquashPC` 重新取指。
- **存储层次**：ICache 命中的取指数据来自 `cache.md` 描述的 L1I 行阵；缺失回填
  由 IMEM（20 周期主存延迟）承担。

---

## 6. 关键规格

| 项 | 规格 |
|----|------|
| 取指带宽 | 每周期至多 1 条（FQ 有空位且无背压/无 squash/未闩锁 halt 时） |
| FQ / IQ | 物理槽 4 / 4；环形队列保留一个空槽判满，实际最多容纳 3 / 3 条 |
| 方向预测 | T0 1024×2b · LHT 128×12b · T1–T4 各 128 项（8b tag，hist {6,12,24,48}）· useAltOnNa 128×4b |
| 目标预测 | BTB 64 · Target Cache 32（BHR 256×8b）· RAS 8 · SARAS 16 |
| checkpoint | ckptId 池 64（≥ ROB 16，static_assert 守护） |
| 预译码 | FQ 尾 jal/jalr 静态分类（call/ret/indirect + 静态 jal 目标） |
| halt | ICache 头 = `0x0ff00513` ⇒ latch haltFetched 停取 |

## 相关文档

- [`../README.md`](../README.md) — 总览 / 数据通路图 / 周期模型
- [`cache.md`](cache.md) — L1I（ICache/IMEM）存储行为与主存延迟
- [`backend.md`](backend.md) — 发射、执行、写回、提交与 squash 恢复

## 参考文献

1. <a id="front-ref-1"></a>RISC-V International, *The RISC-V Instruction Set
   Manual, Volume I: Unprivileged Architecture*, RV32I Version 2.1,
   §“Unconditional Jumps,” 2026.
   https://docs.riscv.org/reference/isa/v20260120/unpriv/rv32.html#_unconditional_jumps
2. <a id="front-ref-2"></a>André Seznec and Pierre Michaud, “A Case for
   (Partially) TAgged GEometric History Length Branch Prediction,” *Journal of
   Instruction-Level Parallelism*, vol. 8, 2006.
   https://www.jilp.org/vol8/v8paper1.pdf
3. <a id="front-ref-3"></a>André Seznec, “A New Case for the TAGE Branch
   Predictor,” in *Proceedings of MICRO-44*, pp. 117–127, 2011.
   https://doi.org/10.1145/2155620.2155635
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
