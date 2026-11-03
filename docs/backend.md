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

# 后端：重命名、执行、写回与恢复

## 2026-11-02 第21轮当前生产

采用D$无冲突Load提前查询；PRF共享写仲裁、执行宽度、单AGU、LQ/SQ与StoreValue旁路未改。主树24 ticks＋58 helpers纪律通过，四构建原六项与两个外部副本各374104条退休PC/周期匹配RTL，见[当前基线](hardware-baseline-disjoint-load.md)。本次没有重跑完整PRF观察。

## 2026-11-01 第20轮生产采用（优先于以下历史）

PRF在行译码前统一计算同tag写者优先级，再用胜者译码各SRAM行；高序号WB覆盖低序号WB和分配，
年轻分配覆盖同tag旧分配。P0、未ready值、ready/everWritten和上一拍旁路均按既有合同。
两软件126项构建输入身份保持，四个全新构建原六项答案/退休数/课程周期全匹配，见[当前基线](hardware-baseline-prf-global-write.md)。
下方完整PRF和逐条退休观察仍分别属于第17/19轮。

## 2026-11-01 第19轮生产采用

当前两项Store位置缓冲r02的四构建原六项答案/退休/课程周期通过；
两软件外部观察副本各374104条退休PC/周期逐条匹配生产RTL。
主树24 ticks＋58 helpers＋CPU clock及负向纪律检查通过。PRF及执行配置沿用下方合同，
当前证据见[第19轮基线](hardware-baseline-store-location.md)，下方完整PRF观察属于第17轮。

## 2026-11-01 当前生产合同（优先于下方历史）

PRF64中P0恒零，P1–P63分别使用1×32单端口同步SRAM；无写bank持续读，7条上一拍写入记录覆盖写沿读数据空窗。四分配均写PC+4（包括未ready值），年轻分配优先，三WB按端口序号覆盖；ready/everWritten独立。退出冻结时仍推进SRAM读并清除有效写历史，读操作只看旧状态。主树24 ticks＋58 const writer helpers＋CPU clock及错bank/目标读取负向检查通过；模板嵌套SRAM每拍work一次并统一sync。

默认2/4/4/4、双ALU/三CDB、单AGU、ROB32/PRF64/checkpoint64；原StoreValue非控制ALU授权旁路与实际离站复用保持，MUL RS未采用同拍复用。原六项411373拍完整PRF ready/value及374104条退休PC/周期三端一致，见[第17轮基线](hardware-baseline-prf-microbank.md)。

**2026-10-31当前**：补齐生产 StoreValue 的非控制 ALU 授权结果当拍旁路及真实离站槽复用；SQ/LQ/RS/Issue 共用派发事件，新分配优先。共享 CDB 不进入 StoreValue 提前旁路。两树原六 perf 答案、退休、课程周期全部匹配正式采用 RTL，见[同步验收](hardware-baseline-word-production.md)。

**2026-10-30当前合同**：默认2/4/4/4，双ALU独立CDB0/1、共享Load/MUL/DIV CDB2；ROB32/PRF64/checkpoint64。双整数离站槽均可同拍复用，三授权口供Integer/Branch/Load旁路；跳转单口只消费最老控制结果。当前限定六例验收见[基线](hardware-baseline-fetch2.md)，下方单ALU/两CDB默认描述为历史。

**2026-10-29当前合同**：主树已同步模板生产版多路发射、BPU、8 KiB D$及Load RS授权WB/实际离站复用。
两树均支持独立1/2/4宽度，默认1/1/1/2；本轮验证状态见[同步记录](tree-sync-20261029.md)。
以下2026-10-27及更早阶段的“主树不变/仍单发射”仅为历史，不描述当前源码。

## 历史阶段记录

