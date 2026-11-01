# RV32IM 周期级乱序处理器模拟器

C++20乱序核：默认取指/译码/分配/提交为 **2/4/4/4，双ALU、三路写回**，同步当前生产RTL。
主树用拍初快照与`comb()/tick()`，模板树用`Wire/Register`与`work()/sync()`；已同步第17轮PRF单行SRAM、BPU置信度选择及DCache同组命中/返回字捕获。
2026-11-01两树Release及断言四构建的原六perf答案、实际退休数、课程周期全部匹配正式采用RTL；411373拍完整PRF状态及374104条退休PC/周期三端一致。详见[当前基线](docs/hardware-baseline-prf-microbank.md)。

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

当前D$支持缺失在途时同组其他way的Load命中，匹配返回字可供其他旧Load捕获；脏victim写回期间可提前返回请求行数据，owner/MSHR按原完整事务释放。当前软件对照范围见[第17轮记录](docs/hardware-baseline-prf-microbank.md)，历史三端周期见[实施记录](../sync/WIDTH-DECODE-SYNC-20261024.md)。

默认即2/4/4/4、FQ8/IQ8。历史单ALU配置须同时指定`CPU_ALU_UNITS=1`、`CPU_ALU_CDB_PORTS=1`；双ALU/独立双ALU CDB档要求issue4。未验收配置不能套用当前六例周期。

Integer/Branch/Load可由真实授权WB补足旧项的就绪和值；StoreValue仅旁路非控制ALU授权结果。实际被接收的旧项可供离站复用，StoreValue的SQ捕获/LQ通知/RS释放/Issue信用共用同一事件。StoreAddr/MUL/DIV仍读旧PRF；新分配项不当拍执行。

## 构建与验证

从工作区根目录按[当前基线的全新构建示例](docs/hardware-baseline-prf-microbank.md#证据与复验)执行，
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
- 第17轮生产采用及两树同步证据、外部构建位置见[同步记录](docs/hardware-baseline-prf-microbank.md)。同源已审计整核PPA：34584.063 μm²、315.271 MHz、几何IPC 0.903890966873。
- [双提交记录](../sync/DUAL-COMMIT-20261020.md)及[2026-10-18验收](../sync/RTL-TO-SW-SYNC-20261018.md)记录对应历史范围；本轮验证限定默认配置的原六perf。
- [历史文档](docs/_archive/20261018/INDEX.md)保留旧配置、迁移过程及完整算术推导，不作为当前容量/退出规则。
