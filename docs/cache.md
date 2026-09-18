# 缓存与存储层次：L1I / L1D / 片上主存

> 存储层次为两层 + 片上主存：L1I（ICache）、L1D（DCache），下一级是各 128 KB
> 的指令/数据主存（IMEM / DMEM）。**主存端口延迟固定 20 周期**；ICache 命中
> 当拍组包，DCache 命中 load 在接受请求后的下一拍应答，store 命中当拍改行。
> 两个缓存的缺失回填与脏逐出写回统一走 20 周期端口。访存队列如何驱动它们见
> [访存子系统](memory.md)。
> [← 返回 README](../README.md)

> 实现词汇：主树用周期初快照与 `tick()`，模板树用 `Wire`、`Register` 与
> `work()/sync()`；下文描述持久状态、组合命中和周期边界，不依赖其中任一写法。

---

## 1. 层次与延迟模型

| 层次 | 容量/组织 | 命中 | 缺失 |
|------|-----------|------|------|
| **L1I** · ICache | 8 KB，512 行 × 16 B，**直接映射** | 当拍组包入 FQ（零命中延迟） | IMEM 整行回填，**20 周期** |
| **L1D** · DCache | 64 KB，1024 组 × 4 路 × 16 B，**tree-PLRU** | load **1 拍自答**；store 当场落行 | 主存回填 **20 周期**；脏 victim 并行写回 **20 周期** |
| **IMEM / DMEM** | 各 128 KB 字节寻址 | — | — |

主存延迟由两树共同的 `MEM_LATENCY = 20` 定义：取指请求、DCache 回填读和
脏 victim 写回都以该值装载各自的倒计时。性能与命中率基线见仓库根目录
`docs/benchmarks.md`。

## 2. 指令侧：ICache + IMEM

### 2.1 ICache（L1I）

- 8 KB 直接映射（512 行 × 16 B），行内 4 个 word；缺失以**行对齐地址**
  （`pc & ~0xF`）请求主存。
- 命中路径是组合谓词：`fetchDecision.valid ∧ ICache.hit(pc)` 成立则命中指令
  当拍组包入 FQ，且**门控** IMEM 请求（命中不发行请求）；
- 行返回后 ICache 持行；`popConsume`（头可被 FQ 消费 = 行返回就绪 ∧ ¬haltFetched
  ∧ ¬FQ 满）时消费、由 ICache 自己清状态（写自有纪律）；
- 停机字识别：ICache 头返回字 == `0x0ff00513` 时置 `haltSignal`（前端闩锁停取，
  见 [frontend.md](frontend.md) §2.2）；
- 统计：`VERBOSE=icache` 输出 hits/misses/hit-rate。

### 2.2 IMEM（指令主存）

- 行级突发服务：请求队列 + `remain_cycle=20` 递减；返回经 **`LineReturn` 四字
  总线**（`{lineAddr, data[0..3]}`，word 索引 = `(pc>>2)&3`）交给 ICache；
- 与 DCache 回填共享同一主存延迟口径（20 周期）。

## 3. 数据侧：DCache（L1D）

### 3.1 组织与替换

- 64 KB = 1024 组 × 4 路 × 16 B；行 `{valid, dirty, tag, datas[16]}`；
- **tree-PLRU**：每组 3-bit（root + 两叶），命中/填行时翻转到被访问侧；
- 几何由编译期常量 `NUM_OF_SETS` / `DCACHE_INDEX_BITS` 定义；若为容量实验调整，
  二者必须保持 `NUM_OF_SETS == 2^DCACHE_INDEX_BITS`，并由 `static_assert`
  守护行大小和索引/tag 切分。

### 3.2 两段式状态机（`Phase::READY / WAIT`）

DCache 是 DMEM 的唯一客户（`MemArbiter` 准入的请求只发给 DCache）。处理器访问
在 `READY` 拍组合求值（`PrRd` / `PrWr`）：

```
READY ── 命中 ───────────────────────────────► load: 1 拍自答 loadResp / store: 当场写行置脏
  │
  └── 缺失 ── latch 身份( park ) + busy=1 ──► WAIT
       （向 DMEM 双通道发：回填读[+ victim 脏时写回写]，各 20 周期）
WAIT ── DMEM 回复就绪( ∧ 写口不忙) ──► 填行 → 服务 park → busy=0 → READY
```

