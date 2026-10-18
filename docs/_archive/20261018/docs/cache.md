# 缓存与存储层次：L1I / L1D / 片上主存

> **2026-10-18 RTL取数结构同步**：D$同步命中数据先选相邻word再作四路字节旋转/截取，
> 一个read grant只计算一次raw，在响应/保存间共享；几何、4 MSHR、8请求及端口时序保持。
> AXIData现8条共享行信用，Slave仍16项。验收见父工作区`sync/RTL-TO-SW-SYNC-20261018.md`。

> **2026-10-17 局部地址/四拍恢复同步**：两软件已按RTL分离raw/victim payload与控制元数据，
> 提前计算lookup候选，对每个way/bank及metadata局部设置地址；只在端口使能时要求地址可消费。
> D$仍4 KiB/两路/4 MSHR/8信用、四拍填一行，每拍2 word，首拍Load早返回。核心恢复的四拍
> 不阻断Cache或AXI；I$继续接收在途回填但前端不消费/新分配。见[memory.md](memory.md)。

> **2026-10-17 第二轮面积优化**：DCache共享小偏移Store冲突判断，raw/victim宽缓冲不做
> 无用复位；容量/4 MSHR/8请求、同行合并、dirty写完成资格和早返回保持。AXIFetch游标化、
> AXIData共享描述符都不改变Cache接口或并发预算，见[memory.md](memory.md)。

> **2026-10-17 AXIData**：只改数据适配器的五通道游标及共享16槽payload；Cache几何、
> MSHR、回填和早返回不变，AXI完整回复仍保留至Cache实际消费；见[memory.md](memory.md)。

> **2026-10-16 两路CDB**：Cache几何、回填与AXI握手不变；Load回复先保存到LQ，随后与
> MUL/DIV竞争共享CDB1。总线争用只延后LQ广播，不丢弃已接受的回复，见[backend.md](backend.md)。

## 2026-10-16 双树AXI同步（当前合同）

D$为4 KiB（64组×2路×32 B），4 MSHR，8请求槽包含MSHR owner；同行Load在首拍回填前合并，整行分4拍、每拍两个32-bit bank写入，Load首拍可返回一次。已预留的组锁至最后回填拍，独立组可继续查询；Store按接受年龄排序，dirty victim实际B完成后才允许其fill完成。
每路8个`SRAM<64,32,8>`及一个`SRAM<64,22>`，valid独立，2-way tree-PLRU。AXIData保持MSHR事务ID，I/D共享AXI4-Lite，数据读写共享16条整行信用，外部每通道16项、10拍服务、256 MiB RAM。退出MMIO Store绕过Cache，不分配/污染行；不恢复全dirty扫描。
两软件共同配置；下方旧DMEM20拍、单miss及RTL对拍记录均为历史，当前golden见`benchmarks.md`。

> **历史主树（2026-10-16，AXI前）**：D$为4 KiB、64组×2路×32 B，4个MSHR及同行Load合并。
> 每路8个`SRAM<64,32,8>`，tag/dirty为每路`SRAM<64,22>`，valid独立寄存。
> DMEM读写共用一个总计16项的混合队列，每拍最多一读一写，共享信用不足时优先写。
> 所有活动项独立推进20拍；回复/写完成回送
> 原MSHR事务ID。I$/IMEM及其接线保持，无I$ MSHR改造。模板/RTL代码尚未同步，
> 下面旧三端逐拍等价结论为历史；主树新活动结果见[benchmarks.md](benchmarks.md)。

> **历史默认（2026-10-14，按RTL同步）**：D$为4 KiB、128组×1路×32 B，8个
> `SRAM<128,32,8>`、`SRAM<128,21>`（tag20＋dirty1）及独立valid。每拍回填64bit，
> 共4拍；缺失Load首个合格回填拍从旧256bit整行回复返回一次，第4拍才安装完整行、
> 消费回复与释放Cache队首。dirty写实际完成资格、持久取消、boot1拍、立即HALT、
> CDB/依赖派发边界保持；I$仍1 KiB/64 B。当前源码回归结果见共同父目录sync/README.md。
> 以下2026-10-13的8 KiB/8拍数据为历史，不是当前默认几何。

