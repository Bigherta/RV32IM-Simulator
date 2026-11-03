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

# RV32IM 周期级乱序处理器模拟器

C++20乱序核：默认取指/译码/分配/提交为 **2/4/4/4，双ALU、三路写回**，同步当前生产RTL。
主树用拍初快照与`comb()/tick()`，模板树用`Wire/Register`与`work()/sync()`；已同步第17轮PRF单行SRAM、BPU置信度选择及DCache同组命中/返回字捕获。
2026-11-02按用户要求正式采用第21轮无冲突Load接受沿提前查询r02，保留PRF共享写仲裁与两项Store位置缓冲。两软件3项DCache构建输入变化、123项保持；全新Release/断言四构建原六项答案、退休数及课程周期全匹配RTL，两个外部副本各374104条退休PC/周期逐条一致。面积34587.315 μm²、Fmax308.806 MHz、几何IPC0.930507527921，评分+0.717984%；median IPC−0.846006%、Fmax−2.412545%、估算IPC×Fmax−1.643488%的代价已披露，详见[当前基线](docs/hardware-baseline-disjoint-load.md)。第17轮完整PRF观察保持历史归属。

## 当前配置

两树支持`CPU_ISSUE_WIDTH=1/2/4`，`CPU_RENAME_WIDTH`为完全同义别名，冲突配置拒绝编译。所有分配消费者使用同一连续前缀；组内最近RAW/LINK、WAW oldPhy链及PRF/LSQ本池排名保持。双ALU独立CDB0/1，共享Load/MUL/DIV竞争CDB2；Cache/AXI/AGU带宽不变。

- 默认ROB32、PRF64、FQ/IQ各8、LQ/SQ各8、七池RS共16槽（Integer/Branch各4）；三路CDB。
- PRF的P1–P63各用1×32单端口同步SRAM，P0恒零；四分配＋三写回的上一拍记录供写后旁路，无写bank持续读，包含退出冻结阶段。
- issue4配套ROB32/PRF64/checkpoint64，恢复窗由4槽变为8槽；其他issue宽度保持ROB16/PRF48/checkpoint32。
- I$1 KiB/64 B；D$8 KiB/2路/32 B、4 MSHR、8请求槽。
- BPU：LHT256×1、local PHT256×3、global/PC chooser各256×2；仅chooser=3且global强或local弱时选global，checkpoint保存预测时历史和分量决定。
- I$请求2包（每包最多2字）、AXIFetch4行、BTB32、RAS8；AXIData共享8项信用，外部Slave每通道16项、默认10拍、256 MiB。
- 同步SRAM基线读＋原存活ROB窗口恢复，默认每拍8槽，省略超出原tail的空beat，至少重放一拍。

两树CMake支持独立取指/译码/分配/提交宽度，FQ/IQ默认均为译码宽度两倍。packet按对齐块和最早CFI截断，不按FQ余量缩短；准入按旧返回包信用。每条独立CID；恢复期仅当前有效双取包的非控制流hit提前取指。旧基准首表只适用于历史1/1/1/2，不再代表新默认。

当前D$允许新Load在无旧lookup/fill/Store写、读流水空闲或旧Load命中已交付、目标set未锁定时，于接受沿启动同步查询；逐项检查所有旧Store，不同行才允许越过，退出MMIO一律阻止。不是Store写当拍的跨bank并发。两项位置表、同组其他way命中、旧Load返回字捕获和脏victim期间提前返回保持，见[当前合同与验收](docs/hardware-baseline-disjoint-load.md)。

默认即2/4/4/4、FQ8/IQ8。历史单ALU配置须同时指定`CPU_ALU_UNITS=1`、`CPU_ALU_CDB_PORTS=1`；双ALU/独立双ALU CDB档要求issue4。未验收配置不能套用当前六例周期。

Integer/Branch/Load可由真实授权WB补足旧项的就绪和值；StoreValue仅旁路非控制ALU授权结果。实际被接收的旧项可供离站复用，StoreValue的SQ捕获/LQ通知/RS释放/Issue信用共用同一事件。StoreAddr/MUL/DIV仍读旧PRF；新分配项不当拍执行。

## 构建与验证

从工作区根目录按[当前基线的构建与复验说明](docs/hardware-baseline-disjoint-load.md#证据与复验)执行，
通过`sync/experiment_storage.py`设置F盘输出和进程临时目录。已有独立构建的验收入口：

```bash
python3 sync/check_current_production_software.py \
  --main .cpu2026-synthesis/software-production-recheck-new/main/bin/code \
  --template .cpu2026-synthesis/software-production-recheck-new/template/bin/code \
  --out .cpu2026-synthesis/software-production-recheck-new/report-new
```

断言版另建目录，追加`-DCMAKE_CXX_FLAGS=-D_DEBUG -DCMAKE_CXX_FLAGS_RELEASE='-O2 -UNDEBUG'`。
25个原始课程镜像及[旧黄金表](docs/benchmarks.md)完整保留，本基线仅验证原六perf。
旧`test.sh`/`test_IPC.sh`仍读取历史黄金周期，不用于新默认验收；使用上面的专用六例入口，不运行额外回归。
软件`clock`/`timing.total`历史口径不含init=1；RTL课程总周期含该拍，专用入口显式核对`init+core+drain`。

## 运行与统计

```bash
RESULT_FULL=1 VERBOSE=clock,branch ../.cpu2026-synthesis/software-production-recheck-new/main/bin/code < data/course-testcases/perf_median/program.data
```

输入为Verilog-hex字节镜像。程序SW写`0x80000000`，对应AXI B握手后结束；退出Store退休时冻结核心IPC，
已提交SQ/Cache/AXI继续，不全dirty扫描。`RESULT_FULL=1`输出完整32-bit结果；默认输出低8位。
旧HALT镜像仅显式设置`LEGACY_HALT=1`运行，不能套用当前课程周期。

`VERBOSE=clock,branch,icache,dcache`将统计写到stderr。分支正确率按解析时next-PC计算，
Cond/JAL/JALR分型缺失或无样本显示`N/A`；汇总按原始计数加权，不平均百分比。
`data/course.py test --report <文件>`可导出逐例周期与分支计数。

## 设计与验收

- [前端](docs/frontend.md) · [后端与MUL/SRT](docs/backend.md) · [访存](docs/memory.md) · [Cache/AXI](docs/cache.md)
- [模板框架](RISC-V-Simulator-Template/docs/help.md) · [同步与验证入口](../sync/README.md)
- 第18轮生产采用及两树复验证据、外部构建位置见[同步记录](docs/hardware-baseline-local-toggle.md)。同源已审计整核PPA：34617.306 μm²、317.815 MHz、几何IPC 0.903890966873。
- [双提交记录](../sync/DUAL-COMMIT-20261020.md)及[2026-10-18验收](../sync/RTL-TO-SW-SYNC-20261018.md)记录对应历史范围；本轮验证限定默认配置的原六perf。
- [历史文档](docs/_archive/20261018/INDEX.md)保留旧配置、迁移过程及完整算术推导，不作为当前容量/退出规则。
