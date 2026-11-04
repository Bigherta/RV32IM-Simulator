## 2026-11-07：正式采用跨16B四取指（优先于以下历史）

用户“采纳生产”，在已审计`fetch4-cross16-ppa-20261107-r01`及两软件资格通过后正式发布。面积34947.061567 μm²（+0.713718%），Fmax估计319.301528 MHz（+0.997817%），六项几何IPC1.137201912621（+6.947322%），评分4.372501753378e-07（+30.260993%）。面积<36000、Fmax>300、IPC>1.0985三项均通过；IPC×Fmax估算+8.014461%，非墙钟实测，频率为同理想时钟／无寄生STA估计、非后布局结果。
默认fetch/decode/rename/commit为4/4/4/4，双ALU／三CDB保持。只替换pkg／frontend／ICache三RTL：任意字位置连续最多4条、允许跨16B但不跨64B行、最早CFI截断、cold单条、单预测及逐条PC／CID不变。两请求包槽／派生8条指令信用，FQ8／IQ8／ROB32／PRF64／checkpoint64／I$1KiB保持，四取指仍采用保守恢复冻结，不加入双取指专属恢复扩展；LSU已采用通知提前计算／SQ完成旁路／SQ4-owner0仲裁／Load预取／标准RAS、Cache／AXI／Slave及其他容量执行宽度保持。
两软件默认宽度头与取包几何共4项构建输入变化、122项保持。主树只读旧快照/Input逐字段写目标，模板Register单写／统一sync保持；所有状态writer／预测及训练不改，无模块next-state复制或生产观察探针。全新Release／断言四构建原六答案／退休／init+core+drain及两外部副本各374104条退休PC／周期匹配。周期7620/12379/3740/5477/148089/159739；qsort相对双取指多614拍，其他五项改善。限定原六资格，不是全状态／全输入形式等价。
44RTL／公共SRAM／converted与已审计候选精确同源，复用本次已认证全新整核综合／ABC／STA及native／converted筛选，不重新综合或冒称另建生产路径RTL模型。发布前备份三RTL／四软件／文档／旧指针，旧黄金／镜像／Slave／既有code及三树HEAD索引保持；历史证书不回写。采用证书`sync/evidence/fetch4-cross16-production-adoption-20261107-r01/adoption.json`，正式指针`sync/evidence/current-production.json`；软件说明`docs/hardware-baseline-fetch4-cross16.md`。
单候选授权完成，全部综合／ABC／STA继续暂停，未提交／推送；不自动继续实验、组合、综合或清理，无额外面积专项，不算完整新优化轮。

## 2026-11-06：正式采用LSU通知资格提前计算（优先于以下历史）

用户明确“采用”，在已审计整核PPA及本次两软件资格通过后发布`lsu-notify-precompute-ppa-20261106-r01`。面积34699.405687 μm²（+0.679820%），Fmax316.146959 MHz（+3.056499%），六项几何IPC1.063329023912（不变），评分3.356723806268e-07（+1.669462%）。面积增234.300600 μm²，面积<36000／Fmax>300通过，长期IPC>1.0985仍未达。IPC×Fmax估算+3.056499%，非墙钟实测；同理想时钟无寄生STA估计，不是后布局频率。
只替换`rtl/memory/rv32_lsu.sv`的返回通知优先级组合拓扑：旧LQ×SQ常数行先计算同START同WIDTH／年龄及年轻未知地址或同word屏障，晚到SV／返回身份仅选一位。notify函数／完整事件身份与flush、Load地址事件最终覆盖、全部SQ／LQ状态写入／完整转发／SQ完成旁路／owner取消／SQ4-owner0仲裁／Load预取／标准RAS及容量宽度和Slave保持。不组合ROB候选，不恢复已否决方案。
两软件126项构建输入全部保持，无需算法改写；主树旧快照/Input逐字段写目标、模板Register单写／统一sync纪律不变，不恢复生产观察探针。全新Release／断言四构建原六答案／退休／init+core+drain及两外部副本各374104条退休PC／周期匹配。周期7676/16908/3984/5509/147475/162806全部保持，独立软件raw审计通过；限定原六资格，不是全状态／全输入形式等价。
44RTL／公共SRAM／converted与已测候选精确同源，复用其已审计整核综合／ABC／STA及native／converted局部619252组任意旧态／真实LSU差分／六有效负向和原六完整日志，本次无新综合或生产路径RTL模型重建。发布前备份一RTL／文档／旧指针，旧黄金／镜像／Slave／既有code和三树HEAD索引保持，旧证书不回写。采用证书`sync/evidence/lsu-notify-precompute-production-adoption-20261106-r01/adoption.json`，正式指针`sync/evidence/current-production.json`；软件说明`docs/hardware-baseline-lsu-notify-precompute.md`。
单候选授权完成，全部综合／ABC／STA继续暂停，未提交／推送。不自动继续实验、组合、综合或清理，无额外面积专项，不算完整新优化轮。