> **2026-10-14 4 KiB几何扫描**：两软件/RTL支持32/64/128 B D$行与1/2/4路tree-PLRU。
> 每路独立SRAM/valid，lookup采样`readWayMask`；Store命中当拍用组合命中路，miss冻结
> victim way/tag/dirty/data。最低编号invalid优先，否则PLRU；命中及末拍安装更新MRU。
> 首拍Load只响应一次，末拍才释放Cache队首；回填为4/8/16拍，I$仍64 B且不加流水级。
> 扫描当时默认与活动golden为8 KiB/64 B/1路；固定4 KiB候选只在独立副本运行，综合/度量见
> [4 KiB扫描说明](../../RISC-V-CPU-2026/docs/dcache-4k-sweep.md)。下面为默认/历史记录。

> **历史双树D$（2026-10-13，early restart＋8拍回填）**：data为16个128×32bit/8bit写粒度SRAM，
> metadata为128×20bit SRAM（低19位tag、bit19 dirty），valid独立为128项寄存器数组。
> 查询沿保存readLineValid，与同步SRAM返回对齐；无效行忽略旧tag/dirty。每拍写两个相邻
> bank，64 B行用8拍回填。首个合格回填拍从完整DMEM行返回Load一次，`loadResponded`
> 防重复；该拍清valid，最后一拍更新tag/dirty、置valid、释放队首及消费DMEM回复，不再次返回Load。
> 请求独立入队，只查询旧队首；查询仍并行读全部bank，命中时序保持。
> 脏行整行及旧tag先保存再写回，读写分别记已发送，DMEM回复保持至实际回填消费。
> D$不依赖写/禁用周期的SRAM旧输出；通用SRAM.hpp输出失效检查尚未另行改造。
> HALT提交立即结束，不排空；不增加SRAM复位/扫描周期，core boot仍1拍。RTL与两软件结构
> 已同步；零/非零SRAM初态四方各15,435拍/19字段一致，289次早响应与队首释放独立。
> 旧无early restart的13,041,323/844,639周期为历史基线；新周期/统计及整核门禁见
> 共同父目录sync/README.md，活动golden只在完整验证后刷新。
> 下方排空/最终外存记录为历史。

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
> 使用单端口同步 SRAM，队首命中最早 n+1 拍旁路交付 FQ；DCache入队与查询分开，
> 无更老请求时n拍入队、n+1拍查询、n+2拍命中Load应答或Store写入。
> 两个缓存的缺失回填与脏逐出写回统一走 20 周期端口。访存队列如何驱动它们见
> [访存子系统](memory.md)。
> [← 返回 README](../README.md)

> 实现词汇：主树用周期初快照与 `tick()`，模板树用 `Wire`、`Register` 与
> `work()/sync()`；下文描述持久状态、组合命中和周期边界，不依赖其中任一写法。
>
> **历史版本（2026-10-01）**：两树 1 KB SRAM ICache 的队首 n+1 旁路已对齐；主树、模板 Release 与
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
| **L1D** · 主树DCache | 4 KB，64组×2路×32 B，data/metadata同步SRAM，4 MSHR | 查询下一拍Load应答；安全Load可hit-under-miss；Store按接受顺序 | 最多4个不同set miss并发；主存读/脏写各 **20 周期**；首拍返回Load，本地回填 **4 拍** |
| **IMEM / DMEM** | 各 128 KB 字节寻址 | — | — |

主存延迟由两树共同的 `MEM_LATENCY = 20` 定义：取指请求、DCache 回填读和
脏 victim 写回都以该值装载各自的倒计时。性能与命中率基线见仓库根目录
`docs/benchmarks.md`。

### 1.1 缓存几何参数

两树以 `common.hpp` / `common.h` 中的基础配置为源：I$ 为
`NUM_OF_ICACHE_SETS`、`ICACHE_BLOCK_CAP`；D$ 为 `NUM_OF_DCACHE_SETS`、
`DCACHE_BLOCK_CAP`、`NUM_OF_DCACHE_WAYS`。I$保持16组/64 B行；主树D$为
64组/2路/32 B行，模板旧实现仍128组/1路/32 B行。组数与行长必须为二次幂。

- `*_OFFSET_BITS` / `*_OFFSET_MASK` 从行长派生；`*_INDEX_BITS` / `*_INDEX_MASK`
  从组数派生；`*_TAG_SHIFT = *_OFFSET_BITS + *_INDEX_BITS`，tag 位宽为
  `RV32_WORD_BITS - *_TAG_SHIFT`（I$22bit；主树D$21bit，模板D$20bit）。
- 行首 = `addr & ~*_OFFSET_MASK`；组号 = `(addr >> *_OFFSET_BITS) & *_INDEX_MASK`；
  tag = `addr >> *_TAG_SHIFT`。D$ 写回用 victim 自身的 tag 与组号重建行首。
