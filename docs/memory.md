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

# 访存：LQ/SQ、转发与退出

## 2026-11-02 第21轮当前生产

新Load接受沿可利用无旧lookup/fill/Store写的同步读端口；读流水须空闲或旧Load命中已实际交付，目标set不得锁定。对Cache内所有旧Store逐项检查32字节行地址，不同行且不存在退出MMIO才可提前；同一行不重叠字节也保守阻止。LSU未知地址消歧、转发、Store按序发送和MMIO退出保持，不是Store写与Load读同拍并发。四构建和外部退休时序已验收，见[当前基线](hardware-baseline-disjoint-load.md)。

**2026-11-01第20轮当前**：正式采用PRF共享写仲裁r02，继续包含下方两项Store位置缓冲与MMIO退出合同。
两软件全新四构建原六项结果/计数匹配，当前证据见[第20轮基线](hardware-baseline-prf-global-write.md)；下方为历次记录。

## 2026-11-01 第19轮生产采用

D$队首非MMIO Store在未ready/无miss/未deferred、旧位置表命中且无回填/set锁时，
可在读流水空闲或当拍交付正常Load命中的条件下直接写原SRAM端口。
两项位置表只记录真实Load命中的行地址/way/有效位，任意miss分配失效同set记录；
训练与PLRU优先级见[Cache合同](cache.md)。原六项四构建及逐条退休复验通过，见[当前基线](hardware-baseline-store-location.md)。

## 2026-11-01 当前生产合同（优先于下方历史）

已同步第17轮D$：在途缺失允许同组其他way的Load命中，其他旧Load可捕获匹配的返回word；请求行数据可在脏victim写回期间提前交付。捕获检查身份、字内范围及Store风险，owner/MSHR和整行安装仍等待完整事务条件。SQ/LQ、单AGU及StoreValue非控制ALU授权事件保持；退出Store退休冻结核心后PRF仍持续读SRAM并清写历史，已提交SQ/Cache/AXI继续完成MMIO退出。六项课程周期及退休三端一致，见[第17轮基线](hardware-baseline-prf-microbank.md)。

**2026-10-31当前**：AXIData关键字先读/寄存字返回、D$整行单拍填充，以及 StoreValue 非控制 ALU 当拍旁路已同步。SQ捕获/LQ通知/RS释放/Issue复用共用同一派发事件；共享Load/MUL/DIV CDB不进入此提前路径。原六项答案、退休、课程周期全匹配，见[验收](hardware-baseline-word-production.md)。

**2026-10-30当前合同**：默认2/4/4/4、双ALU/三WB，Load可使用三授权口；单AGU、LQ/SQ各8、原Store/退出协议不变。仅原六perf验收，计时区间与当前状态见[基线](hardware-baseline-fetch2.md)，下方旧默认为历史。

**2026-10-29当前合同**：主树已同步模板生产版Load RS授权WB/实际离站复用、1/2/4路共同分配前缀及8 KiB D$。
下面旧阶段“主树不变/仍单宽/未移植”仅为历史；当前验证见[同步记录](tree-sync-20261029.md)。

## 历史阶段记录

2026-10-27双向同步r02：模板Load→AGU就绪与值现在共同使用现有真实授权WB，补齐RTL；
只对旧Load项生效，新分配项不当拍执行。issue1/2支持实际Load离站复用，issue4已有复用保留。
被StoreAddr抢占、消费者未接受或冻结的Load不腾位；StoreAddr/StoreValue就绪、LSQ/Cache/AXI/Slave不改。
参考主树源码保持，生产复验状态见[双向记录](../../sync/MUTUAL-OPT-SYNC-20261027.md)。

2026-10-27双向同步r01：模板补齐RTL已有的空队列Load接受沿同步SRAM读，
同沿仍占原请求槽，暖命中最早下一拍响应；无组合请求→响应旁路。
读offset/size随原身份保存；非空队列、Store/MMIO、MSHR及AXI/Slave合同保持。
参考主树未移植，RTL源码未改。验证/采用后复验状态见[双向记录](../../sync/MUTUAL-OPT-SYNC-20261027.md)。

2026-10-27：模板与 RTL 的 D$ 已改为 8 KiB，参考主树仍为 4 KiB；LQ/SQ、单 AGU、
请求/响应端口、AXI 信用和外部 Slave 服务规则不变。尚未采用按部分 R beat 提前返回的
critical-word 旁路，仍须完整行回复就绪才可按原首回填拍响应。生产复验状态见[采用记录](../../sync/ADOPT-BPU-DCACHE-20261027.md)。

2026-10-26模板四宽：所有消费者共享0..4条有序发射事件，Load/Store分别使用本池实际分配排名；
尾快照含自身，LSQ只借旧信用。原内存RS各2项和单AGU限制保持，未扩Cache/AXI/Slave，主树仍单宽。
验收边界见[四宽同步记录](../../sync/TEMPLATE-WIDE4-20261026.md)。

2026-10-25模板可选双重命名：LQ/SQ分别支持同拍0/1/2项顺序分配，满判断只用旧态，不提前借用本拍释放。每条ROB保存包含自身及更老lane的LSQ尾边界，身份含epoch；AGU/Cache请求及SQ发送带宽不变。随后RTL也已接双Load/双Store分配，按既有完整tag/活动位与committed状态恢复，不复制软件尾快照字段；主树仍单宽。见[模板接线](../../sync/DUAL-RENAME-WIRING-20261025.md)和[RTL验收](../../sync/RTL-DUAL-RENAME-20261025.md)。