## 2026-11-06：正式采用旧SQ完成事件旁路（优先于以下历史）

用户“采纳当前的SQ完成旁路”后明确“综合”，在本次新整核PPA独立审计、面积<36000／Fmax>300及两软件资格通过后采用`store-completion-events-ppa-20261106-r01`。面积34465.105087 μm²（-0.432579%），Fmax306.770521 MHz（-2.127022%），六项几何IPC1.063329023912（+0.366506%），评分3.301604769298e-07（+0.180573%）。IPC×Fmax估算-1.768312%，非墙钟实测；长期IPC>1.0985仍未达。
仅一LSU完成bitmap：旧有效未提交SQ完整身份的真实地址／数据／非P0匹配WB可通知ROB寄存ready，退休仍读旧ready。SQ／LQ全部状态写入、实际Store退休／访存接受、Store→Load转发、owner／取消、SQ4-owner0仲裁、Cache预取、标准RAS、容量宽度和Slave保持，不恢复已否决地址→Load数据通知，不含三取指。
两软件各SQ组合查询／头文件与CPU接线共6项构建输入同步、120项保持，主树旧快照/Input逐字段写目标，模板Register单写／统一sync保持。只在完成锥读取完整WB，旧SV／LQ转发锥仍只读early-ALU；旧SV owner保存软件数据依赖身份，对应RTL SQ dataTag，不创建模块next-state或生产观察探针。全新Release／断言四构建原六答案／退休／init+core+drain与两外部副本各374104条退休PC／周期匹配。周期7676/16908/3984/5509/147475/162806，相对标准RAS−345/−85/+31/+32/+1105/+953，median IPC+4.494528%，整体+0.366506%，四项回退已披露。限定原六资格，不是全状态／全输入形式等价。
44RTL／公共SRAM／converted与已审计r02精确同源，本次从elaborate全新六checkpoint／整核opt／ABC／STA及独立逐叶面积／248原宏／工具约束／raw退休评分审计，不拼接旧PPA。复用筛选完整双模型资格，不冒称另建生产路径RTL模型。软件r01只因验证入口未导出路径ImportError停止，未构建；r02仅修导入，六源保持，原失败保留。发布前备份一RTL／六软件／文档／旧指针；旧黄金／镜像／Slave／既有code及三树HEAD索引保持，旧证书不回写。采用证书`sync/evidence/store-completion-events-production-adoption-20261106-r02/adoption.json`，正式指针`sync/evidence/current-production.json`；软件说明`docs/hardware-baseline-store-completion.md`。
单候选授权完成，全部综合／ABC／STA恢复暂停，未提交／推送。不自动继续实验、组合、综合或清理，无额外面积专项，不算完整新优化轮。

## 2026-11-06：正式采用标准同link JALR只push修正（优先于以下历史）