- I$ 的 `ICACHE_WORDS_PER_LINE`、word 索引掩码、lane 索引位宽和 SRAM 行位宽均从
  行长派生；`LineReturn` / IMEM / CPU 接线共享同一 word 数。D$ 读写载荷长度统一为
  `DCACHE_BLOCK_CAP`；metadata位宽为tag位宽加dirty一位，valid独立为组数×way寄存器。
- D$支持1/2/4路tree-PLRU，主树默认两路，模板默认直映；请求表索引/count
  及byte lane位宽由容量和行长派生。可选配置不代表已经完成完整CPU周期验收。

历史参数化属于同配置表示变换，要求所有指标逐项等价；2026-10-16主树四MSHR及流水
DMEM是用户授权的时序/结构变更，因此重测主树活动表，不要求匹配模板旧周期。

**2026-10-01 参数化阶段验收（旧 64 KB 四路配置）**：两树 Release 的活动 18 例与独立 IPC 6 例，及模板
`_DEBUG` 与同编译选项改前检查版的 24 例，均逐项一致；活动总 clock 12,228,320，
独立六例几何 IPC 0.625380057590。仓库外不同几何定向用例覆盖 64 B 数据行、
单-word 指令行、高位 tag 区分、行尾子字访问、有序部分写合并与脏 victim 写回；
两树结果/周期对齐，非二次幂行长或组数在编译期拒绝。

**历史直接映射配置验收（2026-10-01）**：两树 Release 18+6 例结果、retired、新周期与统计一致，模板
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

- 主树4 KB = 64组×2路×32 B，写回＋写分配，tree-PLRU；最低编号invalid路优先。
- 每way八个`SRAM<64,32,8>`，每bank四个byte lane；metadata为每way的`SRAM<64,22>`，
  tag21bit、dirty1bit，valid为独立寄存器。控制复位不需要清SRAM，宿主冷构造的零存储
  不代表硬件初始化事务。查询同一组时并行读全部way/bank，valid采样至`readWayMask`；
  每个bank只有一个1RW端口，回填每拍只写两个相邻bank（64bit），不会整行并行写入。
- 几何和metadata掩码统一在`common.hpp/common.h`，从组数/行长派生。

### 3.2 主树请求表、四MSHR及同行合并

- 保留4个总请求信用，含仍在回填的MSHR owner；活动槽可独立释放，接受顺序由小位图
  维护，不能使用可能越过ROB年龄窗口的已提交Store tag排序。旧满仍拒收，不满槽同拍替换。
- 四个MSHR保存行地址、owner、victim way/tag/dirty及32 B victim数据；不同set可同时
  向DMEM发读/写，同set保守锁定直到完整回填。分配时冻结victim并清其valid，不额外读SRAM。
- 读/写各从最老未发送MSHR选择一笔，实际接受才置sent。读回复和写完成必须同时匹配
  事务ID及原地址；MSHR只有全部运输完成、末拍发布完整行后才可复用。
- 安全的年轻Load可hit-under-miss；更老D$ Store的任意字节重叠阻塞Load，分配年轻miss
  时还避开更老Store所需set。Store仅作为最老活动请求写入，不新增D$ Store转发。
- 同行Load在MSHR首拍回填之前合并，Store主miss不合并；有更老同set Store屏障时不合并。
  首拍由完整旧DMEM回复生成owner响应，同时把其余等待者的原始字节存入各自已有raw槽。
  每拍仍最多一个Load响应；等待者可在后续拍独立释放，owner仍占信用到末拍。
- SRAM端口优先级为回填 > Store写 > lookup；首拍回填与上一拍hit结果撞车时，先响应
  miss owner，hit落槽保存。只有`readValid`资格下读取旧SRAM结果，不使用写/禁用周期输出。
- 每个MSHR的`fillWord`依次为0/2/4/6。脏行必须已收到实际写完成（或本拍合法写完成），
  才回填；首拍Load只返回一次，末拍更新metadata/valid/PLRU、消费读回复、释放owner/MSHR。
- 已接受Load保存持久`cancelled`，取消不提前复用运输身份，物理事务继续但不写LQ；
  一个owner取消不会取消同MSHR其他存活等待者。LQ/CDB仍读旧态，不新增同拍依赖唤醒。
- 主树所有查询读this/Input，tick逐字段写CPUstate，各SRAM每拍tick一次，无整模块next-state复制。
  模板仍是历史单队首模型，其Register规则和旧周期不代表主树新实现。
