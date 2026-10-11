# 缓存与存储层次：L1I / L1D / 片上主存

> **SQ→ROB接口（SPEC §67）**：完成位图由SQ组合输出，ROB只校验并置ready。
> 已提交SQ发送、DCache准入与dirty写回保持；四构建18＋6结果/周期/统计一致。

> **完整活动整核（SPEC §63/64）**：真实CPU 24镜像共同逻辑Memory下12667040拍
> pre/post全等，最终128KiB独立byte核验。只读观察比较所有实际metadata/有效byte，
> 未变才复用指纹；不按预测事件跳过数据，不改变Cache/外存时序。字节响应按不可变
> 实际接受owner归一化，golden保持；AXI/技术映射另验，入口sync/README §56。

> **全部dirty完成（SPEC §62）**：用户要求最终所有dirty落外存。D$新增扫描/等待
> 实际写完状态，CPU顶层在SQ/旧Cache/外存空后发起，扫描set0..127，写接受不算
> 完成，最后行也等ack；保持valid/data并清dirty。16程序9629/2872与全部128KiB
> 独立字节核验通过，DONE保持16拍无重发，7负向有效；入口sync/README §54。

> **真实恢复联合（SPEC §61）**：实际SRAM I$/IMEM、寄存器D$/DMEM与全CPU67程序
> 9001/2872三端全等。年轻已接受Load逻辑取消、迟到返回排空，已提交SQ保持；
> 42次Cache返回/新读同拍及11源码负向通过。最终dirty drain/计时属第19步，README §53。

> **BPU闭环增补（SPEC §60）**：实际SRAM I$/IMEM提供正常指令镜像，BTB竞争26程序
> 6690/1848三端全等；checkpoint预算仍ROB16/I$4/FQ4/IQ4=28。参数合同与明确非法
> 拒绝通过，访存4803/764重新通过。第18全流水恢复继续，入口sync/README §52。

> **当前CPU闭环（SPEC §59）**：用户JALR双link含同寄存器pop后push，真实取指/
> 公共SRAM I$/IMEM/BPU/ROB/退休/恢复33程序13041拍1848字段三端严格同值。CID
> 由实际取指接受分配，取消流水丢弃年轻身份，最多25活owner/配置预算28，111次
> 回绕无覆盖。I$仍真实SRAM/data、寄存器tag；ISA保持、六IPC新691640，README §51。

> **I$同步验收（SPEC §58）**：用户确定RTL data使用`rtl/common/rv32_sram.sv`公共
> sram_fakeram（16×512bit/32bit写粒度），tag为16×22bit寄存器。实际本树I$/IMEM
> 与模板/RTL/独立参考2324步160字段全等，四实际功能负向有效；真实前端2259/216
> 全等。受阻/年轻同步读结果必须保存，写/禁用周期rdata不能作新读结果。README §50。

> **最新Cache联合门禁（SPEC §57）**：Store ROB.ready统一为拍初SQ槽位图/epoch
> 批量完成，实际RS→LSQ→D$/DMEM→共享PRF/ROB→退休4803步764字段正向严格全等，
> 独立参考344字段含原始Store ready；取消§56屏蔽，入口sync/README §49。

> **当前D$模块对拍（SPEC §55）**：RTL按用户决定改寄存器，正常READY/WAIT映射本树
> 实际DCache＋DMEM；6378步14字段与模板/RTL/独立参考全等，覆盖命中一拍/连续接受、
> 子字扩展、31并行脏缺失、全部128组及随机。软件仅新增只读探针友元，行为/golden
> 保持。RTL额外背压/最终flush和真实恢复单独功能验证，完整三端CPU轨迹另验，README §47。

> SQ已按软件同步RTL（SPEC §45）：ROB可先退休，原SQ缓冲至Cache/内存实际接受；
> 裸core HALT等待SQ请求接受，外层Cache的已接受事务/脏行排空仍由外层负责。RTL14/14
> 含裸core长背压/转发/恢复，完整三端Cache FSM不由该例证明；18＋6严格活动基线保持。

> LQ提交回收统一后（SPEC §44）两软件18＋6结果/退休/周期/统计一致，新clock总和
> 11,967,396/690,814；RTL13/13通过。改变LQ预算的实际测量已更新golden，Cache完整FSM
> 时序仍依第14/15步验收。