用户“替换生产并综合”，在本次新整核PPA独立审计、面积<36000及Fmax>300和两软件资格通过后采用`ras-same-link-ppa-20261106-r01`。面积34614.841687 μm²（+0.313261%），Fmax313.437404 MHz（-0.306091%），六项几何IPC1.059446088949（+1.509617%），评分3.295653702718e-07（+5.191507%）。IPC×Fmax估算+1.198905%，非墙钟实测；长期IPC>1.0985仍未达。
仅四RTL frontend／core／rename-plan／RAS分类：JALR pop=rs1Link && rd!=rs1、push=rdLink，x1/x5同link只push，异link保留pop后push。同步投机／BTB类型／标量双宽四宽ROB／真实退休／原始指令恢复，RAS容量8、满空与恢复节奏不变；不组合总线2∶1，不改SQ4／owner0仲裁、Cache／Slave／容量／宽度。
两软件各CPU／StaticArbiter共4项构建输入同步、122项保持，decoded hint沿既有退休／恢复路径；主树旧快照/Input逐字段写目标、模板Register单写／统一sync保持。精确复用r04已审计全新Release／断言四构建原六答案／退休／init+core+drain及两外部副本各374104条退休PC／周期资格，不重复构建、不冒称另建生产路径RTL模型。周期8021/16993/3953/5477/146370/161853；towers少372拍、multiply少413拍，qsort多1拍。局部双模型24576组合、真实RAS／SRAM及七有效RTL负向通过；原三夹具失败证据保持，限定原六资格而非全状态／全输入形式等价。
44RTL／公共SRAM／converted精确同源，本次从elaborate全新六checkpoint／整核opt／ABC／STA，独立Liberty逐叶面积／248原宏形状／工具库约束／raw退休评分审计通过，不拼接旧PPA。发布前备份四RTL／四软件／文档／旧指针；旧黄金／镜像／Slave／已有code和三树HEAD索引保持，旧证书不回写。证书`sync/evidence/ras-same-link-production-adoption-20261106-r01/adoption.json`，正式指针`sync/evidence/current-production.json`；软件说明`docs/hardware-baseline-ras-same-link.md`。
单候选授权已完成，全部综合／ABC／STA恢复暂停，未提交／推送。不自动继续实验、组合、综合或清理，无额外面积专项，不算完整新优化轮。

## 2026-11-06：正式采用SQ≤4／owner=0安全Load让行仲裁（优先于以下历史）

用户“采用SQ <=4 且owner = 0的并进行综合”，在新整核PPA及两软件资格通过后采用`lsu-sq4-owner0-ppa-20261106-r02`。面积34506.745567 μm²（+0.259469%），Fmax314.399754 MHz（+2.364139%），六项几何IPC1.043690365779（+0.759869%），评分3.133003593523e-07（+4.965701%）。IPC×Fmax估算+3.141972%，不是墙钟实测。面积<36000及Fmax>300通过，长期IPC>1.0985未达；rsort+2262拍，其他五项改善。
仅LSU＋core两RTL：旧态全部已分配SQ≤4且LSU运输owner=0时，原最老合法Load可越过全部字节不重叠的已提交／同拍真实退休普通RAM Store一次，随后强制真实Store接受；债务跨flush保持、两处SQ只认真实Store grant，恢复冻结／冲刷／退出不新增让行。不改Cache／Slave／容量／执行宽度／原转发，保留既有Load下一行预取。
两软件9项构建输入同步、117项保持；运输owner计数包括取消后的真实返回，Cache新增纯功能终结事件查询而非改变Cache算法或生产观察探针。主树旧快照/Input逐字段写目标，模板Register单写／统一sync，raw proposal-valid与实际接受分离保持。r01新增顶层标量被原纪律检查拒绝，r02模板SQ计数混用epoch尾指针与物理头槽导致周期漂移，两次失败保留；r03用旧有效行计数，未放宽检查。全新Release／断言四构建原六答案／374104条退休／init+core+drain及两外部副本退休PC周期匹配RTL；周期8022/17406/3953/5849/146369/161862。限定原六资格，不是完整状态／全输入形式等价。
44项RTL／公共SRAM／converted与已审计sq4-owner0精确同源，本次从elaborate全新六checkpoint／整核opt／ABC／STA及独立Liberty逐叶面积／248原宏形状／工具库约束／raw退休评分审计完成，不拼接旧PPA；原r01在ABC因服务器重启中断，五个checkpoint与失败产物保留；r02从elaborate全新重测，无checkpoint继承。r02完成后首审因来源标签变量遮蔽失败，旧失败保留；r03依据原五checkpoint路径与哈希重绑定来源，完整重审同一PPA，不重跑综合／ABC／STA。采用阶段复用r02新PPA与r03审计和已通过的软件r03资格，不重复软件构建，也不冒称另建生产路径RTL模型。备份／旧黄金／镜像／Slave／既有code及三树HEAD索引保持，旧证书不回写。采用证书`sync/evidence/lsu-sq4-owner0-production-adoption-20261106-r03/adoption.json`；正式指针`sync/evidence/current-production.json`；软件说明`docs/hardware-baseline-lsu-sq4-owner0.md`。
单候选授权已完成，全部综合／ABC／STA恢复暂停，未提交／推送；不自动继续实验／组合／综合或清理，无额外面积专项，不算完整新优化轮。

