# 前端子系统：取指 · 预译码 · 译码 · 分支预测

> 负责"把指令送进乱序核心"：预测下一个 PC、沿预测路径取指、按序预译码并排队。
> 前端只产生**顺序的指令流**——乱序发生在后端。相关实现：`FetchUnit` /
> `InstructBuffer`(FQ) / `Decoder`+`DecodeUnit`(IQ) / `BPU`。
> [← 返回 README](../README.md)

存储部件（ICache/IMEM）的行为在 [缓存与存储层次](cache.md) 中描述，本文只讲
取指逻辑如何使用它们。

---

## 1. 边界与职责

```
                 ┌──────────────── 前端（本文） ────────────────┐
  FetchDecision ─►│ FetchUnit(PC/halt) → ICache* → IMEM*         │
  （BPU 预测）     │        │ 命中当拍组包 / 缺失经 IMEM 整行回填  │
                   │        ▼                                    │
                   │  InstructBuffer(FQ, 8) ──► DecodeUnit → IQ(16)│
                   └──────────────┬──────────────────────────────┘
                                  ▼（进入后端发射 Issue）
 * ICache / IMEM 的实现细节见 docs/cache.md
```

| 模块 | 职责 | 备注 |
|------|------|------|
| `FetchUnit` | PC 寄存器与 halt 闩锁（`programCounter` / `haltFetched`） | 每周期一个 `FetchDecision` 有效即推进 PC |
| `InstructBuffer`（FQ） | 取指队列，8 项 `{raw, pc, predictedPC, ckptId}` | 含**预译码** `lastPush` 缓存 |
| `Decoder` / `DecodeUnit` | 指令译码 + Uop 队列 IQ（16 项 `UopQueue`） | `Uop` 携带 pc/imm/ckptId/predictedPC/allocDest |
| `BPU` | 方向预测（TAGE）+ 目标预测（BTB/TargetCache/RAS/SARAS） | 见 §4 |

**不在本文件范围**：ICache/IMEM 存储行为 → `cache.md`；LQ/SQ 尾快照的消费方
（squash 恢复）→ `backend.md`。

---

## 2. 取指数据流

每个周期 `CPU::comb()` 组合求值出一次取指决策：

```
FetchDecision = build(BPU, PC, squashDetect, haltFetched, FQ.isFull(),
                      ICache.isRequestFull() || IMEM.isRequestFull())
```

取指被**门控停止**当且仅当以下任一成立：

- `squashDetect.needSquash`（有恢复在途，前端整窗清空后从目标 PC 重启）；
- halt 已被闩锁（`haltFetched`，见 §2.3）；
- FQ 满（背压）；
- ICache/IMEM 请求队列满（回填在途）。

### 2.1 命中 / 缺失路径

- **命中**：`ICache.hit(pc)` 成立则无需访问 IMEM，命中指令当拍组包入 FQ；
- **缺失**：以**行对齐地址**（`pc & ~0xF`）向 IMEM 发起整行请求，IMEM 以 50 周期
  主存延迟回填 16 B 行（`LineReturn` 四字总线），回填到达后由 ICache 持有并
  组包供后续取指命中。

FQ 出队（供译码）的握手是组合谓词：`ICache 行返回就绪 ∧ ¬haltFetched ∧ ¬FQ满`
时取指结果可入队；`popConsume`（ICache 头被 FQ 消费）由 ICache 自己清除状态
（写自有纪律）。

### 2.2 halt 闩锁

ICache 头返回的指令字若等于停机字 `0x0ff00513`（`li a0, 255`），组合总线
`haltSignal` 置位 → `FetchUnit` latch `haltFetched`，此后停止取指；该 halt 指令
仍照常进入流水线并在后端提交时停机（程序出口 = 停机时 `x10` 低 8 位）。

### 2.3 预译码（pre-decode, `scanJump`）

取指结果推入 FQ 时，FQ 在 `lastPush` 缓存里对**新入队的那条指令**做一次静态
扫描，只关心无条件跳转族（`jal`/`jalr`），**条件分支刻意排除**：

- RISC-V RAS 提示（非特权规范）：链接寄存器为 `x1/ra` 与 `x5/t0`；
- `jal`：`rd∈{x1,x5}` ⇒ `isCall`，并静态解出 `jalTarget`；
- `jalr`：`rd` 为链接寄存器 ⇒ `isCall`；`rs1` 为链接寄存器且 `rd` 非链接 ⇒
  `isRet`（函数指针/PLT/vtable 调用因 `rd` 为链接寄存器而被归为调用）。

预译码的意义：**RAS 维护与 BTB 类型训练不再依赖预测表命中**——跳转指令即使
从未被 BTB 记录，前端也能正确推送 call/ret 语义（修复了 RAS 冷启动漏栈问题）。

