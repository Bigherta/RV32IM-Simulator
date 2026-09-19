# 访存子系统：加载/存储队列 · 转发 · 违例检测 · 请求准入

> 负责处理器侧的访存顺序语义：Load/Store 发射即入队，store 数据/地址以组合
> 事件广播给 load（store→load 转发），内存顺序违规由 MDP 检测并整窗恢复；
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
| `LQ` | 8 个物理槽 / 7 个可用项 | load 条目：地址/值独立就绪、store 转发落值、违例报告、完成总线 |
| `SQ` | 8 个物理槽 / 7 个可用项 | store 条目：地址/数据两段就绪；是转发的**事实源**（查询周期初视图回答“是否存在更老同址 store”） |
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
  `READY`；后到的 DCache 应答只接受仍为 `FETCHING` 且 `robTag/memIndex` 匹配的项，
  因而不会覆盖更新鲜的转发值；
- AGU 首次解析 load 地址时，SQ 只在“最年轻的更老同址 store 数据已就绪，且它与
  load 之间没有地址未知 store”时立即转发；
- **地址未知的更老 store 不会阻塞 cache 请求**。`SQ::canDispatchLoad` 只阻塞已经
  确认同址的更老 store，因此 load 可以投机越过未解析 store；若后者随后解析为
  同址，由 §4 的违例恢复纠正。这与“遇到未知地址就保守停发”的实现不同。

## 4. 记忆违例检测（MDP）

更老 store 的地址解析后，FlushArbiter 从 LQ 头向后扫描；第一个地址相同、年龄
更年轻且值状态已为 `FETCHING` 或 `READY` 的 load 构成违例。`FETCHING` 也算，
因为该 cache 请求已经在途且没有取消通路。

- **squash 边界是违例 load 自己，而不是触发检测的 store**：`SquashTag`、`SquashPC`
  与 `CkptId` 都取该 load，重定向到 load 自己的 PC；store 与更老状态保留；
- ROB 的边界语义保留该 load 条目，load 在发射时保存的 LQ 尾快照又是
  **include-self** 边界，因此恢复不会把自己的 LQ 项越过；更年轻的 ROB/LQ/SQ
  状态按快照截断。请求进入 `FlushArbiter` 后仍与分支请求统一按年龄仲裁
  （见 [`backend.md`](backend.md) §6.2）。

`SQ::replyToLoadRequest` 负责地址解析当拍的安全转发，`SQ::canDispatchLoad` 负责
cache 准入时阻塞已知同址 store；未解析 store 则由上述投机 + 违例检测覆盖。

## 5. 请求准入（MemArbiter → DCache）

`MemArbiter::arbitrate(LQ, SQ, ROB, DCache, squash)` 每周期给出至多 1 个
`MemDispatchDecision`：

- **store 优先互斥**：已经提交、或正位于 ROB 头且提交就绪的 SQ 头 store 优先于
  乱序 load；后者允许在同一周期完成 ROB 提交并进入 DCache；
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
| 投机消歧 | 已知同址老 store 阻塞；地址未知老 store 可越过，解析后由违例检测纠正 |
| 违例 | 老 store 地址解析 × 年轻同址 `FETCHING/READY` load；边界和重定向目标均为该 load |
| 完成 | LQ oldest-ready、未广播项 → `lqCDB`（含 memIndex 通路） |
| 命中路径 | DCache 命中 load 1 拍自答 `loadResp`（见 [cache.md](cache.md)） |

## 相关文档

- [`../README.md`](../README.md) — 数据通路图 / 周期模型
- [`backend.md`](backend.md) — 发射 push、提交 store、FlushArbiter 恢复
- [`cache.md`](cache.md) — DCache/DMEM 的命中、回填与写回
