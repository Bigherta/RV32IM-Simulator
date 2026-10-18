# RV32IM 周期级乱序处理器模拟器

C++20单宽乱序核：按序取指/发射/提交，乱序执行；含重命名、ROB、七池RS、Booth乘法和SRT radix-4除法。
主树用拍初快照与`comb()/tick()`，模板树用`Wire/Register`与`work()/sync()`；二者与当前RTL在已验收范围内逐拍一致。

## 当前配置

- ROB16、PRF48、FQ/IQ各2、LQ/SQ各8、七池RS共12槽；两路CDB。
- I$1 KiB/64 B；D$4 KiB/2路/32 B、4 MSHR、8请求槽。
- AXIFetch8行，AXIData共享8项信用；外部Slave每通道16项、默认10拍、256 MiB。
- 同步SRAM基线读＋固定四拍恢复，每拍四槽。

完整参数和周期规则见[共同规范](../CPU-SYNC-SPEC.md)。

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
- 最新验收：四软件构建及两软件各自对RTL的25例均通过；总15,541,955拍（不含boot），退休7,303,489条。
  详见[验收记录](../sync/RTL-TO-SW-SYNC-20261018.md)，不等于全异常域或门级等价证明。
- [历史文档](docs/_archive/20261018/INDEX.md)保留旧配置、迁移过程及完整算术推导，不作为当前容量/退出规则。