> 第12步主树LQ旧响应覆盖新分配已用初始化后置修复（SPEC §43），Cache路由保持。
> 正确word返回边界、旧实际源码负向及18＋6回归通过；完整Cache/DMEM/该整核用例
> 继续依第14/15步验收。§42保存旧错值与上游接口分析。

> 第10步DIV吞吐定版后两软件18 ISA＋6 IPC严格结果/退休/周期/统计一致，新18 ISA clock
> 总和11,967,406，六IPC690,826；RTL13/13通过。Cache完整FSM/端口时序依第14/16步验收（SPEC §40）。

> 第9步三级弹性MUL及真实返回后端通过；两软件18 ISA＋6 IPC严格golden保持，18 ISA
> clock总和12,246,204，六IPC另计690,826。Cache完整三端FSM/端口周期依第14/16步验收（SPEC §38）。

> 第8步方向训练使用BRU真实actualTaken；控制/BPU后端链1518步四方一致，软件18+6
> 总clock12,246,204、RTL13/13通过。缓存/外部返回周期仍以对应专项为准（SPEC §35）。

> 第7步AGU n拍计算、n+1交付/拍末写LSQ地址、n+2地址ready可见；LSQ满不背压地址更新。
> 两软件18+6总clock12,246,204保持，RTL集成13/13；Cache完整三端周期仍按第14步验收。
> 地址整链1195步四方一致，错tag/槽及恢复后旧结果重放拒收；Cache请求接收/返回不在该地址链驱动域。

> 第6步整数整链补正后两软件树18+6严格结果/周期/统计一致，总clock12,246,204；
> Cache事务不在该整数链中，完整三端端口/FSM周期仍按缓存专门步骤验证。

> 第5步恢复接收扩展后RTL11/11（含缓存和core）通过；RS更老项派发及LSU接收按
> 存活窗口/年龄门控。活动软件基线以benchmarks表为准。

> 第4步后端FIFO基础域验收后软件18+6严格周期仍为12,246,204；回填旁路及缓存
> 活动基线见benchmarks。RTL10/10容量/集成回归同时通过。

> **第3步3B-2**：软件ICache→FQ的接收与popConsume共用真实握手；FQ满但弹出可补入，
> ICache四项请求满但本拍实际消费可补入，HALT仅在真实接收时停取。新活动周期与I$统计
> 经两树一致实测复核；3B-1历史周期保持在benchmarks阶段记录中。

> **同行回填供数补正**：只有实际接受的fillFire可给同拍新请求供数。新槽从512-bit
> 总线选word，拍末写raw/身份/ready，不启动SRAM读、不重发IMEM请求；n+1起按序返回。
> 回填仍只写一个SRAM端口，旧refillSlot独立更新；旁路供数计入hit。总clock12,246,204。

> **2026-10-05 3A 容量同步**：D$8 KB、128×64 B、直接映射；I$1 KB 配置与命中边界保持。
> 两软件树 18+6 例完整 x10/retired/clock/统计一致。主树 DCache 不在 tick 查询 CPUstate：
> comb 仅采样目标组的 `DCacheSetSnapshot`，tick 生成写入意图；DMEM 读完成载荷同样在 comb 采样。
> 下方 2026-10-01 验收数值为历史，当前结果见活动 benchmarks。

> 存储层次为两层 + 片上主存：L1I（ICache）、L1D（DCache），下一级是各 128 KB
> 的指令/数据主存（IMEM / DMEM）。**主存端口延迟固定 20 周期**；两树 ICache
> 使用单端口同步 SRAM，队首命中最早 n+1 拍旁路交付 FQ；DCache 命中 load 在接受请求后的
> 下一拍应答，store 命中当拍改行。
> 两个缓存的缺失回填与脏逐出写回统一走 20 周期端口。访存队列如何驱动它们见
> [访存子系统](memory.md)。
> [← 返回 README](../README.md)

> 实现词汇：主树用周期初快照与 `tick()`，模板树用 `Wire`、`Register` 与
> `work()/sync()`；下文描述持久状态、组合命中和周期边界，不依赖其中任一写法。
>
> **当前版本（2026-10-01）**：两树 1 KB SRAM ICache 的队首 n+1 旁路已对齐；主树、模板 Release 与
> 模板 `_DEBUG` 均为 18/18 x10+cycles 对活动 golden。迁移前路径在下表作历史对照。当前测试结果见
> [benchmarks.md](benchmarks.md)，SRAM 化进展见 [process.md](process.md)。
> 同日完成等价参数化后采用 **16 KB / 64 B / 直接映射 DCache**；活动总 clock
> **12,209,929**，相对 64 KB 四路旧配置 **−0.150397%**，满足总 clock 相差 ≤1% 的门槛。

