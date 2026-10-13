# 访存子系统：加载/存储队列 · 转发 · 保守准入

> **当前双树访存（2026-10-13）**：D$请求队列接收与队首SRAM查询分开；Store在实际
> 字节写入且metadata dirty置位后完成。缺失读/脏行写独立发送一次，DMEM固定20拍，
> 返回行保持至回填消费；写回地址由旧tag与组号重建。恢复取消的已接受Load在D$保存
> cancelled位，迟到结果不因ROB/LQ身份复用而写进新请求，已提交Store保留。
> HALT提交立即停机，不排空；SQ/D$/外存可仍有未完成Store，不能把最终外存或D$覆盖
> 外存直接视为全体已退休Store均已落地。两软件已同步，RTL未同步；下面结束协议为历史。

> **CPU顶层结束协议（SPEC §66）**：SQ接受排空/旧访存结束/全dirty实际写回的
> 资格由CPU拍初状态判断，拍末更新finishPhase；不封装控制模块。活动周期保持。

> **第12/13收口与完整镜像（SPEC §63/64）**：LSQ模块严格及Store扩展22组重新
> 通过；15真实CPU覆盖满LQ/SQ、SV实际交付、地址/数据先后与跨Cache字节/恢复，
> 四源码负向有效。24镜像三端无损逐拍/退休/最终128KiB全等，生产周期表保持；
> 课程AXI/其他异常范围继续，入口sync/README §55/56。

> **HALT外存完成（SPEC §62）**：HALT提交冻结core但SQ已提交项继续发送；SQ空、
> Cache/DMEM旧事务结束后全dirty写回，实际写完才DONE。16程序9629/2872及最终
> 128KiB独立字节核验/7负向通过；core保持，total增加drain。SQ新增完整head seq
> 只读查询便于epoch观察，访存4803/764回归保持。入口sync/README §54。

> **全流水恢复（SPEC §61）**：67真实CPU程序9001/2872三端全等，11源码负向；
> LSQ与所有FU/前端同链核验，已提交SQ保留、取消Load迟到返回/复用通过。RTL
> 分配旁路补P0非生产者资格，软件算法保持；后端4803/764严格回归通过，README §53。

> **最新关联门禁（SPEC §60）**：BTB真实26程序6690/1848与参数边界通过；访存
> 后端4803/764＋六实际源码负向、RTL16项重新通过。第17当前BPU域完成，下一
> 第18多恢复请求/所有FU及LSQ全流水恢复，入口sync/README §52。

> **checkpoint身份闭环（SPEC §59）**：完整CPU取指使用实际IMEM和SRAM I$，33程序
> 13041/1848严格同值，CID单次取指单次分配、111次回绕/取消后重用及RAS内容恢复
> 通过。真实producer事件与活ROB关联，模板窗口口径已统一；既有4803/764访存
> 后端/RTL16项保持。用户新RAS策略六IPC691640，ISA11967396，README §51。

> **指令行边界（SPEC §58）**：实际IMEM16槽、20拍倒计时、顺序返回与逻辑squash
> 清队列已用于真实SRAM RTL I$三端验收2324/160及完整前端2259/216。每个旧miss
> 登记行请求，接受回填才消费行；恢复清逻辑身份，物理运输旧响应由总线适配器
> 消费/丢弃。软件功能/golden保持，后端4803/764严格回归通过，入口README §50。

> **Store完成位图（SPEC §57/67）**：SQ把所有拍初活动、地址/数据齐且未提交项按ROB槽
> 组合译码ready/epoch位图，CPU comb采样；ROB核对旧有效Store/epoch及恢复年龄后批量置ready。已提交
> 缓冲Store不参与，不用本拍新地址/数据旁路，退休最早下一拍。764字段严格对拍入口
> 见sync/README §49；正常SQ退休/发送仍独立，末尾全dirty排空按结束协议验收。