2026-10-27双向同步r02已将RTL的Load RS授权WB当拍派发/值旁路及issue1/2离站复用写入模板；
issue4既有Load复用保留。模板/RTL的Load与Integer/Branch都可用真实授权WB，StoreAddr/StoreValue/MUL/DIV
仍按旧就绪；参考主树不变。旧空槽优先，只有共享AGU实际选中接收的Load才腾位，多lane不得重复占槽，
RS原push优先/单写保持。没有新增执行/CDB/AGU口；采用后复验见[双向记录](../../sync/MUTUAL-OPT-SYNC-20261027.md)。

2026-10-27双向同步r01只补模板D$接受沿同步读；不改RS、执行、WB、提交或恢复。
Load RS的WB当拍派发/issue1及2离站复用仍列后续项；模板issue4已有真实Load离站复用。
参考主树及RTL源码保持，当前证据与范围见[双向记录](../../sync/MUTUAL-OPT-SYNC-20261027.md)。

2026-10-27：本轮只在模板与 RTL 采用 BPU 预测时上下文和 D$8 KiB，参考主树源码不变。
没有新增执行、CDB、提交或恢复级；模板继续 Register 单写/统一 sync，既有宽度选项保留。
下面历史周期对齐结论不能直接用于新配置；当前验收见[采用记录](../../sync/ADOPT-BPU-DCACHE-20261027.md)。

2026-10-26模板同步RTL四宽分配：`CPU_ISSUE_WIDTH=1/2/4`（旧rename宏为同义别名），
四宽配套ROB32/PRF64/checkpoint64、6-bit完整tag和8槽恢复窗；最近实际写者RAW/LINK、WAW链、
稀疏PRF/LSQ排名和七池RS与ROB共用真实连续前缀。单/双宽原路径及主树不改，不新增执行或CDB口。
见[同步记录](../../sync/TEMPLATE-WIDE4-20261026.md)；不将历史三端或PPA结论扩大到此配置。

2026-10-25模板与RTL可选扩展：`CPU_RENAME_WIDTH=2`接通两路顺序重命名/资源分配，支持双Load和双Store入队；默认仍为1，主树仍单宽。组内RAW转发新物理tag、WAW年轻映射胜出，PRF/ROB/RS/LSQ使用同一组实际发射事件。RTL值缓存RS对组内LINK源捕获PC+4/ready，对应模板下拍PRF查询；并非新项本拍执行。详见[模板接线](../../sync/DUAL-RENAME-WIRING-20261025.md)和[RTL验收](../../sync/RTL-DUAL-RENAME-20261025.md)。本文其余单宽描述适用于主树和模板默认配置。

独立译码宽度1/2只改变前端前缀传递及可选FQ/IQ容量，rename宽度另行配置。Integer/Branch已同步RTL的RS4、授权WB旁路与离站复用；历史RS阶段验证范围见[RS记录](../../sync/RS-RTL-TO-SW-20261024.md)，当前结果以各配置最新验收为准，不扩大为全域三端等价。

本文是当前后端契约入口；公共容量见[共同规范](../../CPU-SYNC-SPEC.md)。
MUL/SRT的完整推导、伪代码、位宽表和调试轨迹保留在[详细原文](_archive/20261018/docs/backend.md)的§4.3～4.6、附A/B；
**这些算术定义继续有效，原文的四路CDB、旧容量和结束协议等集成说明已由本文覆盖。**

## 1. 当前重命名与身份

- Issue独立1/2/4，`CPU_RENAME_WIDTH`为`CPU_ISSUE_WIDTH`同义别名。默认ROB16/PRF48，issue4为ROB32/PRF64；RAT32。
- ROB tag为epoch＋slot（默认5 bit、issue4为6 bit），P0不分配；所有消费者共用实际连续前缀。
- 组内最近实际写者决定RAW及WAW oldPhy链，PRF/LSQ按本池排名分配；只借旧信用，不借当拍退休/回收。
- Issue预留ROB、目的物理寄存器、目标RS及所需LQ/SQ槽；LINK目的值为PC+4。
- PRF使用FIFO自由队列；恢复按年龄回收年轻`newPhy`，正常退休回收`oldPhy`，不改分配顺序。
- 迟到完成必须匹配完整tag和ROB存活窗口，不能因槽内残留tag接受已退休/取消指令的写回。

