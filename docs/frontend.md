## 2026-11-03：正式采用空IQ整组旁路版（优先于以下历史）

用户在知悉面积／频率代价后要求“采用生产”，已采用`iq-whole-group-ppa-20261103-r01`。面积34819.996867 μm²、Fmax310.303030 MHz、六项几何IPC0.981946949077、评分2.379472345843e-07；相对上一生产IPC+2.807266%、面积+0.387729%、Fmax-1.151515%、评分+9.573112%。IPC×Fmax估算+1.623425%（不是墙钟实测）。本版为已审计评分及IPC最高候选，旧生产和旧评分最优版继续保留。
旧IQ为空时展示旧FQ译码整组，候选展示不依赖后端ready；仅全部有效lane都能分配时授权全部RAT／PRF／ROB／RS／LSQ目标并跳过IQ写入，否则分配数零、整组入IQ。IQ非空保持原连续前缀；按实际有效条数，不要求凑满四条。两软件同步同一门控与单次消费语义，主树只读旧快照/Input逐字段写目标，模板Register单写／统一sync保持；无模块下一状态复制。
保留前端恢复冻结解耦、安全响应鉴权及后端恢复冻结；不改BPU算法／表、访存、容量、执行／写回／退休宽度。不是部分整组旁路，不带入已否决DCache miss接续、接受沿回填字捕获、Store地址通知或第28轮候选。
44项RTL及公共SRAM与已审计候选逐字节认证，复用已完成整核综合／ABC／STA，不重跑综合，也未另建生产路径RTL模型。模型资格来自`iq-whole-group-screen-20261103-r01`的native／converted、局部合同及三个负向控制；PPA归属独立测量目录，不冒称完整状态等价。
两软件全新Release／断言四构建原六项答案／退休数／init+core+drain及外部观察副本退休PC／周期匹配；课程周期9090/18228/3994/6029/163977/168557。旧黄金、镜像、Slave、既有code及三树HEAD／Git索引保持；使用当前生产专用检查器，不套历史黄金周期。
面积<36000与Fmax>300通过，长期几何IPC>1.0985仍未达到。此次为已测候选采用，不是新优化轮次。单候选PPA授权已完成，综合／ABC／STA继续暂停，不恢复第28轮、不自动继续优化。
正式指针`sync/evidence/current-production.json`；证书`sync/evidence/iq-whole-group-production-adoption-20261103-r01/adoption.json`；软件说明`docs/hardware-baseline-iq-whole-group.md`。以下数字均保留各自历史归属。

## 2026-11-03：正式采用前端恢复冻结解耦版（优先于以下历史）

用户在知悉评分和频率代价后要求“应用到生产”，采用`frontend-recovery-decouple-ppa-20261103-r01`。面积34685.510947 μm²、Fmax313.917842 MHz、六项几何IPC0.955133800420、评分2.171584154117e-07；相对上一生产IPC+0.185873%、面积+0.125295%、Fmax-0.735745%、评分-0.245794%。IPC×Fmax估算-0.551240%（非墙钟实测）。本版是已审计IPC最高版，不是评分最优版；评分最优旧生产仍完整保留。
恢复期间继续禁止未知／控制包的新预测请求，按实际返回指令鉴权安全非控制响应，并允许安全FQ→IQ推进；后端分配／派发／写回／退休仍冻结。两软件同步同一门控语义，保留旧快照/Input逐字段写目标和模板Register单写／统一sync纪律。不改BPU预测／训练算法或表、队列容量、存储协议、Slave；不恢复已否决回填字捕获或Store地址通知，不带入第28轮候选。
44项RTL及公共SRAM与已审计候选精确同源，复用其整核综合／ABC／STA，本次采用不重跑综合，也未另建生产路径RTL模型。模型资格来自`frontend-recovery-decouple-screen-20261103-r01`的native／converted及局部定向／负向验证；测量归属独立PPA目录，不冒称新生产路径重建或完整状态等价。
两软件全新Release／断言四构建原六项答案／退休数／init+core+drain及外部观察副本退休PC／周期匹配；课程周期9570/18946/3995/6126/174053/168587。旧黄金、镜像、Slave、既有code、三树HEAD／Git索引保持，专用当前生产检查器验收，不套旧黄金周期。
面积<36000和Fmax>300通过，长期几何IPC>1.0985仍未达到。本次是已测候选的采用，不是新优化轮次。综合／ABC／STA保持暂停；此前单候选综合授权已完成，不恢复第28轮或自动继续优化。
正式指针`sync/evidence/current-production.json`；采用证书`sync/evidence/frontend-decouple-production-adoption-20261103-r01/adoption.json`；软件说明`docs/hardware-baseline-frontend-decouple.md`。以下数字均保留各自历史归属。