> **当前返回接口（SPEC §55）**：寄存器RTL D$正常请求/实际DMEM6378/14三端全等。
> RTL LSU现在先消费旧响应，再捕获同拍下一读，与软件连续Cache请求行为一致；旧
> tag/宽度/符号解释和迟到丢弃保持，旧气泡实际负向有效。软件行为未改，完整三端
> CPU＋Cache至退休联合轨迹另验，入口sync/README §47。

> **最新Cache准入修复（SPEC §52）**：两软件接口统一为
> `SQ::canSendLoadToCache(addr, nBytes, loadTag)`，传实际Load字节数，转发失败后阻塞
> 地址未知或任意字节重叠的更老未提交Store。同word但不重叠可放行，完整/部分覆盖
> 两个方向都检查。DIVU延迟数据后的SW→偏移LHU/LBU、SH→偏移LBU、SB→覆盖它的LW
> 四例及排空对照通过，实际旧地址相等条件重编负向有效。两软件Release/_DEBUG各
> 19个完整CPU镜像及18ISA＋6IPC通过，golden保持；入口sync/README §44。

> **转发尚未共同等价（SPEC §46）**：1338步323字段真实来源链两软件全等，但word
> 转发拍/广播带宽/未知地址覆盖及字节范围与RTL不同。SQ.replyToLoadRequest和LQ
> notify直接写完整Store源值，SB后LBU/LB/LW忽略字节覆盖/扩展/合并，在正常DIVU
> 七指令完整软件CPU提交错值；相同RTL core正确，80 NOP排空控制正确。先修该功能，
> 再定时序；本轮生产/golden保持，差异文档sync/STORE-FORWARD-DIFFERENCES.md。

> **当前共同SQ合同（SPEC §45）**：同一SQ缓冲已退休Store至下游实际接受，ROB退休
> 不等ready；SQ独立FIFO顺序保证回绕/恢复保留及最新Store选择，允许同拍退休/发送。
> LQ1432步816字段、SQ872步340字段四方一致，整体严格模式通过；RTL14/14包含真实
> Store来源/四word Load/回绕/分支恢复/HALT，完整三端来源和Cache/字节交互另验。
> 18＋6严格软件活动clock11,967,396/690,814保持；下面§42～44为历史阶段记录。

> 当前LQ统一ROB实际提交同拍回收（SPEC §44），CDB只标记已广播；旧态willCommit/
> 队首tag直接pop，更老提交与恢复并发也回收。LQ1432步816字段四方一致，满时下一拍
> 分配；SQ提交/发送耦合仍待定。新ISA/六IPC clock11,967,396/690,814。

> **第12步第一项修复（SPEC §43）**：主树pushLoad在响应/CDB后初始化新槽，旧响应写入
> 不能覆盖本拍新分配。LQ1365步816字段正确数据参考三端通过，临时旧顺序仍复现错值。
> 本拍仍只读this/Input，flush最后；LQ释放/Store提交合同差异（§42）待定版。

> 第10步DIV专用结果通道实际消费可同拍接收新工作，strict-younger取消/边界保持已统一；
> 真实返回链5000步500字段一致。18 ISA新clock总和11,967,406；Load/访存生命周期依第12～15步验收。

> 第9步MUL_CAP=1，三级弹性准入读取实际mulCDB/flush；真实乘法返回链2302步508字段
> 四方一致，n+3写PRF/ROB、n+4依赖派发。Load返回及访存生命周期依第12～15步验证（SPEC §38）。

> 第8步actualTaken运输/训练修正及对齐控制后端全链已验收；访存接口边界保持，AGU链
> 1195步回归及两软件18+6严格周期通过，总clock12,246,204。misAlign异常按用户要求排除。

> 第7步AGU已统一单槽同拍交付重填：n拍计算/保存，n+1交付/拍末写地址ready，n+2可见。
> LQ/SQ槽在issue预留，满只停新分配，不阻止AGU写已有条目；取消旧结果后可重填更老项。
> 3182步模块及1195步/2390点200字段真实地址整链四方一致；LQ/SQ.matchesAddress匹配
> 活动槽和完整tag，SQ.planAddressForward(memSlot,address,producerTag)同样过滤错身份广播。
> WB/StoreValue为上游输入；提交释放及Cache返回按第12～15步验收，详见SPEC §33。