## 2. RS与派发

七池默认容量：Integer/MUL/DIV/Branch/Load/StoreAddr/StoreValue＝4/1/1/4/2/2/2。Integer/Branch容量可用同名CMake/编译宏`CPU_INT_RS_ENTRIES`、`CPU_BRANCH_RS_ENTRIES`覆盖；当前索引上限分别16/4，不要求非环形RS容量为二次幂。
各池按最老就绪指令选择；Load/StoreAddr共用一个AGU口，StoreValue独立交付，每拍最多2项。

新入RS项仍只能从下一拍参与派发。Integer/Branch/Load旧活动项可由本拍已授权且完整身份认证的两条CDB写口补足就绪和值；旧ready值优先，P0常量不匹配，重复命中取低编号口。MUL/DIV/StoreAddr/StoreValue仍只读旧就绪。
Integer/Branch/Load只有被执行端真实接受的旧项才能同拍复用；Load必须赢得共享AGU仲裁。issue1/2优先旧空槽，issue4按可用槽位排名；每槽只能分配一次。分配覆盖释放，主树tick只读快照/Input写目标，模板逐Register单写。
恢复广播拍允许严格更老、已就绪且被执行端真实接收的工作，边界保留不派发、年轻项取消。
后续原存活窗口重放期间RS/FU冻结。

软件从PRF派生RS操作数，RTL缓存ready/value；比较有效语义，不裸比释放槽残留数据。

## 3. 结果与退休

- 两条物理CDB：ALU独占CDB0；MUL/DIV/Load按完整ROB年龄竞争CDB1。
- 未获授权者保持结果和背压；消费者资格、生产者释放和授权是同一事件。
- PRF/ROB拍末接收WB，退休读取旧ready；提交宽度独立1/2/4，默认最多2条连续队首指令。
- `CommitPlan`（模板为ROB输出的`CommitPort`数组）统一驱动ROB、RAT、PRF、LQ、SQ、RAS和计数。
  后一lane不能绕过前一lane；HALT、异常、退出Store截断后续提交。恢复广播及窗口重放仍禁提交。
- 同rd的已提交映射以年轻lane为准；每条oldPhy按年龄追加自由FIFO；本拍回收下拍才可分配。
  已提交RAT/RAS仍用单端口宽字SRAM，先合并写掩码/同槽年轻优先，再写一次。
- Load广播只置已广播标记，实际退休才回收LQ；Store退休后可在SQ等待Cache接受。
- LQ内部保存raw，CDB读出时按存活行的宽度/符号扩展，不把raw缓存直接当作架构值。

## 4. 执行单元

### 4.1 ALU

n拍选择操作数并运算，拍末保存单槽结果；n+1拍CDB，依赖的Integer/Branch/Load可在实际授权的n+1拍旁路派发；其他池最早n+2拍。
不加执行输入寄存级；结果被消费或取消时可同拍重填。

### 4.2 AGU与BRU

AGU共享Load/Store地址口，32-bit模加法；n拍保存地址/tag/目标槽，n+1拍交付LSQ，n+2拍地址ready可见。Load B允许n+1拍的真实旧AGU结果在登记前参与Cache请求；只对已预留活动LQ行、完整tag匹配且无完整SQ转发的Load生效。
LSQ更新须校验目标存活槽与生产者完整tag。
Load C允许Cache返回在捕获raw的同拍竞争CDB，必须检查活动FETCHING行/完整tag/未广播/flush并服从完整Store转发优先。返回与旧buffer结果按年龄选择后再与MUL/DIV竞争，未授权仍捕获raw，授权才标已广播；不是绕过CDB直接唤醒。

BRU保存真实条件结果和next-PC；ALU处理JAL/JALR的链接值/目标。错误预测经FlushArbiter排队后按年龄广播，
不能用next-PC等于PC+4推断分支未跳转。软件BRU四槽、RTL单槽是既有表示差异。

### 4.3 MUL：radix-4 Booth＋CSA