## 2026-11-03：采用评分最优生产版；全部综合暂停（优先于以下历史）

用户要求“暂停所有综合任务，将评分最优应用于生产即可”，已采用`round27-lq-address-area-20261103-r01`。面积34642.106287 μm²、Fmax316.244595 MHz、六项几何IPC0.953361761030、评分2.176934924387e-07；相对上一生产IPC+1.415615%、面积-0.104184%、Fmax-0.154416%、评分+5.840915%。IPC×Fmax估算+1.259013%（非墙钟实测）。
采用奇字位置双取指（跨8字节对齐对、不跨I$行、最早控制指令截断）及LQ结果/地址载荷去复位；valid/就绪/身份控制仍复位。不是三Load仲裁组合版，不含第28轮年龄门禁或SQ地址候选；Store位置表仍4项、Store地址/数据RS仍2项、Branch RS仍4项。不改预测/训练算法或表，不恢复被否决字捕获或Store地址通知。
44项RTL及公共SRAM与已审计候选逐字节认证，复用该版本已审计PPA，本次不启动任何综合/ABC/STA。两软件只同步fetch2包几何与恢复时控制截断（模板适配位于BPU.cpp的fetchGroup，不改predict/训练）；保留无效载荷初始化的表示差异，不声称完整状态等价。
两软件全新Release/断言四构建原六项答案/退休数/init+core+drain及外部副本退休PC/周期匹配；课程周期9607/18947/3995/6126/175315/168588。未另做生产路径RTL模型重建；采用资格来自精确同源候选native/converted完整日志和局部契约。
所有综合已暂停；未经用户重新授权，不恢复第28轮或启动新优化综合。第28轮面积任务在dff-map中止，checkpoint/失败与既有产物保留，整轮不标完成。面积/频率门槛通过，长期IPC>1.0985仍未达到；旧黄金/镜像/Slave/既有code、三树HEAD及Git索引保持。
正式指针`sync/evidence/current-production.json`；证书`sync/evidence/score-best-production-adoption-20261103-r01/adoption.json`；软件说明`docs/hardware-baseline-score-best.md`。以下历史数字保留原归属。

## 2026-11-02：正式采用Store命中位置表4项（优先于以下历史）