## 2026-11-05：正式采用Load触发下一行预取（优先于以下历史）

用户明确“采纳”，采用已审计`load-prefetch-ppa-20261105-r01`。面积34417.443067 μm²（+0.523014%），Fmax307.138572 MHz（-1.979604%），六项几何IPC1.035819497011（+2.458373%），评分2.984787940536e-07（+6.899400%）。IPC×Fmax估算+0.430103%，不是墙钟实测。面积<36000及Fmax>300通过，长期IPC>1.0985未达；towers+10拍，其他五项改善。
仅DCache＋backend两项RTL采用r02同源候选：真实接受普通RAM Load触发下一条32字节行，4项最近源行／单pending目标，最多一个typed内部预取，共享8请求／4MSHR／2bit ID。需求优先、入槽占用≤6／MSHR≤2；未占MSHR冲突可丢弃，已占MSHR保留脏victim／写回／回填所有权。全部CPU返回排除预取类型，不占LSU身份；退出接受后停止新预取。不改容量／宽度／BPU或Slave，不带已否决方案。
两软件10项构建输入同步，116项保持：DCache预取和CPU／MemArbiter原始proposal-valid接线；实际接受事件仍单独门控。主树只读旧快照/Input逐字段写目标，模板Register单写／统一sync保持，无模块下一状态复制或生产观察探针。全新Release／断言四构建原六答案／退休／init+core+drain匹配，两外部副本各374104条退休PC／周期匹配RTL；周期8025/17410/3971/5874/153886/159600。r01两软件rsort均少5拍被门禁拦住；外部状态见证定位满Cache时raw-valid被旧接受门控隐藏，r02仅修软件映射，旧失败／诊断保留，RTL／PPA不变。限定原六资格，不是完整状态或全输入形式等价。
44项RTL／公共SRAM及converted精确同源认证，复用已审计整核综合／ABC／STA和native／converted筛选，本次不新综合，也不冒称另建生产路径RTL模型。旧黄金／镜像／Slave／既有code和三树HEAD索引保持，历史证书不回写。采用证书`sync/evidence/load-prefetch-production-adoption-20261105-r02/adoption.json`，正式指针`sync/evidence/current-production.json`；软件说明`docs/hardware-baseline-load-prefetch.md`。
全部综合继续暂停，未提交／推送；不自动继续实验／组合／综合或清理，无额外面积专项，不算完整新优化轮。

## 2026-11-04：正式采用ROB载荷复位分离（优先于以下历史）

用户要求“采纳ROB 复位分离”，仅采用已审计`rename-area-ppa-sequence-20261104-r01/rob-payload-reset`。面积34238.371507 μm²（-1.349543%），Fmax313.341493 MHz（+1.744186%），六项几何IPC1.010966177065（不变），评分2.792146575072e-07（+4.546957%）。IPC×Fmax估算+1.744186%，非墙钟实测。
仅替换`rv32_rob_lanes.sv`：载荷raw不复位，每行initialized复位并屏蔽未初始化读出；valid／ready／完整tag控制复位及写优先级保持。initialized不跟随valid清除，保留取消项replay和失效端口载荷。不带PRF或ROB SRAM候选，不改容量／宽度／BPU／访存协议。现有frontend格式及等宽截取修复原样保留并记录旧指针差异。
44项RTL／公共SRAM与已审计候选同源，复用该候选已完成整核综合／ABC／STA及native／converted局部与原六资格；本次不新综合、不声称另建生产路径RTL模型或全状态形式等价。两软件126项构建输入不变，无需改算法；全新Release／断言四构建原六答案／退休／init+core+drain及外部副本各374104条退休PC／周期匹配。周期8605/17550/3985/5864/155719/168501。旧黄金／镜像／Slave／既有code和三树HEAD／索引保持。
面积<36000及Fmax>300通过，长期IPC>1.0985未达。面积／频率合格候选中评分最优，不是无面积限制的最高IPC或最高评分者；历史排名／否决保持。采用证书`sync/evidence/rob-payload-reset-production-adoption-20261104-r01/adoption.json`；正式指针`sync/evidence/current-production.json`；软件说明`docs/hardware-baseline-rob-payload-reset.md`。
全部综合／ABC／STA继续暂停，未提交／推送；不自动继续新实验／组合／清理，不算完整新优化轮。

