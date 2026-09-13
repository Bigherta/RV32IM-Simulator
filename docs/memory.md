# 访存子系统：加载/存储队列 · 转发 · 违例检测 · 请求准入

> 负责处理器侧的访存顺序语义：Load/Store 发射即入队，store 数据/地址以组合
> 事件广播给 load（store→load 转发），内存顺序违规由 MDP 检测并整窗恢复；
> 每周期经仲裁准入**一个**访存请求交给缓存/主存。
> 相关实现：`LQ`、`SQ`、`MemArbiter`（StaticArbiter）、store 广播组合逻辑。
> [← 返回 README](../README.md)

缓存与主存的存储行为（命中/回填/写回/延迟）见 [缓存与存储层次](cache.md)；
本文件只描述 LQ/SQ 与仲裁逻辑。

---

## 1. 边界与职责

```
         AGU（后端执行）                    ROB（后端提交）
        load/store 地址就绪                store 已提交
              │                                 │
   StoreValue RS 数据就绪 ──(组合广播)──► SQ（16）◄── push（发射）
              │ storeNotifies / addrNotify      │
              ▼                                 ▼
          LQ（16）◄──────────────────────── SQ 地址/数据事件
              │                                │
              │        MemArbiter（store 优先，每周期 1 请求）
              ▼                                ▼
         DCache ◄────────────────────────（已提交的 store）
              │ loadResp（组合）
              ▼
         LQ head ──lqCDB──► PRF / ROB（完成）
```

| 结构 | 容量 | 职责 |
|------|-----:|------|
| `LQ` | 16 | load 条目：地址/数据两段就绪、store 转发落值、违例报告、完成总线 |
| `SQ` | 16 | store 条目：地址/数据两段就绪；是转发的**事实源**（查快照回答"是否存在更老同址 store"） |
| `MemArbiter` | —（无状态） | 每周期准入 1 个访存请求；**store 优先**、DCache busy 时停发 |
| StoreValue RS | 4 | store 数据源（数据就绪事件的发生地） |

## 2. 两段就绪与广播事件

访存指令在发射时 push 到 LQ/SQ（`robTag` + `n_bytes` + 无符号位等），地址与
数据分别由两类保留站承担，**地址与数据各自独立就绪**：

- **地址就绪**：AGU 执行 `base + offset`。AGU 队首是 store 时，组合广播
  **地址事件** `storeAddrNotify = SQ.planAddressForward(memSlot, address)`；
- **数据就绪**：StoreValue RS 中 `isOperandReady(data)` 的条目，组合广播
  **数据事件** `storeNotifies[i] = SQ.planDataForward(memSlot, value)`。

两个广播都基于 **SQ 快照**求值（顺序无关），`StoreNotify` 携带"存在性结论"：
是否存在同地址更老 store（`knownSameAddressOldestTag`）、是否存在地址未知的
store（`unknownOldestTag`）——load 越过存储墙的判断依据。

## 3. store → load 转发

`LQ.applyStoreForward` 把广播中的地址与数据写入等待中的 load 条目；load 的
值状态机：

```
ValueState: NOTREADY ── 地址就绪 ──► FETCHING ──(store 转发 或 DCache 应答)──► READY
```

- 命中转发后 load 无需访问缓存（数据已在窗口内）；
- **转发窗口边界**：若等待中的 load 与广播 store 之间隔着地址未知的 store，
  转发不能安全进行（数据可能来自错误的 store）——`StoreNotify` 的存在性字段
  使 LQ 能做精确判断，不触发就继续走缓存路径。

## 4. 记忆违例检测（MDP）

地址**更老**的 store 解析后，若发现**更年轻**的 load 已越过它执行（load 已
读旧值），则构成违例：

- 违例 load 的窗口从该 store 起整窗 squash（`SquashKind::LoadViolation`），
  上报进入后端的 `FlushArbiter` 排队（最老优先，见
  [`backend.md`](backend.md) §6.2）；
- squash 恢复后 LQ/SQ 按 ROB 尾快照截断，load 重执行。

`SQ::canDispatchLoad` / `replyToLoadRequest` / `hasOlderUnresolvedAddressStore`
为 LQ 提供查询原语：决定一个 load 能否在当前存储墙下安全访问缓存。

## 5. 请求准入（MemArbiter → DCache）

`MemArbiter::arbitrate(LQ, SQ, ROB, DCache, squash)` 每周期给出至多 1 个
`MemDispatchDecision`：

- **store 优先互斥**：已提交的 store（写缓存，属于程序序提交点）优先于乱序
  load；
- **busy 门控**：DCache `isBusy()`（缺失在途）时**不准入**——DCache 注释保证
  "`!isBusy()` 时必须无条件接受 decision"，因为该拍 store 已从 SQ 弹出；
- 请求携带完整身份：`{op, value/address, isSigned, n_bytes, robTag, memIndex}`
  （store 的 memIndex 带 `MEM_STORE_BIT` 高位标记；`memSlot` 为低 6 位，LQ/SQ/
  DCache 共槽位域）。

DCache 按两段 FSM 接受请求并回 `loadResp`（组合、过 squash 门），命中 1 拍
自答；缺失回填与脏逐出等存储行为见 [cache.md](cache.md)。

## 6. 完成总线

LQ 头（按序的 load 完成口）就绪后，`lqCDB.build(LQ, squash)` 每周期最多给出
一个完成载荷 `{value, robTag, memIndex}`，经 lqCDB 广播到 PRF（完成写口）与
ROB（置提交就绪）——load 结果与 ALU/MUL/DIV 结果在同一周期内四总线并行。

## 7. 关键规格

| 项 | 规格 |
|----|------|
| LQ / SQ | 16 / 16 |
| 地址/数据保留站 | StoreAddr 4（配合 AGU）+ StoreValue 4 |
| 请求带宽 | 每周期 1 个访存请求（store 优先、DCache busy 停发） |
| 转发 | 数据事件（StoreValue RS 就绪）+ 地址事件（AGU 队首 store），基于 SQ 快照组合求值 |
| 违例 | MDP：老 store 地址解析 × 年轻已执行 load ⇒ 整窗 squash（LoadViolation） |
| 完成 | LQ head → `lqCDB`（含 memIndex 通路） |
| 命中路径 | DCache 命中 load 1 拍自答 `loadResp`（见 [cache.md](cache.md)） |

## 相关文档

- [`../README.md`](../README.md) — 数据通路图 / 周期模型
- [`backend.md`](backend.md) — 发射 push、提交 store、FlushArbiter 恢复
- [`cache.md`](cache.md) — DCache/DMEM 的命中、回填与写回
