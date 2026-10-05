# 大数组 SRAM 化

> 更新日期：2026-10-01。本文维护 SRAM 化阶段、接线约束、验证结果和后续事项。
> 活动镜像为 `data/testcases/` 的 18 个 RV32IM 用例；逐项结果与周期 golden 的唯一来源是
> [benchmarks.md](benchmarks.md) 的 `Testcases` 表。架构细节见
> [cache.md](cache.md)、[frontend.md](frontend.md)、[backend.md](backend.md)、[memory.md](memory.md)。

## 1. 当前进展

| 阶段 | 内容 | 当前状态 |
| --- | --- | --- |
| S0 | 定义 SRAM 端口与周期契约 | 主树与模板均已有 `SRAM.hpp` 模型 |
| S1 | 主树 ICache data 接入 SRAM | 已完成当前版本；1 KB、16×64 B、4 项请求队列，18/18 x10+cycles 对新活动表通过 |
| S2 | 模板 ICache 同配置接线 | 已完成；嵌套 SRAM work/sync、回填握手、端口背压及 HALT 门控对齐，Release / `_DEBUG` 各 18/18 x10+cycles |
| S3 | 命中结果 n+1 拍直接交付 FQ | 已完成；先主树通过再移植模板，队首读旁路、消费/落槽互斥，两树 18/18 与模板 `_DEBUG` 全量通过 |
| S3.1 | I$/D$ 几何参数化 + D$ 面积调整 | 已完成；等价重构验收后采用 16 KB/64 B/直接映射 D$，总 clock −0.150397%，满足 ≤1% 门槛；两树及 `_DEBUG` 18+6 例通过 |
| S4 | DCache data 与 IMEM/DMEM 大存储阵列 | 待按端口需求逐模块推进 |
| S5 | PRF / BPU 表等其他阵列 | 待评估读写端口、初始化和恢复语义后确定 SRAM 组织 |

当前两树完成同配置接线，主树、模板 Release 与模板 `_DEBUG` 均通过活动表 18/18
x10 与 clock 严格门禁，总 clock 均为 **12,209,929**；相对调整前 64 KB 四路 D$ 的
12,228,320 减少 **18,391 拍（0.150397%）**。参数化与 D$ 配置的验收见 [benchmarks.md](benchmarks.md)。

## 2. SRAM 模型契约

- 单端口、同步读写；`addr` 是 SRAM 行/字索引，不是原始字节地址。
- `enable && !writeEnable` 执行读，结果在时钟沿后可用；写或禁用时读输出保持旧值。
- 每拍最多执行一次端口操作，回填写与命中读必须在组合阶段仲裁。
- 数据按 lane 拆分；当前 ICache 使用 `SRAM<16,512,32>`，每行 16 个 32-bit lane，
  回填写入全部 lane。SRAM 输出本身没有请求身份，需要控制状态保存对应槽位和 word 下标。
- 主树从周期初快照读取结果，只对 `CPUstate.ICacheModule.datas` 执行 `tick()` 更新。
  模板的 SRAM 嵌入 ICache Inner，非 squash 拍调用一次 `work()`；父模块 `sync()` 递归
  提交读输出 Register 并清端口 Wire 缓存，不单独注册 SRAM。

## 3. 已完成的双树 ICache 接线

### 3.1 几何与地址

| 项目 | 当前规格 |
| --- | --- |
| 容量 / 组织 | 1 KB；16 行 × 64 B；直接映射 |
| 数据 SRAM | `SRAM<16,512,32>` |
| 请求队列 / 主存延迟 | 4 项 / 20 周期 |
| 行首字节地址 | `pc & ~0x3F` |
| setIndex / wordIndex / tag | `(pc >> 6) & 15` / `(pc >> 2) & 15` / `pc >> 10` |
| 回填总线 | `LineReturn { valid, lineAddr, data[0..15] }` |

`ICACHE_BLOCK_CAP=64` 的单位是字节。一行只有 16 个 word，因此 `pc >> 2` 后的行内字段
占 4 位，而原始字节 PC 的低 6 位在行首地址中清零。DCache/DMEM 现为独立的 64 B 字节载荷。

