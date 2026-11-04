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