---

## 1. 层次与延迟模型

| 层次 | 容量/组织 | 命中 | 缺失 |
|------|-----------|------|------|
| **L1I** · 两树 ICache | 1 KB，16 行 × 64 B，**直接映射**、单端口同步 SRAM | 队首且无背压时最早 n+1 拍旁路进入 FQ；否则落槽保存 | IMEM 整行回填，**20 周期** |
| **L1I** · 模板迁移前路径（历史对照） | 8 KB，512 行 × 16 B，直接映射、Register 数据阵列 | 命中组合取字、本拍登记请求；最早下一拍进入 FQ | IMEM 16 B 行回填，20 周期 |
| **L1D** · DCache | 8 KB，128 组 × 1 路 × 64 B，**直接映射** | load **1 拍自答**；store 当场落行 | 主存回填 **20 周期**；脏 victim 并行写回 **20 周期** |
| **IMEM / DMEM** | 各 128 KB 字节寻址 | — | — |

主存延迟由两树共同的 `MEM_LATENCY = 20` 定义：取指请求、DCache 回填读和
脏 victim 写回都以该值装载各自的倒计时。性能与命中率基线见仓库根目录
`docs/benchmarks.md`。

### 1.1 缓存几何参数

两树以 `common.hpp` / `common.h` 中的基础配置为源：I$ 为
`NUM_OF_ICACHE_SETS`、`ICACHE_BLOCK_CAP`；D$ 为 `NUM_OF_DCACHE_SETS`、
`NUM_OF_DCACHE_WAYS`、`DCACHE_BLOCK_CAP`。当前分别为 16 组/64 B 行和
128 组/1 路/64 B 行。组数与行长必须为二次幂。

- `*_OFFSET_BITS` / `*_OFFSET_MASK` 从行长派生；`*_INDEX_BITS` / `*_INDEX_MASK`
  从组数派生；`*_TAG_SHIFT = *_OFFSET_BITS + *_INDEX_BITS`，tag 位宽为
  `RV32_WORD_BITS - *_TAG_SHIFT`（当前 I$ 22 bit、D$ 19 bit）。
- 行首 = `addr & ~*_OFFSET_MASK`；组号 = `(addr >> *_OFFSET_BITS) & *_INDEX_MASK`；
  tag = `addr >> *_TAG_SHIFT`。D$ 写回用 victim 自身的 tag 与组号重建行首。
- I$ 的 `ICACHE_WORDS_PER_LINE`、word 索引掩码、lane 索引位宽和 SRAM 行位宽均从
  行长派生；`LineReturn` / IMEM / CPU 接线共享同一 word 数。D$ 读写载荷长度统一为
  `DCACHE_BLOCK_CAP`，模板 tag/way/PLRU 载体使用派生位宽。
- 相联度支持 1 路直接映射或 4 路 tree-PLRU。当前固定选 way 0；模板零位 way/PLRU
  状态为空特化，没有替换状态寄存器。四路配置仍使用原 tree-PLRU 拓扑。
  主树组数压力测试可用 `-DNUM_OF_DCACHE_SETS=64`，索引位宽自动随之变化。

参数化是同配置表示变换：验收要求 x10、retired、cycles 与缓存/分支统计逐项等价。

**2026-10-01 参数化阶段验收（旧 64 KB 四路配置）**：两树 Release 的活动 18 例与独立 IPC 6 例，及模板
`_DEBUG` 与同编译选项改前检查版的 24 例，均逐项一致；活动总 clock 12,228,320，
独立六例几何 IPC 0.625380057590。仓库外不同几何定向用例覆盖 64 B 数据行、
单-word 指令行、高位 tag 区分、行尾子字访问、有序部分写合并与脏 victim 写回；
两树结果/周期对齐，非二次幂行长或组数在编译期拒绝。

**当前直接映射配置验收**：两树 Release 18+6 例结果、retired、新周期与统计一致，模板
`_DEBUG` 24/24 结果和周期通过、零断言。独立六例精确几何 IPC **0.720538578156**；
目标直接映射及两组不同几何的有序缓存边界场景均通过，见 [benchmarks.md](benchmarks.md)。

## 2. 指令侧：ICache + IMEM

### 2.1 ICache（L1I）