### 3.2 回填选槽与接受握手

`ICache::selectRefill()` 从旧 head 开始固定检查 4 槽，选择第一个符合以下条件的槽：

1. `age < count`，处于存活窗口；`valid` 只表示结果就绪，不能用它判断槽位是否占用。
2. 请求尚未就绪。
3. 不是 `readValid && readIndex == slot` 标识的待落槽 SRAM hit。
4. 请求的行地址与 IMEM 提供的 `lineReturn.lineAddr` 相同。

组合选择产生 `{valid, slot}`，`refillSlot` 表示请求队列槽，不是 SRAM 行号。
同一行的多个 miss 各自对应一个 IMEM 请求，一份返回只完成一个槽，不广播补齐所有匹配槽。

```text
fillFire = lineReturn.valid && selection.valid && !needSquash
```

同一个 `fillFire` 驱动 IMEM 的 `lineConsumed`、ICache 回填输入有效位、选中请求槽补写和
SRAM 行写入；无匹配时 IMEM 保持返回。选中槽的 critical word 直接来自返回总线。
FQ 只消费队首，因此年轻请求可先完成回填，但不能越过更老请求交付。

模板保留 `refillValid()` / `refillSlot()` 两个组合函数，以相同条件分别提供接受谓词和请求槽。
模板请求槽 `valid` 表示占用、`ready` 表示结果就绪，故候选要求 `valid && !ready`；
占用计数由 valid 位归约，新 hit/miss 都先占槽且未 ready。

### 3.3 取指准入与单端口仲裁

```text
旧快照 → IMEM 返回及回填选槽 → fillFire → readBlocked
       → 构建本拍 FetchDecision → 分发所有消费者输入 → 时钟沿更新

readBlocked = fillFire && ICache.hit(pc)
```

回填写优先于新命中读；冲突时不接受该命中取指，PC、BPU checkpoint 与 ICache 入队都不推进。
新 miss 不占用 ICache 读口，可以与已接受回填写同拍送往 IMEM。反压只作用于取指，后端和
数据访存继续推进。`readValid` 每拍按实际获准的 SRAM 命中读更新，无新读时清零。

模板 `readBlocked` 读取未门控的 FetchUnit PC；不能连接由 `fetchAllowed()` 门控的
`BPU.outPC`，否则会形成 `outPC → readBlocked → fetchAllowed → outPC` 的组合环。

两树 HALT 检测均有 FQ 非满门控：ready 队首为 `0x0ff00513` 且 FQ 可接收时，
交付标记并锁存 `haltFetched`；FQ 满时保持请求等待。squash 优先清请求及待读有效位、保留缓存行。

### 3.4 当前命中时序

```text
第 n 拍：接受命中请求、入队并读取 SRAM
第 n+1 拍：若读身份匹配队首且 FQ 可接收，直接旁路旧 SRAM 输出并 pop
           否则将读结果登记到原请求槽，后续按序交付
```

`headReadReady()` 是组合谓词，不新增 Register：队首占用、尚未 ready、`readValid`
且 `readIndex==head` 时成立。`isReturnReady()` 合并保存槽 ready 与该谓词；`returnRaw()`
在旧 SRAM lane 与请求槽 raw 之间选择。PC/预测 PC/ckptId 始终来自原队首槽。
FQ、pop 与 HALT 共用这组返回视图，无需 FQ 直接访问 ICache 内部或读取 Register 新值。

`readConsumed = popConsume && headReadReady()`，本拍已交付的读结果跳过落槽，
防止 pop 后复活槽位、模板 ready 双写；未消费或年轻读照常保存，以免后续读覆盖 SRAM 输出。
更老 miss 未完成时年轻 hit 不旁路越队。squash 压过交付与落槽；回填写优先的端口仲裁保持原规则。

## 4. 调试与验证记录

### 4.1 已修复：组合分发错拍导致 IMEM 队列溢出

把 `FetchDecision` 构建放在 `CPU::comb()` 末尾、消费者输入赋值之后，会使 FetchUnit、
ICache、IMEM、BPU 收到上一拍决定，而 SRAM 端口使用新决定。容量背压、PC 更新和端口
使能不一致，最终触发 `IMEM::pushRequest()` 的 `count != IMEM_CAP` 断言，退出码为 134。

