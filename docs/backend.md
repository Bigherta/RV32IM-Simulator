# 后端子系统：发射 · 乱序执行 · 写回 · 提交 · squash 恢复

> 负责乱序核心本体：从 IQ 发射（rename）→ 保留站就绪乱序执行 → 结果总线写回 →
> ROB 按序提交；误预测与记忆违例的排队、整窗恢复也在这里仲裁并触发。
> 相关实现：`StaticArbiter`（IssueArbiter/DispatchArbiter）、`RS`、`PRF`、`RAT`、
> `ROB`、`ALU`、`MUL`、`AGU`、`BRU`、`CDB`、`DynamicArbiter`（FlushArbiter）。
> [← 返回 README](../README.md)

取指与译码在前端完成；访存队列（LQ/SQ）与缓存/主存在
[访存](memory.md) / [缓存](cache.md) 中描述。后端需要掌握"程序序边界"，因此
ROB 条目同时是前端预测 checkpoint 与 LQ/SQ 尾快照的宿主。

---

## 1. 边界与职责

```
 IQ（前端）─► IssueArbiter ─ rename ─► RS 占槽 / ROB push / PRF alloc / RAT 改名
                                      │    （含 LQ/SQ push，见 memory.md）
              ┌───────────────────────▼──────────────────────┐
              │ RS ──DispatchArbiter──► ALU · MUL · AGU · BRU │
              │        （乱序派发，就绪即发）                   │
              └───────────────────────┬──────────────────────┘
                                      ▼
              aluCDB / mulCDB / lqCDB ──► PRF 完成写口 / ROB 置位 / 训练
                                      ▼
              ROB 按序提交 ──► FlushArbiter（误测/MDP 排队、最老优先）
```

| 结构/模块 | 职责 |
|------|------|
| `IssueArbiter`（StaticArbiter） | 组合构建每周期至多 1 个发射包（rename 决策） |
| `DispatchArbiter`（StaticArbiter） | 保留站 → 执行单元的乱序派发（四独立通道） |
| `RS` | 五类保留站：Integer 8 / Multiply 4 / Load 4 / StoreAddr 4 / StoreValue 4 / Branch 4 |
| `PRF` | 物理寄存器堆 128：循环序号自由表、完成写口、checkpoint 恢复 |
| `RAT` | 架构寄存器 → 物理寄存器映射 |
| `ROB` | 重排序缓冲 64：按序提交、checkpoint 快照宿主、squash 边界 |
| `ALU` | 算术/逻辑/移位 + JALR 目标（`isControl` 载荷） |
| `MUL` | M 扩展乘法单元（Booth → CSA → 最终加，3 级流水） |
| `AGU` | 访存地址计算（load/store；队首 store 地址广播给 SQ） |
| `BRU` | 条件分支执行 |
| `CDB` | 三路结果总线载荷的 `build()` 工厂（aluCDB/lqCDB/mulCDB） |
| `FlushArbiter`（DynamicArbiter） | squash 请求队列：检测（BRU 误测/CDB 误测/MDP）、最老优先 |

---

## 2. 发射（Issue / Rename）

`IssueArbiter::build` 在组合阶段从 **IQ 头**解析一条指令，构造 `IssuePacket`：

- **容量门控**：ROB/PRF 自由表/对应 RS 类别/LQ/SQ 同时有空位才发射；
- **rename**：`allocDest` 时 PRF 分配新物理寄存器（`phy`），RAT 建立新映射，
  旧映射记入 ROB 条目（`oldPhy`）供提交释放；
- **操作数解析**：`Operand{tag, imm}`——立即数用 `tag == InvalidPhy` 编码，
  寄存器操作数记录其当前物理标签；
- **哨兵域**：`InvalidPhy = 0` 为全物理域唯一哨兵（P0 永不分配、永不映射）；
  真实物理标签恒在 `1..PRF_CAP-1`；`x0` 恒 0、不参与 rename。

发射包由各模块 tick **各自 apply**（写自有纪律）：RAT 改名 / PRF `pop`+LINK /
ROB push / RS 占槽 / LQ/SQ push（访存指令）/ IQ pop。每周期**至多发射 1 条**
（单口 rename + 单口 ROB push 的硬件约束）。

## 3. 物理寄存器与就绪模型

- PRF 以**循环序号**（headSeq/tailSeq）管理自由表，条目 `{value, ready}`；
- **保留站不缓存值**：就绪判定 `isOperandReady` 与取值 `getOperandValue` 直接
  查询 PRF（或返回立即数）。依赖唤醒的延迟表现为：结果经 CDB 写入 PRF 的下一
  拍，依赖它的保留站自然就绪——**总线数量不再是依赖链长度的上限**；
- 提交时释放 `oldPhy` 回自由表（循环序号语义天然支持 checkpoint `restoreHead`）。

## 4. 派发与执行

### 4.1 派发（Dispatch）

`DispatchArbiter::arbitrate` 每个周期给每个执行单元**独立**选一个就绪候选
（`!isFull()` + 就绪 + 过 squash 门），四通道互不阻塞：ALU / MUL / AGU / BRU
各一个 `DispatchInfo`。访存指令的地址就绪由 AGU 执行，store 数据就绪由
StoreValue RS 提供（见 [访存](memory.md) §2）。

### 4.2 执行单元