> 第6步普通整数结果路径与真实PRF/ROB已闭环验收，ALU CDB由实际结果产生；
> load/store返回与跨Cache/LSQ身份使用仍属于访存专门范围，软件18+6严格周期保持。

> 第5步flush接收统一：RTL AGU/StoreValue可交付严格更老项，LSU在恢复周期接收
> 存活更老地址/数据，边界/年轻地址拒绝；普通分配与地址同槽更新优先级经定向测试保持。

> 第4步PRF分配统一为FIFO，返回值的物理目标身份仍由存活ROB完整tag认证；四路
> 写回与普通重命名/恢复域三端对拍通过，LSQ专门的生命周期验收按访存步骤进行。

> 第3步3B完成前端满时替换，LSQ满时准入、释放与转发规则保持；两树全量周期一致。

> **2026-10-05**：LQ/SQ 各 8 项全部可用，4-bit epoch 指针；恢复尾快照保存完整序号，
> memIndex/getHead 仍是物理槽，getTail 返回序号。D$8 KB/64 B。主树所有 tick 只写 CPUstate，
> DCache 行查询、DMEM 读完成载荷在 comb 采样；写完成从旧writeExecute读地址/数据，tick直接写内存。

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

> **当前行粒度（2026-10-01）**：两树 ICache/IMEM 使用 64 B 的 `LineReturn`，
> DCache/DMEM 使用独立的 64 B 字节载荷；D$ 为 16 KB 直接映射，两侧主存延迟均为 20 周期，返回总线与接受事件各自独立。
> ICache 的 `fillFire/readBlocked` 只参与取指准入，不门控本节的 LQ/SQ、`MemArbiter`
> 或 DCache 完成路径。活动性能结果见 [benchmarks.md](benchmarks.md)。
> ICache 的队首 n+1 读输出旁路只缩短取指交付路径，不改变本节的 load/store 准入和数据侧延迟。

---

## 1. 边界与职责