前端独立译码宽度与FQ/IQ容量不改变本文LQ/SQ或访存带宽。Load A/B/C已同步D$连续lookup、旧AGU提前请求和返回当拍CDB；双取安全hit恢复期提前取指只开放前端，不开放AGU/StoreValue/Load广播或退休。验证配置边界见[实施记录](../../sync/WIDTH-DECODE-SYNC-20261024.md)。

## 当前访存合同

Integer/Branch/Load使用当拍已授权WB派发；Load的AGU操作数和值使用同一授权旁路。StoreAddr/StoreValue仍只读旧PRF，Load B提前的是已寄存AGU结果的请求。

LSQ按同一实际Issue前缀的本池排名分配，ROB尾边界包含自身和更老lane；只使用旧信用，不借当拍退休/发送释放。每条保留完整ROB tag和队列epoch，恢复不改变已提交Store资格。

Load A允许D$命中响应与下一同步lookup重叠。B只对完整身份匹配的旧活动LQ行提前请求，完整SQ转发时不发冗余读；C允许合格Cache返回与旧buffer结果按年龄选择后竞争共享CDB，未授权仍捕获raw，下拍继续竞争，实际授权只广播一次。完整转发优先，已ready/已广播行不被通知再次唤醒。

活动路径：`AGU/StoreValue → LQ/SQ → MemArbiter → DCache → AXIData → AXIBus`。
LQ/SQ各8项，采用包含epoch的队列身份；配置见[共同规范](../../CPU-SYNC-SPEC.md)。

## Store生命周期

1. Issue预留SQ槽；地址、数据由独立路径就绪，StoreValue不占AGU。
2. 旧SQ地址/数据齐备时，组合输出ROB slot/epoch完成位图；ROB校验身份/type/恢复年龄后置ready。
3. ROB按序退休Store，在原SQ槽标记committed；不等待Cache接受。
4. 按SQ顺序发送已提交或本拍实际退休的队首Store；Cache实际接受才释放SQ槽。

已提交Store可跨ROB回绕存活，不用ROB年龄代替SQ发送顺序，也不被分支恢复取消。
多提交可同拍标记多个SQ项，实际Cache发送仍每拍一个。发送资格按同一组实际退休tag计算，
即便Store位于非首lane也可在成为SQ队首时同拍发送；不会把退休提议当作实际提交。

## Load生命周期

1. Issue预留LQ槽，AGU结果经存活槽和完整tag检查后更新地址。
2. 尝试Store转发；不能转发时，检查是否允许进入Cache。
3. Cache/转发返回保存raw，CDB读出时按该行宽度/符号扩展。
4. 与MUL/DIV竞争共享CDB；授权只标已广播，实际ROB退休才回收LQ。

迟到Cache响应必须检查原运输身份及取消状态；新分配初始化不能被同拍旧响应覆盖。
Cache完成、CDB广播和ROB退休是三个不同事件。
多个Load同拍退休时按提交顺序释放LQ项；新分配仍不借用本拍释放的满队列额度。

## 转发与保守准入

- 从更老或已提交Store中选最年轻的字节重叠来源，要求**单源完整覆盖**且数据就绪；不做多源拼接。
- 未知地址Store按既定年龄支配规则阻塞转发；不能仅因另一个已知同址Store就绕过它。
- 未转发Load遇到地址未知或任意字节重叠的更老未提交Store时，不准入Cache；不重叠字节可放行。
- 仅Store数据通知读取旧SQ，通知的地址、宽度和完整tag须与生产者一致。Store地址事件只更新SQ，不再直接通知Load。
- 字节重叠使用同word/相邻word分类，保留非对齐和32-bit地址回绕边界。

## AXI与恢复

AXIData共享8项描述符/word payload信用；读写顺序环各16项，五个epoch进度游标分别跟踪AR/R/AW/W/B。
AW/W独立，普通D$行各需8次32-bit传输；收齐整行R/B后才向Cache呈现完成，并保持至Cache消费。
旧满停收、最后信用写优先；本拍消费释放的槽不被新请求同拍复用。

原存活窗口恢复暂停AGU/StoreValue新事件、Load广播和退休，但LSQ保存真实返回、已提交Store/Cache/AXI继续。恢复按原liveCount省略空beat，不改访存返回身份和已提交Store资格。
不重复交付冻结前的执行事件，也不冻结整个内存系统。

## 课程退出

SW全字写`0x80000000`，`WSTRB=0xf`。该Store按序退休后冻结核心，继续已提交SQ/Cache/AXI；
MMIO等待AXIData读写均空后独立发送，不查询/分配Cache，外部对应B握手后捕获完整结果并结束。
不全dirty写回，不用退休或AW/W握手冒充结束。
退出Store可以位于任一退休lane；它及其之前的老指令计入退休，更年轻lane必须截断。

软件total不含独立boot1拍；`total=core+drain`，IPC分母为core，退休数包含退出Store。
外部Slave仍每通道16项、默认10拍、256 MiB，与主机8条行信用分开。

[验证记录](../../sync/RTL-TO-SW-SYNC-20261018.md) · [Cache细节](cache.md) · [历史原文](_archive/20261018/docs/memory.md)