修复是在所有输入分发前完成回填选择、端口仲裁和本拍决定构建，并全量重编二进制。
单例诊断应直接读取原始 stderr，例如：

```bash
VERBOSE=clock ./code < data/testcases/naive.data
```

### 4.2 历史：S1 主树 n+2 实测结果（2026-09-30）

主树以 `g++ -std=c++20 -O2` 全量直编，18/18 正常退出、x10 与原行为 golden 全对。
旧周期表仅 3/18 一致，其余 15 项均为周期差异；本轮按实测刷新活动表并保持 x10+cycles
双列严格门禁。更新前的完整表已归档于 [benchmarks.md](benchmarks.md)。

文档更新后复跑 `./test.sh`，**18/18 x10+cycles 全部通过**；benchmark 的 18 行、14 列
与当次输出逐项一致。另核验统计汇总、IPC 精度说明及 15 份相关文档的本地链接。

| 指标 | 当时主树 |
| --- | ---: |
| 总 clock / IPC clock | 12,309,770 / 12,309,752 |
| 总 retired | 6,776,149 |
| IPC（脚本逐用例几何汇总） | 0.428542 |
| 分支正确率 | 93.9597%（1,281,294 / 1,363,664） |
| 对更新前活动表的总 clock 变化 | +81,499（+0.6665%） |

IPC 精确重算与脚本末位舍入的差异见 benchmark 汇总说明；这里保留当次脚本输出。
本次同时改变容量、行长、读时序和请求控制，不能把总周期变化单独归因于某一个变量。

回填选择与接受握手另通过 **9 个 ASan/UBSan 定向场景**：空闲及遗留槽、ready 队首背压、
年轻槽先完成、回绕后的重复行、待落槽 hit 排除、同拍 pop/push/refill、squash、完整 32-bit
PC 匹配，以及 IMEM 无匹配时保持返回。临时验证源码和构建产物放在仓库外。

### 4.3 历史：S2 模板 SRAM-I$ n+2 对齐验收（2026-09-30）

保留 `refillValid()` / `refillSlot()`，修正占用/就绪判定、64 B 地址切分和回填完成槽；
命中占槽后等待旧 SRAM 输出，读使能要求 `enable && !writeEnable`。
squash 与普通路径互斥，清待读有效位并压过落槽、回填和 push，守住每个 Register 单写口。
嵌套 SRAM 在构造阶段一次接线，由 ICache 统一 work/sync，顶层共用 `fillFire` 确认 IMEM 消费。

使用仓库外独立构建目录，全量重编主树 `g++ -std=c++20 -O2`、模板 CMake Release 及
Release `-D_DEBUG` 三个版本，运行各自 `./test.sh`：

| 版本 | x10+cycles 严格门禁 | 总 clock | 总 retired | 框架断言 |
| --- | --- | ---: | ---: | --- |
| 主树 | 18/18 | 12,309,770 | 6,776,149 | — |
| 模板 Release | 18/18 | 12,309,770 | 6,776,149 | — |
| 模板 Release + `_DEBUG` | 18/18 | 12,309,770 | 6,776,149 | 零触发 |

三版逐例 x10、clock、IPC clock、retired、整体分支率及 I$/D$ 命中率均一致，活动表无需改数。
模板另通过 **10 个 `_DEBUG` + ASan/UBSan 定向场景**：空队列返回拒绝、ready 队首背压、
年轻 miss 先完成、回绕重复行的年龄选择、待落槽 hit 排除、同拍 pop/refill/push、
squash 压顶、32-bit PC 匹配、连续 SRAM 读及 lane 身份、回填写优先与新 miss 同拍准入。
临时验证源码及产物均在 `/tmp/opencode/`，不进入仓库。

### 4.4 S3：先主树、后模板的 n+1 旁路验收（2026-10-01）

