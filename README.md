# RV32IM 周期级乱序处理器模拟器

C++20乱序核：默认取指/译码/发射/提交为1/1/1/2，各宽度可独立配置1/2/4；含重命名、ROB、七池RS、Booth乘法和SRT radix-4除法。
主树用拍初快照与`comb()/tick()`，模板树用`Wire/Register`与`work()/sync()`。2026-10-29起主树同步模板生产版的Tournament BPU、8 KiB D$、接受沿同步读、Load RS授权WB旁路/离站复用及多路分配；不包含实验候选的critical-word、双ALU或第三写回口。

## 当前配置

两树支持`CPU_ISSUE_WIDTH=1/2/4`，`CPU_RENAME_WIDTH`为完全同义别名，冲突配置拒绝编译。所有分配消费者使用同一连续前缀；组内最近RAW/LINK、WAW oldPhy链及PRF/LSQ本池排名保持。未增加执行、Cache或AXI带宽。

- ROB16、PRF48、默认FQ/IQ各2、LQ/SQ各8、七池RS共16槽（Integer/Branch各4）；两路CDB。
- issue4配套ROB32/PRF64/checkpoint64，恢复窗由4槽变为8槽；其他issue宽度保持ROB16/PRF48/checkpoint32。
- I$1 KiB/64 B；D$8 KiB/2路/32 B、4 MSHR、8请求槽。
- BPU：LHT256×1、local PHT256×3、global/PC chooser各256×2；checkpoint保存预测时历史和分量决定。
- I$请求4槽、AXIFetch4行、BTB32、RAS8；AXIData共享8项信用，外部Slave每通道16项、默认10拍、256 MiB。
- 同步SRAM基线读＋原存活ROB窗口恢复，每拍四槽，省略超出原tail的空beat，至少重放一拍。

两树CMake支持独立取指/译码/发射/提交宽度。FQ默认取指宽度两倍，IQ默认译码宽度两倍，也可分别覆盖；单取I$默认4请求，双/四取默认2包，AXIFetch仍4行。packet按对齐块和最早CFI截断，每条独立CID；恢复期仅非控制流hit提前取指。默认基准首表只适用于1/1/1/2，其他配置不能套用默认周期。

历史三端逐例周期结论保留于[实施记录](../sync/WIDTH-DECODE-SYNC-20261024.md)，不直接扩用于本轮新源码或四取/四提交。当前软件对照范围见[本轮记录](docs/tree-sync-20261029.md)。

例如双取四译码四发射：在外部构建命令追加`-DCPU_FETCH_WIDTH=2 -DCPU_DECODE_WIDTH=4 -DCPU_ISSUE_WIDTH=4 -DCPU_COMMIT_WIDTH=2`（FQ4/IQ8）。编译成功不等于该档周期验收；更宽也不保证逐例加速。

Integer/Branch/Load可由真实授权WB补足旧项的就绪和值，仅实际被执行端接收的旧项可供离站复用；StoreAddr/StoreValue/MUL/DIV仍读旧PRF。新分配项不当拍执行。

## 构建与验证

在本目录执行；输出放在仓库外，避免覆盖已有`code`：

```bash
cmake -S . -B /tmp/opencode/rv32-main -DCMAKE_BUILD_TYPE=Release -DCPU_RUNTIME_OUTPUT_DIRECTORY=/tmp/opencode/rv32-main
cmake --build /tmp/opencode/rv32-main -j2
cmake -S RISC-V-Simulator-Template -B /tmp/opencode/rv32-template -DCMAKE_BUILD_TYPE=Release -DCPU_RUNTIME_OUTPUT_DIRECTORY=/tmp/opencode/rv32-template
cmake --build /tmp/opencode/rv32-template -j2
python3 data/course.py test --binary /tmp/opencode/rv32-main/code --peer /tmp/opencode/rv32-template/code
python3 data/course.py smoke --binary /tmp/opencode/rv32-main/code --peer /tmp/opencode/rv32-template/code
```

断言版另建目录，追加`-DCMAKE_CXX_FLAGS=-D_DEBUG -DCMAKE_CXX_FLAGS_RELEASE='-O3 -UNDEBUG'`。
活动语料是`data/course-testcases/`的25个原始课程镜像；golden只读[benchmarks.md](docs/benchmarks.md)首表。
`test.sh`默认25例，`test_IPC.sh`默认六perf，`BP_BIN`指定外部二进制；需要同步语料时用`python3 data/course.py sync`。

## 运行与统计

```bash
RESULT_FULL=1 VERBOSE=clock,branch /tmp/opencode/rv32-main/code < data/course-testcases/correctness_gcd/program.data
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
- 本轮不修改RTL、课程镜像、外部Slave或仓库已有`code`；当前验证状态及外部构建位置见[同步记录](docs/tree-sync-20261029.md)。
- 本轮RTL逐拍/独立ISA、退休专项和验证边界见[双提交记录](../sync/DUAL-COMMIT-20261020.md)。
  详见[验收记录](../sync/RTL-TO-SW-SYNC-20261018.md)，不等于全异常域或门级等价证明。
- [历史文档](docs/_archive/20261018/INDEX.md)保留旧配置、迁移过程及完整算术推导，不作为当前容量/退出规则。
