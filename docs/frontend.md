# 前端：取指、译码与预测

**2026-10-29当前合同**：主树已同步模板生产版BPU及独立1/2/4宽度，默认1/1/1/2。
下面旧阶段“主树不变/仍单宽/旧方向表”仅为历史；当前验证见[同步记录](tree-sync-20261029.md)。

## 历史阶段记录

2026-10-27双向同步r02：模板补齐RTL Load RS授权WB派发和值旁路、issue1/2真实离站复用；
不改前端/BPU/宽度，参考主树与RTL源码不变。新整核周期以[双向记录](../../sync/MUTUAL-OPT-SYNC-20261027.md)
及模板专用实测表为准，生产复验状态单列，不继承旧三端周期结论。

2026-10-27双向同步r01：仅模板D$移植RTL的空闲Load接受沿同步读及offset/size快照，
不改本节前端、BPU或宽度。生产复验状态见[双向记录](../../sync/MUTUAL-OPT-SYNC-20261027.md)；
参考主树与RTL源码本轮保持，不据此前端未改就继承旧整核周期。

**2026-10-27 分树采用说明**：生产模板树与 RTL 已采用 LHT256×1、local3/global2、
PC 索引 chooser2，并在 checkpoint 保存预测时局部历史及两分量决定；参考主树源码未改，
下文方向表的旧配置仍描述参考主树，不再是模板当前 BPU 的配置。模板默认周期表见
[`RISC-V-Simulator-Template/docs/benchmarks.md`](../RISC-V-Simulator-Template/docs/benchmarks.md)。
本轮隔离及生产复验均已通过；不能延用旧“三端同周期”结论，见[采用记录](../../sync/ADOPT-BPU-DCACHE-20261027.md)。

2026-10-26模板增补：`CPU_DECODE_WIDTH=4`和`CPU_ISSUE_WIDTH=4`已接入独立四路旧态前缀，
`CPU_RENAME_WIDTH`改为发射宽度兼容别名，默认不变；四宽示例fetch2/decode4/issue4/commit2、FQ8/IQ8。
只改模板实现，主树仍单发射；下方旧前端宏说明为历史，范围与新验收见[四宽同步记录](../../sync/TEMPLATE-WIDE4-20261026.md)。

2026-10-25模板新增独立`CPU_RENAME_WIDTH=1/2`（默认1），不借用旧前端`CPU_ISSUE_WIDTH`名称。IQ按实际0/1/2条发射计算腾出的空间，第二条只取拍初IQ的head+1，不增加译码到发射直通；随后已移植RTL，主树仍单发射。见[模板接线](../../sync/DUAL-RENAME-WIRING-20261025.md)和[RTL验收](../../sync/RTL-DUAL-RENAME-20261025.md)。

当前生产默认是单取指档：I$请求4、AXIFetch4、BTB32、RAS8，与真实RTL 1/1/2完成25例结果/退休/core/total对齐。可选双取指packet、独立CID及恢复期提前取指已合入，两树2/1/2及2/2/2各25例也与对应RTL精确一致；默认与golden保持。

**2026-10-24阶段更新**：取指/译码/提交宽度可独立配置1/2，发射仍单宽；FQ/IQ默认各为译码宽度两倍并允许分别覆盖。实际FQ→IQ传递为旧态前缀，数量由译码宽度、旧FQ占用、IQ空位及实际Issue出队共同决定，两队列消费同一事件。默认仍单取/单译码，下面固定2项描述指默认配置。Integer/Branch的RS4及WB/复用已同步，模板旧双取/共享CID算法已替换。完整边界见[实施记录](../../sync/WIDTH-DECODE-SYNC-20261024.md)。

当前共同配置见[规范](../../CPU-SYNC-SPEC.md)，源码入口为`src/FetchUnit/`、`ICache/`、`Decoder/`和`BPU/`。

## 当前数据流

```text
BPU → FetchUnit → ICache → FQ(2) → Decode/IQ(2) → Issue
                     ↕
               AXIFetch(4行) → AXIBus → 外部Slave
```

