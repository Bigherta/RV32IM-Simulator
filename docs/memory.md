# 访存子系统：加载/存储队列 · 转发 · 保守准入

> 负责处理器侧的访存顺序语义：Load/Store 发射即入队，store 数据/地址以组合
> 事件广播给 load（store→load 转发）；load 只有在更老 store 地址均已知且无
> 已知同址冲突时才准入缓存，不再依赖 MDP 违例恢复；
> 每周期经仲裁准入**一个**访存请求交给缓存/主存。
> 相关实现：`LQ`、`SQ`、`MemArbiter`（StaticArbiter）、store 广播组合逻辑。
> [← 返回 README](../README.md)

> 实现词汇：主树以周期初快照和 `tick()` 更新状态，模板树以 `Wire`、`Register`
> 和 `work()/sync()` 表达同一边界。本文的“快照”若未特别说明，指 ROB 保存的
> 恢复边界或周期初只读视图，而不是某一树专有的数据结构。

缓存与主存的存储行为（命中/回填/写回/延迟）见 [缓存与存储层次](cache.md)；
本文件只描述 LQ/SQ 与仲裁逻辑。

---

## 1. 边界与职责

```
         AGU（后端执行）                    ROB（后端提交）
        load/store 地址就绪                store 已提交
              │                                 │
    StoreValue RS 数据就绪 ──(组合广播)──► SQ（8槽/7可用）◄── push（发射）
              │ storeNotifies / addrNotify      │
              ▼                                 ▼
       LQ（8槽/7可用）◄─────────────────── SQ 地址/数据事件
              │                                │
              │        MemArbiter（store 优先，每周期 1 请求）
              ▼                                ▼
         DCache ◄────────────────────────（已提交的 store）
              │ loadResp（组合）
              ▼
       LQ 最老已就绪项 ──lqCDB──► PRF / ROB（完成）
```

| 结构 | 容量 | 职责 |
|------|-----:|------|
| `LQ` | 8 个物理槽 / 7 个可用项 | load 条目：地址/值独立就绪、store 转发落值、完成总线 |
| `SQ` | 8 个物理槽 / 7 个可用项 | store 条目：地址/数据两段就绪 + 显式 `committed`；是转发的**事实源**（查询周期初视图回答“是否存在更老同址 store”） |
| `MemArbiter` | —（无状态） | 每周期准入 1 个访存请求；**store 优先**、DCache busy 时停发 |
| StoreValue RS | 4 | store 数据源（数据就绪事件的发生地） |

## 2. 两段就绪与广播事件

访存指令在发射时 push 到 LQ/SQ（`robTag` + `n_bytes` + 无符号位等），地址与
数据分别由两类保留站承担，**地址与数据各自独立就绪**：

- **地址就绪**：AGU 执行 `base + offset`。AGU 最老有效结果是 store 时，组合广播
  **地址事件** `storeAddrNotify = SQ.planAddressForward(memSlot, address)`；
- **数据就绪**：StoreValue RS 中 `isOperandReady(data)` 的条目，组合广播
  **数据事件** `storeNotifies[i] = SQ.planDataForward(memSlot, value)`。

两个广播都基于 **SQ 的周期初只读视图**求值（不依赖模块调用顺序）。对于某个
正在广播的 store，`StoreNotify` 还摘要它与目标 load 之间是否已有更年轻的同址
store（`knownSameAddressOldestTag`）或地址未知 store（`unknownOldestTag`），以免
较老 store 的值越过真正应该覆盖它的 store。

## 3. store → load 转发

load 的地址有效位与值状态**相互独立**。发射时值状态为 `NOTREADY`；AGU 写入
地址本身不会改变值状态。值状态机实际为：

```
NOTREADY ── cache 请求获准 ──► FETCHING ── 匹配的 DCache 应答 ──► READY
    └────────────── store 转发 ───────────────────────────────► READY
FETCHING ────────── store 转发 ───────────────────────────────► READY
```

- `LQ.applyStoreForward` 可把 `NOTREADY` 或已发请求的 `FETCHING` load 直接置为
  `READY`；后到的 DCache 应答只在该槽**周期初已是 `FETCHING`**、`robTag/memIndex`
  匹配且本拍有效状态仍为 `FETCHING` 时接受。周期初状态阻止 squash 后复用同一槽位
  与 tag 的新 load 接收旧应答，本拍有效状态则保证更新鲜的 store 转发值优先；