## 2026-11-04：正式采用空FQ＋Store地址RS WB组合（优先于以下历史）

用户要求“采纳，并开始综合”，已在全新PPA及两软件资格通过后采用`fq-sa-combined-ppa-20261104-r01`。面积34706.754007 μm²、Fmax307.969925 MHz、原六项几何IPC1.010966177065、评分2.670710512892e-07。相对上一生产IPC+2.780824%、面积+0.223820%、Fmax-3.248120%、评分+7.489459%。IPC×Fmax估算-0.557621%（非墙钟实测）。排名不自动授权其他候选。
仅旧FQ／IQ均空时返回包直通译码，整组分配或整组进入IQ；宽前端proposal独立ready，真实上下文按实际接受出队，旧路径保持。旧Store地址RS使用已授权CDB值派发，真正获共享AGU接受才返还信用，新分配不得同拍派发。三项RTL变化，不加其他释放信用、已否决PRF排名／Store地址通知／回填字捕获／miss接续，不改BPU算法／表、容量、执行宽度、访存协议或Slave。
44项RTL及公共SRAM与已审计组合精确同源；本次同源全新整核综合／ABC／STA已完成并独立Liberty逐叶面积、约束、六阶段checkpoint及评分审计，不拼接旧PPA。资格来自`fq-sa-combined-screen-20261104-r01`局部native／converted两组oracle、四源码负向和原六项完整日志；不声称完整状态／全输入形式等价或另建生产路径RTL模型。
两软件同步空FQ单次消费与SA旁路，共13项构建输入变化、113项保持；主树只读旧快照/Input逐字段写目标，模板Register单写／统一sync保持。全新Release／断言四构建原六项答案／退休数／init+core+drain及外部副本退休PC／周期匹配。课程周期8605/17550/3985/5864/155719/168501。旧黄金、镜像、Slave、既有code及三树HEAD／索引保持。
面积<36000及Fmax>300通过，长期几何IPC>1.0985仍未达。此次为已筛选组合的PPA及采用，没有额外面积专项，不算完整新优化轮。单候选综合授权已结束，全部综合／ABC／STA恢复暂停，不自动启动其他实验。
正式指针`sync/evidence/current-production.json`；证书`sync/evidence/fq-sa-combined-production-adoption-20261104-r02/adoption.json`；软件说明`docs/hardware-baseline-fq-sa-combined.md`。以下记录保留历史归属。

## 2026-11-04：正式采用ROB Load身份行并行校验版（优先于以下历史）