- HALT提交立即结束，不等待已提交SQ、D$/DMEM事务或全脏行写回。

## 4. 主树DMEM（读写共享16项混合队列）

- 只有一个`requests[16]`，读写合计最多16项，由共同`head/count`管理；每项含读写类型、
  地址/MSHR ID、倒计时、消费标记及一份32 B载荷，写入数据与读回复复用同一载荷字段。
  先前“读16＋写16”的实现已替换，不是保留两组数组再仅限制总计数。
- 每拍最多接受一读一写，双接受时先追加写再追加读；仅一个旧信用时只接受写，读保留
  未发送资格。旧满即使本拍回复消费也拒收；`canAcceptRead(writePending)`、DMEM tick
  与D$ sent标记使用同一准入条件，不允许两个通道各自误用最后一个信用。
- 所有已接受项独立递减20拍，旧回复背压不暂停后续项执行；不是只执行混合队首。
  `isReadBusy/WriteBusy`仅代表仍有该类型未消费请求，不能取反用作信用判据。
- 只接受 DCache 转发的**行级请求**（读行/写行），不再接受任何处理器侧的
  Load/Store 原语；
- 每拍单读接受、固定执行延迟使同拍至多一项新读完成；comb采样该项对应的持久32 B，
  Input携带物理队列slot和数据，tick按资格保存到该读项回复。队列slot不同于MSHR事务ID。
  写完成时，const writeLine从旧写队列项取得地址/数据，直接在tick写目标阵列，
  不增加待提交状态或CPU循环末提交调用。
  同周期读/写采用读旧值，后续周期回填可见此前已完成的脏写回；
- 读回复携带原行地址/MSHR ID，保持至`fillFire/replyConsumed`；只要共享队列仍有
  相应信用，持有旧回复也可接收新请求。写在20拍处实际落内存，写完成地址/ID保持至
  `writeConsumed`。各类型按自身接受顺序取完成，每拍各最多一项，可越过另一类型的
  未消费项；否则“读回填等脏写完成、写完成又被旧读堵住”会死锁。
- 被确认的项置consumed，只有混合FIFO的连续已消费前缀才回收；后部已确认槽仍计入
  16项占用，不提前复用，不会重复响应/重复写入。回收只读旧状态与本拍消费意图。

## 5. 关键不变量与画像

- **脏逐出不变量**：同组不同 tag 的 store 强制逐出时，写回地址必须由 victim
  的 tag 与组号重建；后续回填必须读到写回后的数据；
- **命中率画像**：18 基准的 I$ / D$ 命中率与逐分型预测准确率在
  仓库根目录 `docs/benchmarks.md`（D$ 覆盖 load+store 全部访存）；
- **验证**：模板`_DEBUG`检验Register单写；外部真实CPU测试检查独立退休/内存参考、
  SRAM无效输出poison和乱序work，不能以历史寄存器D$三端报告证明当前RTL等价。

## 6. 关键规格

| 项 | 规格 |
|----|------|
| 两树 ICache | 1 KB · 16 行 × 64 B · 直接映射 · 单端口同步 SRAM · 4 项请求队列 |
| 历史模板验收 | 旧共同基线Release / `_DEBUG`各18＋6通过；主树升级后的新周期不能用于旧模板门禁 |
| 主树DCache | 4 KB · 64组×2路×32 B · PLRU · 4 MSHR · 同行Load合并 |
| 命中交付时序 | L1I 队首最早 n+1 拍交付FQ；L1D n拍入队、最早n+1查询/n+2服务 |
| 主存延迟 | 固定 **20 周期**（IMEM 回填、DCache 回填、脏逐出写回共用） |
| D$本地回填 | 每way八个32bit bank，每拍写一对，共4拍；首拍Load早返回，末拍安装完整行并消费回复 |
| 主树DMEM | 读写共享总计16项混合队列，各20拍；每拍最多一读一写，最后一信用优先写；完成带MSHR ID |
| 存储阵列 | IMEM/DMEM 各 128 KB；缓存/主存阵列均为模块持久状态 |

## 相关文档

- [`../README.md`](../README.md) — 数据通路图 / 周期模型 / 性能口径
- [`frontend.md`](frontend.md) — 取指如何消费 L1I（命中组包 / 缺失回填）
- [`memory.md`](memory.md) — LQ/SQ 与 MemArbiter 如何驱动 DCache
- 仓库根目录 `docs/benchmarks.md` — 主存延迟口径与命中率实测