- AGU 首次解析 load 地址时，SQ 只在“最年轻的更老同址 store 数据已就绪，且它与
  load 之间没有地址未知 store”时立即转发；
- store 离开 ROB 后可能因 DCache busy 继续留在 SQ，因此提交状态由 ROB commit
  显式置位，不再用 retained `robTag` 与当前 ROB head 推断。转发的新旧关系按 SQ
  从 head 到 tail 的队列位置确定；只有未提交、仍在 ROB 活跃窗内的 store 才做
  RobTag 年龄比较；
- **地址未知的更老 store 会阻塞 cache 请求**。`SQ::canDispatchLoad` 对更老的未提交
  store 同时检查地址未知和已知同址两种情况；只有当所有更老 store 地址已知且没有
  同址冲突时，load 才能进入 cache。这样 load 不会投机越过未解析 store。

## 4. 保守准入与恢复边界

保守准入使内存顺序违例在正常路径上不可发生：一个 load 进入 cache 时，所有更老
未提交 store 的地址都已经解析，且没有已知同址 store。更老 store 后续不会再改变
地址，因此不会出现已执行 load 被更老 store 追溯覆盖的情况。

`SQ::replyToLoadRequest` 与 `SQ::canDispatchLoad` 共同完成准入安全性：地址解析当拍
优先尝试 store→load 转发；没有可转发值时，`canDispatchLoad` 再决定是否允许 cache
请求。`LQ` 的 include-self 尾快照仍保留给统一的 ROB/分支 squash 边界，不代表 load
仍会触发 squash。

## 5. 请求准入（MemArbiter → DCache）

`MemArbiter::arbitrate(LQ, SQ, ROB, DCache, squash)` 每周期给出至多 1 个
`MemDispatchDecision`：

- **store 优先互斥**：`SQ.head.committed`，或正位于 ROB 头且满足统一
  `storeWillCommit` 谓词的 SQ 头 store，优先于乱序 load；后者允许在同一周期完成
  ROB 提交并进入 DCache。squash 周期仅允许严格早于 `SquashTag` 的 ready head 提交；
- **busy 门控**：DCache `isBusy()`（缺失在途）时**不准入**——DCache 注释保证
  "`!isBusy()` 时必须无条件接受 decision"，因为该拍 store 已从 SQ 弹出；
- 请求携带完整身份：`{op, value/address, isSigned, n_bytes, robTag, memIndex}`
  （store 的 memIndex 带 `MEM_STORE_BIT` 高位标记；`memSlot` 为低 6 位，LQ/SQ/
  DCache 共槽位域）。

DCache 按两段 FSM 接受请求并回 `loadResp`（组合、过 squash 门），命中 1 拍
自答；缺失回填与脏逐出等存储行为见 [cache.md](cache.md)。

## 6. 完成总线

LQ 从 head 起按程序序扫描，选择**最老的 `READY` 且尚未广播**的 load；它不要求
物理 LQ head 本身已经就绪，因此语义是 oldest-ready，而不是 FIFO head-only。
lqCDB 每周期最多广播一个 `{value, robTag, memIndex}` 到 PRF 与 ROB；它可与
ALU/MUL/DIV 三路结果同周期并行。

## 7. 关键规格

| 项 | 规格 |
|----|------|
| LQ / SQ | 各 8 个物理槽；保留一个空槽区分满/空，各最多 7 个有效项 |
| 地址/数据保留站 | StoreAddr 4（配合 AGU）+ StoreValue 4 |
| 请求带宽 | 每周期 1 个访存请求（store 优先、DCache busy 停发） |
| 转发 | 数据事件（StoreValue RS 就绪）+ 地址事件（AGU 最老有效 store 结果），基于 SQ 快照组合求值 |
| 访存消歧 | 已知同址老 store 阻塞；地址未知老 store 也阻塞，load 不越过未解析 store |
| 违例恢复 | 不再设置 MDP load-violation squash 通路；顺序由保守准入和 store→load 转发保证 |
| 完成 | LQ oldest-ready、未广播项 → `lqCDB`（含 memIndex 通路） |
| 命中路径 | DCache 命中 load 1 拍自答 `loadResp`（见 [cache.md](cache.md)） |

## 相关文档

- [`../README.md`](../README.md) — 数据通路图 / 周期模型
- [`backend.md`](backend.md) — 发射 push、提交 store、FlushArbiter 恢复
- [`cache.md`](cache.md) — DCache/DMEM 的命中、回填与写回