三级弹性流水：19行部分积 → CSA压缩为S/C → 最终64-bit加法结果。
压缩树为19→13→9→6→4→3→2；负Booth行用反码和稀疏+1修正，保留高乘的符号/无符号修正。
`MUL`取低32位，其余高乘取高32位。

无争用时n拍partial、n+1 CSA、n+2 result、n+3 CDB、n+4依赖最早派发。
准入从实际CDB授权向前递推；受阻级valid/data/tag/op整体保持，不能假设共享CDB恒授权。

### 4.4 DIV 实现：SRT radix-4

递推`W[j+1] = 4W[j] - qD`，`q∈{-2,-1,0,1,2}`，保持`|W|≤2D/3`。
归一化、奇偶shift、QDS、on-the-fly商转换及收尾修正均由移位/加减/CSA实现，数据通路不使用宿主乘除取模。

- `B_d=32+shiftD`，`SW=B_d+1`，`PW=SW+2`；S/C寄存器保存的是**P=4W域**，宽35/36 bit。
- QDS使用`bitsD=5, estBits=9`；估计切片按`PW-estBits`取高位，先掩掉估计加法进位再补码折叠。
- CSA递推按完整PW回卷，不能将S/C截成32 bit；on-the-fly拼接避免负字段按位或污染高位。
- 除零、有符号溢出等快速路径独立处理；一般路径`L=ceil(align/2)`次loop，无背压时n+L+3拍CDB，快速路径n+1拍。
- 结果消费同拍可接收下一项；flush取消严格年轻，边界结果无授权时保持。

精确QDS判据、商转换表、收尾和位级黄金轨迹请查[详细原文§4.4～4.6、附A/B](_archive/20261018/docs/backend.md)，
修改DIV前必须核对这些定义及两树`src/DIV/`、`src/include/DIV.hpp`。

### 4.5 除法参数速查

| shiftD | B_d / SW / PW | QDS切片起点PW−9 |
|---|---|---|
| 0 | 32 / 33 / 35 | 26 |
| 1 | 33 / 34 / 36 | 27 |

两档均为`bitsD=5, estBits=9, estPShift=1`，dSlice范围16～31。完整阈值和折算单位见详细原文§4.4/4.5。

### 4.6 实现陷阱

QDS必须按P域切片、先丢估计加法进位再折叠；递推S/C不能截成32 bit，比较两侧保持同号。
on-the-fly的q=0分支不能把负数字段直接按位或入商寄存器。反例、断言条件和黄金轨迹见详细原文§4.6、附B。

## 5. 原存活窗口恢复

广播沿同步读已提交RAT/RAS基线，广播拍禁止退休/分配。随后每拍4个ROB槽（issue4为8槽）：
RAT/RAS重放存活前缀至边界，PRF回收严格年轻后缀；按广播前原liveCount在`offset + RECOVERY_WIDTH >= liveCount`时结束，省略空beat。同步基线读及至少一个重放beat保持，不能只按保留前缀长度结束。

ROB输出共享窄窗，RAT/PRF读tag/rd/newPhy，RAS读tag/PC/已解码hint。head与边界只处理一次，回收保持FIFO顺序。
重放期间冻结RS/FU、ROB、FlushArbiter及CDB消费；默认单取冻结前端，双取仅对当前PC的非控制流hit开放Fetch/FQ/IQ，资格见[前端](frontend.md)。LSQ返回、Cache/AXI和已提交Store继续。

已提交RAT为1×192 bit、6-bit掩码写SRAM；默认RAS为1×256 bit、32-bit掩码写SRAM，valid屏蔽复位未初始化内容。
投机映射/栈、PRF数据和自由队列仍是寄存状态。

## 6. 验证与限制

主树保持只读拍初快照/逐字段写目标；模板保持Register单写/全部work后统一sync。
当前[25例逐拍与专项验收](../../sync/RTL-TO-SW-SYNC-20261018.md)不等于所有异常域或所有参数组合已证明。
课程退出按[访存协议](memory.md)执行，不采用旧的全dirty扫描结束。