- **容量与数据阵列**：1 KB 直接映射（16 行 × 64 B），行内 16 个 32-bit word；
  data 为 `SRAM<16,512,32>`，每次读取一整行、每次回填写满 16 个 lane。tag/valid
  与 4 项请求队列由 ICache 控制状态保存。
- **字节地址划分**：行首 `pc & ~0x3F`，行号 `(pc >> 6) & 15`，行内 word
  `(pc >> 2) & 15`，tag `pc >> 10`。先转换为 word 地址时，行内字段占 4 位。
- **命中读**：组合 tag 命中门控 IMEM 请求；获得 SRAM 读端口后入队，保存请求槽和 word
  下标。第 n 拍接受并读 SRAM，第 n+1 拍若结果对应存活、未就绪队首，`headReadReady()`
  使返回接口直接选择旧 SRAM 输出，FQ 可接收时当拍交付。PC/预测 PC/ckptId 仍取队首请求槽。
- **落槽与消费互斥**：`readConsumed = popConsume && headReadReady()`；已交付的队首读
  跳过结果落槽，避免 pop 后重新置 ready 或模板 Register 双写。背压或非队首读仍在 n+1 拍
  登记原槽，防止后续 SRAM 读覆盖输出；后续按序从保存槽交付。旁路不增加 SRAM 读端口。
- **回填选槽**：`selectRefill()` 从旧 `head` 开始固定扫描 4 个槽，仅在 `age < count`
  的存活窗口中匹配行地址，排除已就绪槽和 `readValid/readIndex` 标识的待落槽 hit。
  选择第一个匹配槽；同一行有多个 miss 时，一份返回只完成一个请求。
  模板保留 `refillValid()` / `refillSlot()` 两个组合函数，以 `valid` 表示占用、`ready`
  表示结果就绪；两个函数使用相同的旧窗口与候选条件。
- **统一接受事件**：`fillFire = lineReturn.valid && selection.valid && !needSquash`。
  它同时驱动 IMEM 的 `lineConsumed`、ICache 的回填输入有效位、tag 更新和 SRAM 写口；
  选中请求的 critical word 直接来自 `LineReturn`，不再经过一次 SRAM 重读。
  无匹配槽时 IMEM 保持返回，不能只因为返回有效就弹出。
- **单端口仲裁**：回填写优先于新命中读；`readBlocked = fillFire && ICache.hit(pc)`
  进入取指准入条件。新 miss 不占用 ICache 读口，可与已获准回填写同拍发送给 IMEM。
  仲裁和 `FetchDecision` 构建必须早于各消费者输入赋值，只反压取指，不冻结后端。
- **顺序交付**：FQ 只消费 ready 的队首，`popConsume = isReturnReady && !haltFetched
  && !FQ.full`。`isReturnReady()/returnRaw()` 统一保存槽与队首 SRAM 旁路的视图，供 FQ、
  pop 和 HALT 共用。年轻请求可以先完成，但不越过更老请求进入 FQ。
- **恢复与停机**：squash 清请求队列及待读有效位、保留缓存行；HALT 检测要求队首已就绪且
  FQ 不满，保证锁存停取时该标记可交付（见 [frontend.md](frontend.md) §2.2）。
- 统计：`VERBOSE=icache` 输出 hits/misses/hit-rate。

主树`comb()`维持原有ICache旧态快照；tick和const helper读取该快照，逐字段写
CPUstate，SRAM从旧datas读取、显式向目标datas写，不再增加模块局部副本或整对象回写。
该原有comb快照不是额外物理SRAM端口。
模板将 `ICacheData` 嵌入 ICache Inner，非 squash 拍由 ICache 调用一次 SRAM `work()`，
父模块 `sync()` 递归提交读输出 Register 并清除端口 Wire 缓存，不额外注册或重复时钟 SRAM。
`readBlocked` 使用未门控的 FetchUnit PC，避免由已门控 `BPU.outPC` 反馈至取指准入形成组合环。

### 2.2 IMEM（指令主存）

- 两树行级突发服务：16 项请求队列 + `remain_cycle=20` 递减；请求以 64 B 对齐，
  完成时打包 **`LineReturn` 16-word 总线**（`{lineAddr, data[0..15]}`），critical word
  索引为 `(pc>>2)&15`。
- 已完成的队首返回持续有效，直到 `fillFire` 确认被对应请求接收；IMEM 才弹出该返回。
  squash 优先清除所有投机请求。