用户明确要求“可以，采用”，已采用`rob-load-identity-ppa-20261104-r01`。面积34629.246727 μm²、Fmax318.308984 MHz、六项几何IPC0.983613617806、评分2.484625496498e-07；相对上一生产IPC+0.000000%、面积-0.273049%、Fmax+5.377681%、评分+5.955512%。IPC×Fmax估算+5.377681%（不是墙钟实测）。本版为已审计评分最优、最高IPC并列中评分优胜者；排名不自动授权后续采用。
仅`rv32_rob_lanes.sv`独立Load lookup valid改为各行并行完整身份检查后归约；仍检查stored tag低位与常数row、query slot及高位epoch，非split输出全零。ROB全部状态写入和其他输出保持，无新流水级／接口／容量／执行宽度变化，不带入已否决PRF排名方案，不改PRF、RS信用、写回旁路、BPU、访存协议或Slave。整组IQ旁路、非MUL／DIV真实离站信用、前端恢复安全鉴权和后端冻结均保持。
44项RTL与公共SRAM同已审计候选逐字节认证；复用已有整核综合／ABC／STA，本次采用不重跑PPA。模型资格来自`rob-load-identity-screen-20261103-r02`的native／converted组合切片、实际ROB队列oracle、三个源码负向及原六项完整日志，测量根单独归属20261104；不声称另建生产路径RTL资格模型或完整状态／全输入形式等价。旧20261103中断展开记录保留，不冒称已完成或checkpoint续算。
此为RTL组合拓扑等价改写，两软件全部126项构建输入保持，无需改算法。两软件全新Release／断言四构建原六项答案／退休数／init+core+drain及外部观察副本退休PC／周期匹配；课程周期9090/18228/3994/5968/163977/168556。主树旧快照/Input纪律与模板Register单写／统一sync保持，不恢复生产观察探针。旧黄金、镜像、Slave、既有code及三树HEAD／Git索引保持。
面积<36000与Fmax>300通过，长期几何IPC>1.0985仍未达到。本次是既有已测候选采用，不算新优化轮；全部综合／ABC／STA继续暂停，不自动继续新候选。历史指标与否决记录保留，不伪称已清理历史源码／模型重新可用。
正式指针`sync/evidence/current-production.json`；证书`sync/evidence/rob-load-identity-production-adoption-20261104-r01/adoption.json`；软件说明`docs/hardware-baseline-rob-load-identity.md`。以下记录保留各自历史归属。

## 2026-11-03：正式采用非MUL／DIV全RS信用旁路版（优先于以下历史）

用户在知悉频率／评分代价后要求“采纳到生产”，已采用`rs-credit-reuse-ppa-20261103-r01`。面积34724.060467 μm²、Fmax302.064897 MHz、六项几何IPC0.983613617806、评分2.344970490776e-07；相对上一生产IPC+0.169731%、面积-0.275521%、Fmax-2.654867%、评分-1.449979%。IPC×Fmax估算-2.489642%（不是墙钟实测）。本版是已审计最高IPC候选，不是评分最优版；旧评分最优指标与审计记录保留，历史源码／仿真模型／网表缓存按用户清理授权处理，不承诺仍完整存在。
仅Store地址RS补开实际离站信用同拍复用；整数／Branch／Load／Store数据RS原已开启并保持，MUL／DIV仍不复用。共享AGU必须实际选中并接受旧Store地址指令才能返还SA信用，不能把Load获胜或仅有ready候选算作SA离站。新分配写入优先于旧释放，不允许新项同拍派发；不增加写回当拍数据旁路，Store地址RS仍只看旧就绪。
两软件默认四宽同步同一实际AGU离站信用；仅主树StaticArbiter与模板CPU功能接线变化，两个RS文件只更新注释。主树只读拍初快照/Input逐字段写目标，模板Register单写／统一sync保持；无模块下一状态复制。不改容量、执行／写回／退休宽度、BPU算法／表、存储协议或Slave，不恢复已否决Store地址通知Load数据、回填字捕获、DCache miss接续或第28轮方案。原整组IQ旁路、前端恢复安全鉴权与后端冻结保持。
44项RTL及公共SRAM与已审计候选逐字节认证，复用已完成的整核综合／ABC／STA，本次不重跑综合，也不声称另建生产路径RTL模型。模型资格来自`rs-credit-reuse-screen-20261103-r02`的native／converted、七池列表oracle、共享AGU及三个有效负向；PPA归属独立测量目录，不冒称完整状态／全输入等价。
两软件全新Release／断言四构建原六项答案／退休数／init+core+drain和外部观察副本退休PC／周期匹配；课程周期9090/18228/3994/5968/163977/168556。采用阶段与缓存清理互斥；旧黄金、镜像、Slave、既有code和三树HEAD／Git索引保持；当前验收使用专用生产检查器，不套历史黄金周期。
面积<36000和Fmax>300通过，长期几何IPC>1.0985未达。此次是已测候选采用，不算新优化轮次。所有综合／ABC／STA继续暂停，不恢复第28轮或自动继续优化；历史指标索引只重绑定新生产，不伪称已删历史源码／模型仍可复核。
正式指针`sync/evidence/current-production.json`；证书`sync/evidence/rs-credit-reuse-production-adoption-20261103-r01/adoption.json`；软件说明`docs/hardware-baseline-rs-credit-reuse.md`。以下数字保留各自历史归属。

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