用户批准“补齐验证和PPA后采纳”，已完成所选候选的整核converted、全新综合/ABC/STA独立审计及两软件Release/断言四构建，正式采用。此前“暂不综合”仅是筛选阶段限制；未修改历史筛选记录。
仅DCache位置元数据2→4，最低无效项优先、已有同行原位训练、victim=(trained+1)&3；不是精确4项LRU。Store地址/数据RS仍2项、发射1/2路，Branch RS仍4项；不改BPU、不恢复已否决字捕获或Store地址通知。
面积34678.235527 μm²、Fmax316.733684 MHz、六项几何IPC0.940054209024、评分2.056799041947e-07；相对第22轮IPC+1.025965%、面积+0.709439%、Fmax-3.093102%、评分-0.471751%。按各自Fmax估算IPC×Fmax-2.098872%（非墙钟实测）。
原六项课程周期9338/19591/4278/6127/177204/168587；native/converted完整日志一致、两软件四构建答案/退休数/init+core+drain匹配，外部观察副本退休PC/周期匹配。44项RTL及公共SRAM、两软件构建输入逐字节认证；复用精确同源已测模型，不声称另做生产路径重建或完整状态等价。
面积<36000及Fmax>300通过，长期IPC>1.0985尚未达到。本次是既有候选的采用补验，不含新面积专项、不冒称完整新优化轮次。旧黄金/镜像/Slave/既有code及Git索引保持；当前验收用专用生产检查器，不套历史黄金周期。
证书：工作区`sync/evidence/store-location4-production-adoption-20261102-r01/adoption.json`；正式指针`sync/evidence/current-production.json`。当前软件说明`docs/hardware-baseline-store-location4.md`，以下测量均保留各自历史归属。


## 2026-11-02：按用户要求删除Store地址通知旁路（优先于历史基线）

Store地址就绪只更新SQ，不再直接向Load通知数据，也不再压住Cache返回。保留Load地址查询SQ、Store数据就绪通知两条转发；未命中两事件的Load等待旧Store提交/写入Cache后读取，保守消歧及完整身份校验不变。
本次全新native/converted原六项及两软件Release/断言四构建答案/退休/课程周期匹配：9338/19591/4278/6317/177506/173548。全新整核综合/ABC/STA独立审计：面积34433.947627 μm²、Fmax326.843281 MHz、几何IPC0.930507527921。
采用证书位于工作区`sync/evidence/store-address-notify-removal-production-20261102-r01/adoption.json`；当前生产以`sync/evidence/current-production.json`为准。下方第21轮等数字保留历史归属，不作为删除后RTL的PPA。

# 前端：取指、译码与预测

## 2026-11-02 第21轮当前生产

前端/BPU源码未改，默认2/4/4/4、预测时上下文合同保持。D$提前查询改变执行时序，median退休分支误预测比第20轮多16次；不能以前端源码未改推断性能不变。两树四构建及外部退休时序复验通过，见[当前基线](hardware-baseline-disjoint-load.md)。以下记录保持历史归属。

**2026-11-01第20轮当前**：正式采用PRF共享写仲裁r02，前端继续使用下方2/4/4/4与预测时上下文合同。
两软件全新四构建原六项结果/计数匹配，当前证据见[第20轮基线](hardware-baseline-prf-global-write.md)；下方为历次记录。

## 2026-11-01 第19轮生产采用

当前生产为两项Store位置缓冲r02。默认2/4/4/4、FQ8/IQ8、BTB32及预测时上下文合同见下；
两树四构建原六项答案/退休/课程周期及外部逐条退休PC/周期复验通过，见[当前基线](hardware-baseline-store-location.md)。

## 2026-11-01 当前生产合同（优先于下方历史）

已同步第17轮包含的BPU置信度选择：仅PC chooser=3且global计数器强（0/3）或local计数器弱（3/4）时选global，否则选local。LHT256×1、local256×3、global/chooser256×2及预测时上下文训练保持；默认2/4/4/4、FQ8/IQ8与replay安全双取包保持。两树四构建原六perf全部匹配正式生产RTL，见[第17轮基线](hardware-baseline-prf-microbank.md)。

**2026-10-31当前**：生产访存与 StoreValue 旁路/复用已同步，两树六例答案、退休及课程周期全部匹配正式采用 RTL，见[同步验收](hardware-baseline-word-production.md)。前端保持2/4/4/4与既有预测算法；以下10-30验收为当时基线。

**2026-10-30当前合同**：默认2/4/4/4、FQ8/IQ8、I$2包。新包按对齐/最早CFI截断，准入按旧返回包信用；replay只放行fetch2当前有效非控制包。当前限定六例验收见[基线](hardware-baseline-fetch2.md)，下方默认/信用缩包描述为历史。

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