- 与 DCache 回填共享同一主存延迟口径（20 周期）。

## 3. 数据侧：DCache（L1D）

### 3.1 组织与替换

- 8 KB = 128 组 × 1 路 × 64 B；行 `{valid, dirty, tag, datas[DCACHE_BLOCK_CAP]}`；
- **直接映射**：每组唯一行，命中和缺失选路均为 0；脏行在覆盖前并行发出写回与新行回填；
- 几何由 `NUM_OF_DCACHE_SETS` / `NUM_OF_DCACHE_WAYS` / `DCACHE_BLOCK_CAP` 定义；
  偏移、索引、tag 位宽和掩码自动派生，编译期断言守护二次幂行长/组数与地址切分。

### 3.2 两段式状态机（`Phase::READY / WAIT`）

DCache 是 DMEM 的唯一客户（`MemArbiter` 准入的请求只发给 DCache）。处理器访问
在 `READY` 拍组合求值（主树 `sampleSet`，模板 `lookupLine`）：

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
- **缺失**：周期更新在 `cacheRequestBuffer` 里 **park 请求**（包含
  地址/宽度/符号/方向），并 latch 本次准入总线上携带的身份
  （`robTag/memIndex` + 分配的 `targetWay`）——回填完成后据此组装真正的应答；
  同时置 `busy` 进入 `WAIT`。**busy 期间 `MemArbiter` 不再准入**（见
  [memory.md](memory.md) §5）；
- **双通道脉冲**：向 DMEM 发 `DMEMRequest{readValid, writeValid}`。victim 行脏
  时读、写**并行**发出（读回填 + 写回脏行），各 20 周期；**写回地址由 victim
  行自身的 tag 重建**（而不是请求地址的 tag——否则脏数据会落到错误帧，回填再
  读回陈旧值）；
- **WAIT 完成**：`DMEM.isReplyReady() ∧ ¬isWriteBusy()` 时，把 64 B 行写入
  `targetWay`（清 dirty、写 tag、置 valid、更新 PLRU），然后服务 park：load →
  提取字节 + 符号扩展填入 `loadBuffer`；store → 逐字节落行并置脏。最后清
  park/request/busy 回 `READY`；
- **行阵是持久状态**：主树 comb 仅采样目标组和命中/替换选择为 Input；tick 只读取该
  输入与控制快照，构造行/字节/PLRU 写入意图，不读取写入目标。模板以 Register/持久数组
  表达相同边界，不复制整阵。

## 4. DMEM（双口主存）

- **读/写双口各自独立**：`readBusy`/`writeBusy`、`readExecute`/`writeExecute`
  分开计数递减，因此回填读与脏逐出写回可**并行在飞**（各 20 周期）；
- 只接受 DCache 转发的**行级请求**（读行/写行），不再接受任何处理器侧的
  Load/Store 原语；
- 主树comb在读倒计时到1时采样持久阵列的64 B，tick只从Input填回复缓冲；
  写完成时，const writeLine从旧writeExecute取得地址/数据，直接在tick写目标阵列，
  不增加待提交状态或CPU循环末提交调用。
  同周期读/写采用读旧值，后续周期回填可见此前已完成的脏写回；
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
| 两树 ICache | 1 KB · 16 行 × 64 B · 直接映射 · 单端口同步 SRAM · 4 项请求队列 |
| 模板 ICache 验收 | Release / `_DEBUG` 各 18/18 x10+cycles 对活动 golden；逐例与主树一致 |
| DCache | 8 KB · 128 组 × 1 路 × 64 B · 直接映射 · 写回 + 写分配 |
| 命中交付时序 | 两树 L1I 队首最早 n+1 拍旁路进入 FQ；L1D load 1 拍自答 / store 当场落行 |
| 主存延迟 | 固定 **20 周期**（IMEM 回填、DCache 回填、脏逐出写回共用） |
| DMEM | 读/写双口独立在飞（各 20 周期）；仅行级请求 |
| 存储阵列 | IMEM/DMEM 各 128 KB；缓存/主存阵列均为模块持久状态 |

## 相关文档

- [`../README.md`](../README.md) — 数据通路图 / 周期模型 / 性能口径
- [`frontend.md`](frontend.md) — 取指如何消费 L1I（命中组包 / 缺失回填）
- [`memory.md`](memory.md) — LQ/SQ 与 MemArbiter 如何驱动 DCache
- 仓库根目录 `docs/benchmarks.md` — 主存延迟口径与命中率实测