| 单元 | 槽位 | 行为 |
|------|-----:|------|
| `ALU` | 4 | 算术/逻辑/移位；**JALR** 目标计算（`isControl` 载荷，经 aluCDB 供 FlushArbiter/BPU 消费） |
| `MUL` | 4 | RV32M 乘法族：radix-4 **Booth** 生成 19 行部分积（0..15 Booth 行 + 16/17 MULHU/MULHSU 无符号修正 + 18 稀疏 +1 补位行）→ **CSA 进位保存压缩** → 最终加法；3 级寄存器流水，经专用 Multiply RS 派发 |
| `AGU` | 4 | load/store 地址 = base+offset；队首为 store 时组合广播地址事件 |
| `BRU` | 4 | 条件分支：`BranchResult{pcFrom, pcResult, robTag}` 出队 |

每单元每周期**取队首**作为唯一写回候选（输出缓冲 + `slotValid`，先进先出），
配合三总线保证"每源每周期至多一个结果"。

## 5. 写回：三路结果总线

结果总线载荷定义在 `CDB.hpp`：`aluCDB` / `lqCDB` / `mulCDB`——ALU、Load(LQ)、
MUL 各驱动一根，**源之间无跨单元仲裁**，每周期至多三个结果并行广播：

- 消费端：PRF 完成写口（置 ready + 写值）、ROB 完成置位（`isCommitReady`）、
  LQ 完成口（`lqCDB` 带 `memIndex`，见 [访存](memory.md)）、BPU 训练
  （CDB 侧只改表）；
- 条件分支/间接跳转的结果载荷挂在 ALU 总线上（`isControl`）——FlushArbiter 在
  该总线上检测 JALR 目标误预测；
- `VERBOSE=cdb` 输出争用统计（both / 仅单侧 / 若单总线谁胜出），用于量化
  三总线 vs 单总线的收益边界。

> 设计注记：同一指令只可能由一个执行源完成，"同周期两总线写同一物理寄存器"
> 在源头上即被排除；保留站查 PRF 就绪的模型使总线数量不构成依赖链瓶颈。

## 6. 提交与 squash 恢复

### 6.1 按序提交

ROB 头就绪即提交（每周期至多 1 条）：`REGISTER` 类型释放 `oldPhy` 回 PRF
自由表；`STORE` 类型在提交点经访存路径写缓存（见 [访存](memory.md)）；
halt 条目（`0x0ff00513`）提交后 `haltCommitted`，停机条件 = halt 已提交 ∧
FQ/IQ/ROB 全空；进程输出停机时 `x10` 低 8 位。

### 6.2 FlushArbiter（squash 排队）

有状态仲裁器，拥有 4 项 squash 请求队列，一个周期内按固定顺序检测：

1. **BRU 分支误测**（BRU 队首结果 vs 预测）；
2. **CDB JALR 误测**（ALU 总线上 `isControl` 载荷 vs 预测）；
3. **记忆违例 MDP**（更老 store 地址解析发现更年轻 load 已越过，见
   [访存](memory.md) §4）——同一周期多源请求时**最老优先**。

`arbitResult()` 产出全局 `squashDetect{SquashTag, SquashPC, CkptId}`，随后每个
模块在自己的 tick 内按该窗口恢复：

| 模块 | 恢复动作 |
|------|----------|
| `RAT` | 从被 squash 的最老 ROB 条目的 checkpoint 快照整表回滚 |
| `PRF` | 按 ROB 条目 checkpoint 的 `headSeq` `restoreHead`，回卷自由表（未提交分配全部作废） |
| `BPU` | 按 `ckptId` 恢复 `BPUSnapshot`（GHR/AlignQueue/RAS_top），折叠视图重算（见 [frontend.md](frontend.md) §4.3） |
| `FQ/IQ/RS/LQ/SQ` | 各按 ROB 条目记录的尾快照回卷（RS 释放槽位、LQ/SQ 按 `getTailSnapshot` 截断） |
| `FetchUnit` | 清 `haltFetched`（若被回卷）并从 `SquashPC` 重启取指 |

误预测惩罚 = squash 排队到前端重启取指之间的固定拍数 + 重执行时间；分支预测
统计（`VERBOSE=branch`）在解析点记录正确/总数，含方向与目标两个维度。

---

## 7. 关键规格

| 项 | 规格 |
|----|------|
| ROB / PRF / RAT | 64 / 128 / 32 |
| 保留站 | Integer 8 · Multiply 4 · Load 4 · StoreAddr 4 · StoreValue 4 · Branch 4 |
| 发射 | 每周期至多 1 条（IQ 头，单口 rename） |
| 执行/写回 | ALU·AGU·BRU·MUL 各 4 槽；每源每周期 1 个队首结果 |
| 结果总线 | 3 根（aluCDB / lqCDB / mulCDB），无跨单元仲裁 |
| MUL | Booth radix-4 + CSA（19 行 → S+C），3 级流水；`mul/mulh/mulhu/mulhsu` |
| FlushArbiter | 4 项请求队列；检测序 = BRU 误测 → CDB JALR 误测 → MDP；最老优先 |
| 停机 | halt 字 `0x0ff00513` 提交后整机清空停机；出口 = `x10 & 0xFF` |

## 相关文档

- [`../README.md`](../README.md) — 数据通路图 / 周期模型（comb/tick）
- [`frontend.md`](frontend.md) — 预测 checkpoint 的语义与恢复（GHR/RAS）
- [`memory.md`](memory.md) — LQ/SQ 尾快照的推进、MDP 违例上报（squash 来源之一）
- [`cache.md`](cache.md) — store 提交落缓存 / load 回填的存储侧行为