细节：

- **命中自答**：`READY` 拍命中 load 直接填 `loadBuffer`（下拍组合 `loadResp`
  对 LQ 可见）；store 命中当场写行并置脏，不产生主存流量；
- **缺失**：`PrRd/PrWr` 在 `cacheRequestBuffer` 里 **park 请求**（只含
  地址/宽度/符号/方向），并 latch 本次准入总线上携带的身份
  （`robTag/memIndex` + 分配的 `targetWay`）——回填完成后据此组装真正的应答；
  同时置 `busy` 进入 `WAIT`。**busy 期间 `MemArbiter` 不再准入**（见
  [memory.md](memory.md) §5）；
- **双通道脉冲**：向 DMEM 发 `DMEMRequest{readValid, writeValid}`。victim 行脏
  时读、写**并行**发出（读回填 + 写回脏行），各 20 周期；**写回地址由 victim
  行自身的 tag 重建**（而不是请求地址的 tag——否则脏数据会落到错误帧，回填再
  读回陈旧值）；
- **WAIT 完成**：`DMEM.isReplyReady() ∧ ¬isWriteBusy()` 时，把 16 B 行写入
  `targetWay`（清 dirty、写 tag、置 valid、更新 PLRU），然后服务 park：load →
  提取字节 + 符号扩展填入 `loadBuffer`；store → 逐字节落行并置脏。最后清
  park/request/busy 回 `READY`；
- **行阵是持久状态**：命中、填行、写回和 PLRU 更新都作用于模块拥有的唯一行阵，
  不把整阵复制成每拍组合输入。主树只快照控制状态并直接更新活体行阵；模板树以
  `Register`/持久数组表达相同边界。这是实现词汇差异，不改变缓存可见语义。

## 4. DMEM（双口主存）

- **读/写双口各自独立**：`readBusy`/`writeBusy`、`readExecute`/`writeExecute`
  分开计数递减，因此回填读与脏逐出写回可**并行在飞**（各 20 周期）；
- 只接受 DCache 转发的**行级请求**（读行/写行），不再接受任何处理器侧的
  Load/Store 原语；
- 完成读时从持久 DMEM 存储阵列取 16 B 填回复缓冲；因此回填能看到此前脏逐出
  已写回的新值。写端口经 `writeLine` 更新同一阵列；
- `MemPull` 消费已就绪的回复（`readBufferValid` 清零）。

## 5. 关键不变量与画像

- **脏逐出不变量**：同组不同 tag 的 store 强制逐出时，写回地址必须由 victim
  的 tag 与组号重建；后续回填必须读到写回后的数据；
- **命中率画像**：18 基准的 I$ / D$ 命中率与逐分型预测准确率在
  仓库根目录 `docs/benchmarks.md`（D$ 覆盖 load+store 全部访存）；
- **debug 校验**：`-D_DEBUG` 下 clean 行命中会把缓存值与活体 DMEM 读数比对
  （dirty 行更新值不参与）。

## 6. 关键规格

| 项 | 规格 |
|----|------|
| ICache | 8 KB · 512 行 × 16 B · 直接映射 · 命中当拍组包 |
| DCache | 64 KB · 1024 组 × 4 路 × 16 B · tree-PLRU · 写回 + 写分配 |
| 命中延迟 | L1I 0（当拍组包）；L1D load 1 拍自答 / store 当场落行 |
| 主存延迟 | 固定 **20 周期**（IMEM 回填、DCache 回填、脏逐出写回共用） |
| DMEM | 读/写双口独立在飞（各 20 周期）；仅行级请求 |
| 存储阵列 | IMEM/DMEM 各 128 KB；缓存/主存阵列均为模块持久状态 |

## 相关文档

- [`../README.md`](../README.md) — 数据通路图 / 周期模型 / 性能口径
- [`frontend.md`](frontend.md) — 取指如何消费 L1I（命中组包 / 缺失回填）
- [`memory.md`](memory.md) — LQ/SQ 与 MemArbiter 如何驱动 DCache
- 仓库根目录 `docs/benchmarks.md` — 主存延迟口径与命中率实测