先全量直编并重跑 n+2 主树，确认 18/18 与旧活动表一致，保存完整 x10、clock 和画像。
随后只修改主树 ICache 的队首返回访问器与读结果落槽门控；18/18 完整 32-bit x10、retired
与旧版一致，主树 8 个 ASan/UBSan 定向场景通过后，才将同一逻辑移植到模板。
模板保留 `refillValid()` / `refillSlot()`，仅增加 `headReadReady()` 并扩展现有 ready/raw 访问器。

| 版本 | x10+cycles 验收 | 总 clock | 总 retired | 附加检查 |
| --- | --- | ---: | ---: | --- |
| 主树 `g++ -std=c++20 -O2` | 18/18 | 12,228,320 | 6,776,149 | 完整 x10 对 n+2，ASan/UBSan 8/8 |
| 模板 CMake Release | 18/18，与主树逐项一致 | 12,228,320 | 6,776,149 | 完整 x10、IPC clock、分支及缓存计数逐项一致 |
| 模板 Release + `_DEBUG` | 18/18 | 12,228,320 | 6,776,149 | 零框架断言；`_DEBUG` + ASan/UBSan 定向 10/10 |

定向覆盖：队首 n+1 交付与消费后清空、背压保存、年轻 hit 不越队、待落槽 hit 排除、
squash 压顶、HALT 的旁路/保存共用视图、pop/refill/push 同拍、完整 PC 与 lane 身份、
连续旁路消费并读下一条（含槽位回绕）、回填写优先。验证资产和独立构建目录均置于仓库外。

性能 A/B：总 clock **12,309,770→12,228,320（−81,450，−0.6617%）**，15 快/3 平/0 慢。
superloop **−3.4597%**、queens **−3.2013%**、magic **−2.3024%**；pi 只省 3 拍且占旧
总 clock 63.5666%，其余 17 例合计 **−1.8160%**。精确几何 IPC **+1.3342%**。
活动表已复核 18 行全部 14 列，n+2 完整表归档于 [benchmarks.md](benchmarks.md)。
新增的是 SRAM 已寄存输出至 FQ 的组合 lane/mux 路径；上述 clock 是周期数收益，fmax 待 STA。

## 5. 后续事项与验收

1. **DCache / IMEM / DMEM**：先约定命中读、回填、逐出和双通道访存的端口组织。指令侧
   16-word 取指载荷与数据侧 64-byte 载荷的身份和握手不能混用；同步 RAM 读与主存倒计时必须对齐，避免提前宣告旧数据有效。
2. **PRF / BPU 阵列**：评估多读口、训练写口、初值、同拍冲突和恢复所需访问，确定 bank、
   复制或仲裁后再迁移，不能仅将数组包进单端口模型。

每步优先检查端口授予、请求身份、背压、重复行和 squash，再跑活动镜像回归。
纯表示转换要求同配置 x10+clock 严格一致；有意改变时序时，逐项复核并更新
`benchmarks.md` 的活动表及两树架构文档。当前没有恢复已退役的 reorder_test 验证流程。

```bash
./test.sh gcd
./test.sh                 # 当前活动表的 result + cycles 全量严格门禁
```
## 公共容量同步 3A（2026-10-05）

- FQ/IQ3-bit epoch、LQ/SQ4-bit epoch，实际容量4/4/8/8；完整LSQ尾快照随ROB/Wire运输。
- D$降至8 KiB/64 B直映；保留满时停收和原ALU周期；checkpoint预算28。
- 主树20个tick及const writer helper读this/Input、逐字段写CPUstate，不复制整个模块；D$查询和DMEM读完成
  数据在comb采样，DMEM写完成在tick直接落内存，不增加提交阶段。审计拒绝目标读取等绕过。
- RAS32空pop/满push保持，避免错误路径造成宿主越界；主树/模板24例统计完全对齐。
- 主树O2+断言、模板Release+`_DEBUG`：18/18活动+6/6 IPC通过，活动总clock12,246,363。
  RTL10/10容量/接口单元验证通过。旧16 KiB活动表归档，新基线经过两树逐项复核。
- 验证驱动长期保留于共同父目录sync/；产物均在/tmp/opencode。此为容量验收，其他
  执行缓冲/访存/恢复周期同步留后续步骤，课程AXI顶层未封装。