---

## 3. 译码进队

`DecodeUnit.tick` 从 FQ 头取原始指令字，`Decoder::decode` 生成 `Uop`
（类型/opcode/funct3/funct7/rd/rs1/rs2/imm/pc/halt/allocDest/predictedPC/
ckptId），压入 16 项 Uop 队列 IQ。FQ 头是否可被消费由 FQ 自己按 DecodeUnit
快照的空槽决定。发射侧（后端 IssueArbiter）从 IQ 头取指，见
[`backend.md`](backend.md) §2。

---

## 4. 分支预测（BPU）

方向预测采用 **TAGE 族混合预测器**（局部二级基表 + 4 张全局历史标签表），目标
预测按控制流类型拆分。预测在**取指当拍**完成，结果随 `FetchDecision` 携带
（含供解析期消费的 `TAGESCMeta`）。

### 4.1 方向预测：TAGE

| 部件 | 配置 | 说明 |
|------|------|------|
| 基表 T0 | 1024 × 2-bit | 索引 = `PC ⊕ LHT[PC]`；LHT 为 512 条目 × 12-bit **每 PC 局部历史**（非推测更新），兜底纯全局历史看不见的单 PC 模式 |
| condSeen 过滤器 | 512 × 1-bit | 条件分支解析时置位；取指侧 `btbHit ∨ condSeen` 才移位 GHR——避免"从不 taken 的分支不留历史、BTB 驻留漂移改变历史成员"两类缺口 |
| 标签表 T1–T4 | 每表 1024 条目 × 8-bit tag | 历史长度 {6, 12, 24, 48}；索引与 tag = 两种不同宽度的 Seznec 折叠视图 ⊕ `pc` |
| TageEntry | `{valid, tag(8b), ctr(3b), u(2b)}` | provider = 最长命中的历史表；alt = 次长命中（无次命中回退 T0） |
| useAltOnNa | 128 条目 × 4-bit（初值偏 alt） | 弱 provider（`ctr==3/4`）时学习"此 PC 改用 alt 是否更准" |
| 分配/老化 | 8-bit Galois LFSR（taps `0xB8`）抽签 | 仅在 `u==0` 行上分配；无空位则衰减候选行 `u`；每 64 次更新 bankTick 全表 `u >>= 1`（减半不清零，强表项可活过两轮） |

预测选取：provider 命中则以其 `ctr≥4` 为方向，弱计数时按 useAltOnNa 决定是否
改信 alt；无 provider 命中回退 T0。统计校正器（SC）曾试装后移除（简化版全线
退化），此处保留 TAGE-only 形态便于未来按 Seznec 论文补回。

### 4.2 目标预测（跳去哪）

| 部件 | 配置 | 说明 |
|------|------|------|
| BTB | 256 条目 | 携带 `unconditional/isCall/isRet/isIndirect` 类型；命中且无条件 ⇒ 必 taken |
| Target Cache | 128 条目 | JALR 专用：256×8b 提交级局部历史 BHR，按 `pc ⊕ BHR` 哈希——区分同一静态间接跳转在不同动态上下文的目标 |
| RAS | 8 条目 `{retPC, times}` | 同返回地址递归共用一条目（`times` 计数去重） |
| SARAS | 16 条目 `{addr, index, times}` | 纠错队列：对每次投机 call-dedup / ret 记录原值，flush 可精确撤销 |

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
  由 IMEM（50 周期主存延迟）承担。

---

## 6. 关键规格

| 项 | 规格 |
|----|------|
| 取指带宽 | 每周期至多 1 条（FQ 有空位且无背压/无 squash/未闩锁 halt 时） |
| FQ / IQ | 8 / 16 |
| 方向预测 | T0 1024×2b · LHT 512×12b · T1–T4 各 1024×8b tag（hist {6,12,24,48}）· useAltOnNa 128×4b |
| 目标预测 | BTB 256 · Target Cache 128（BHR 256×8b）· RAS 8 · SARAS 16 |
| checkpoint | ckptId 池 64（≥ ROB 64，static_assert 守护） |
| 预译码 | FQ 尾 jal/jalr 静态分类（call/ret/indirect + 静态 jal 目标） |
| halt | ICache 头 = `0x0ff00513` ⇒ latch haltFetched 停取 |

## 相关文档

- [`../README.md`](../README.md) — 总览 / 数据通路图 / 周期模型
- [`cache.md`](cache.md) — L1I（ICache/IMEM）存储行为与主存延迟
- [`backend.md`](backend.md) — 发射、执行、写回、提交与 squash 恢复