- 默认每拍最多接受一个取指请求；I$为1 KiB/64 B直映同步SRAM，4请求槽。`CPU_FETCH_REQUEST_ENTRIES`与D$的8请求槽分离；AXIFetch由`CPU_AXI_FETCH_ENTRIES`独立配置。
- 提交宽度独立1/2/4，默认双提交不扩取指/译码/发射宽度。已提交RAS顺序处理实际退休前缀，各事件先pop后push；
  同一栈槽多次写时保留年轻事件，恢复读完整已提交基线并按原存活窗口重放。
- 命中经过同步读边界，队首且下游可接收时交付FQ，否则保存；空队列不组合直通。
- FQ/IQ满时可用本拍真实消费释放的额度补入；取指预留按真实事件更新。
- 同行实际回填可以供给本拍新接受的请求，不重复发主存请求或读取SRAM新写值。
- AXIFetch逐32-bit AXI读收齐整行后返回。恢复清逻辑请求，已接受旧R继续排空；
  已断言但未握手的AR必须保持VALID/地址，不能撤回。

## 分支预测

### 可选多取指packet

- I$回填时保存每字控制流mask；旧hit在FETCH_WIDTH对齐块剩余空间内形成1..FETCH_WIDTH字packet，按FQ信用和最早CFI截断，冷miss或legacy模式退化单字。
- 单口预测只查询所选CFI；无CFI的已知hit直接顺序前进，不因陈旧BTB/condSeen移位。packet基址与实际预测查询PC不同，诊断不得混比。
- 一个packet占一个I$请求槽，单取默认4槽，多取默认2槽；FQ按指令信用原子接收，译码消费旧前缀，不读本拍新入FQ值。FQ默认取指宽度两倍、IQ默认译码宽度两倍。
- 每条独立CID为base+lane，均保存同一旧GHR及各自预测时局部历史/分量决定；下一CID按实际指令数推进。存活预算为ROB＋I$请求字预算＋FQ＋IQ，checkpoint默认32、issue4为64。
- 恢复期间仅当前PC为I$ hit且两个字均非控制流时开放Fetch/FQ/IQ；后端继续冻结，不消费call/return早事件，所有模块仍统一sync。

| 资源 | 配置 |
|---|---|
| 方向 | Tournament：local256×3 bit（初值3），global/PC-index chooser各256×2 bit（初值1） |
| 历史 | LHT256×1（解析时更新）、GHR8；32/64个checkpoint保存预测时上下文，chooser不使用解析时重新计算的分量决定 |
| 目标 | 寄存器BTB32，`CPU_BTB_ENTRIES`可配置，完整PC匹配；模板tag位宽/位移随容量派生 |
| 类型 / 栈 | condSeen512，specRAS/archRAS各8项，`CPU_RAS_ENTRIES`可配置 |

取指预译码提供call/return早事件；训练读取拍初状态。条件分支训练使用真实`actualTaken`，不从next-PC反推。
BTB同槽竞争的tag/类型与target独立按fetch > jump CDB > taken BRU仲裁，无target写者不覆盖已有target。

JAL按链接目的寄存器决定push；JALR的pop/push hint独立，双hint（含同寄存器）按项目规则先pop再push。
空pop、满push保持；预测读取旧栈顶。

## 恢复与退出

GHR/checkpoint在广播拍回卷；已提交RAS使用同步SRAM，每拍通过ROB共享四槽窗口恢复完整spec栈内容和深度，
head/边界各重放一次。默认单取指期间暂停取指、FQ/IQ和早事件；双取指仅按上述安全hit资格开放前端，后端仍冻结，I$/AXIFetch继续处理在途回填。

终止条件为`offset + RECOVERY_WIDTH >= liveCount`；liveCount是广播前原占用，包含待回收取消后缀，不只重放保留前缀。省略空beat，但同步基线读及至少一个重放beat保持。

默认`0x0ff00513`为普通ADDI。课程MMIO退出Store退休后停取，外部完成规则见[访存](memory.md)。

验证范围见[验收](../../sync/RTL-TO-SW-SYNC-20261018.md)；历史设计和参考文献见[归档原文](_archive/20261018/docs/frontend.md)。