```
         AGU（后端执行）                    ROB（后端提交）
        load/store 地址就绪                store 已提交
              │                                 │
    StoreValue RS 数据就绪 ──(组合广播)──► SQ（8槽/8可用）◄── push（发射）
              │ storeNotifies / addrNotify      │
              ▼                                 ▼
       LQ（8槽/8可用）◄─────────────────── SQ 地址/数据事件
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
| `LQ` | 8 个物理槽 / 8 个可用项 | load 条目：地址/值独立就绪、store 转发落值、完成总线 |
| `SQ` | 8 个物理槽 / 8 个可用项 | store 条目：地址/数据两段就绪 + 显式 `committed`；查询拍初快照 |
| `MemArbiter` | —（无状态） | 每周期准入 1 个访存请求；store优先、DCache请求队列旧满时停发 |
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
- AGU 首次解析 load 地址时，SQ 按同word候选查询，最年轻候选须数据就绪且完整覆盖
  Load，并比最后一个未知地址来源年轻才立即转发；返回值按字节偏移移动并截取/扩展；
- store 离开 ROB 后可能因 DCache 请求队列满继续留在 SQ，因此提交状态由 ROB commit
  显式置位，不再用 retained `robTag` 与当前 ROB head 推断。转发的新旧关系按 SQ
  从 head 到 tail 的队列位置确定；只有未提交、仍在 ROB 活跃窗内的 store 才做
  RobTag 年龄比较；
- **未知地址或字节重叠的更老 store 会阻塞 cache 请求**。
  `SQ::canSendLoadToCache(addr, nBytes, loadTag)` 检查更老未提交Store的完整字节范围，
  任意重叠都阻塞尚未转发的Load，不能仅检查同起点或Store完整覆盖Load。
  同word内不重叠的已知Store不阻塞。已提交SQ队首仍优先发送、按Cache请求队列旧满门控。

## 4. 保守准入与恢复边界

保守准入使内存顺序违例在正常路径上不可发生：一个 load 进入 cache 时，所有更老
未提交 store 的地址都已经解析，且没有字节范围重叠的 store。更老 store 后续不会再改变
地址，因此不会出现已执行 load 被更老 store 追溯覆盖的情况。

`SQ::replyToLoadRequest` 与 `SQ::canSendLoadToCache` 共同完成准入安全性：地址解析当拍
优先尝试 store→load 转发；没有可转发值时，`canSendLoadToCache` 再决定是否允许 cache
请求。`LQ` 的 include-self 尾快照仍保留给统一的 ROB/分支 squash 边界，不代表 load
仍会触发 squash。

## 5. 请求准入（MemArbiter → DCache）

`MemArbiter::arbitrate(LQ, SQ, ROB, DCache, squash)` 每周期给出至多 1 个
`MemDispatchDecision`：

- **store 优先互斥**：`SQ.head.committed`，或正位于 ROB 头且满足统一
  `storeWillCommit` 谓词的 SQ 头 store，优先于乱序 load；后者允许在同一周期完成
  ROB 提交并进入 DCache。squash 周期仅允许严格早于 `SquashTag` 的 ready head 提交；
- **旧满门控**：DCache `isRequestFull()` 时不准入，即使本拍出队也不替换满槽；
  缺失在途且队列有空间仍可入队，Store从SQ弹出与DCache接受为同一事件；
- 请求携带完整身份：`{op, value/address, isSigned, n_bytes, robTag, memIndex}`
  （store的memIndex带`MEM_STORE_BIT`标记，memSlot指LQ/SQ槽，不是DCache请求队列索引）。

DCache入队与队首同步查询分离，查询下一拍命中服务；`loadResp`为组合输出，
同时检查跨拍cancelled与当拍squash。缺失回填/脏写见[cache.md](cache.md)。

## 6. 完成总线

LQ 从 head 起按程序序扫描，选择**最老的 `READY` 且尚未广播**的 load；它不要求
物理 LQ head 本身已经就绪，因此语义是 oldest-ready，而不是 FIFO head-only。
lqCDB 每周期最多广播一个 `{value, robTag, memIndex}` 到 PRF 与 ROB；它可与
ALU/MUL/DIV 三路结果同周期并行。

CDB消费只标记已广播，不释放LQ。当前释放使用拍初ROB.willCommit及队首tag匹配，
在实际Load提交同拍pop；严格更老提交与恢复并发亦释放。满时按旧full停收，下一拍
复用空位，三端实测15写回/16提交释放/17新分配（SPEC §44）。

## 7. 关键规格

| 项 | 规格 |
|----|------|
| LQ / SQ | 各 8 个物理/有效槽；4-bit epoch 序号区分满空，数组仅用 slot |
| 地址/数据保留站 | StoreAddr 2（配合 AGU）+ StoreValue 2 |
| 请求带宽 | 每周期1个访存请求（store优先、DCache请求队列旧满停发） |
| 转发 | 数据事件（StoreValue RS 就绪）+ 地址事件（AGU 最老有效 store 结果），基于 SQ 快照组合求值 |
| 访存消歧 | 已知同址老 store 阻塞；地址未知老 store 也阻塞，load 不越过未解析 store |
| 违例恢复 | 不再设置 MDP load-violation squash 通路；顺序由保守准入和 store→load 转发保证 |
| 完成 | LQ oldest-ready、未广播项 → `lqCDB`（含 memIndex 通路） |
| 命中路径 | 入队后队首查询SRAM，查询下一拍命中Load组合返回raw给LQ |

## 相关文档

- [`../README.md`](../README.md) — 数据通路图 / 周期模型
- [`backend.md`](backend.md) — 发射 push、提交 store、FlushArbiter 恢复
- [`cache.md`](cache.md) — DCache/DMEM 的命中、回填与写回
