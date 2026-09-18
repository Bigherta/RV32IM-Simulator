# RV32IM 乱序处理器模拟器

这是一个 C++20 编写的周期级 RV32IM 处理器模型。核心采用 Tomasulo 风格设计：
按序取指和发射、乱序执行、按序提交。

项目同时提供一套简单的硬件建模框架，用 `Register` 表示时序状态，用 `Wire`
表示组合连线。它用于在编写 RTL 前验证指令行为和流水线时序，不是可直接综合的 C++。

## 构建

需要支持 C++20 的编译器，推荐 GCC 12 及以上版本。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

可执行文件默认生成在当前目录：

```bash
./code < data/testcases/gcd.data
# 178
```

模拟器从标准输入读取 Verilog hexadecimal 字节镜像。标准输出是程序结束时
`x10` 的低 8 位，调试信息写入标准错误。

```bash
VERBOSE=clock,icache ./code < data/testcases/gcd.data
```

`0x0ff00513` 被用作 HALT 标记。HALT 提交后，CPU 会先排空 SQ、DCache 和 DMEM
中的在途请求再结束。内存大小为 128 KiB。

## 核心结构

```text
 TAGE / BTB / RAS
         |
 Fetch -> ICache -> FQ(8) -> Decode / IQ(16)
   |                                  |
 IMEM                           Issue + Rename
                               RAT / PRF / ROB
                                      |
                         Reservation Stations
                         |                 |
                 ALU / MUL / DIV / BRU    AGU -> LQ / SQ -> DCache / DMEM

             ALU CDB | Load CDB | MUL CDB | DIV CDB
                              |
                         PRF / ROB
```

| 项目 | 配置 |
| --- | --- |
| ISA | RV32I 整数核心指令 + RV32M |
| 取指 / 发射 / 提交宽度 | 1 / 1 / 1 |
| ROB / PRF / RAT | 64 / 128 / 32 |
| FQ / IQ | 8 / 16 |
| Integer RS | 8 |
| MUL / DIV / Load / Branch RS | 各 4 |
| Store Address / Store Value RS | 各 4 |
| LQ / SQ | 16 / 16 |
| 结果总线 | ALU、Load、MUL、DIV 四路独立 CDB |
| ICache | 8 KiB，直接映射，16 B line |
| DCache | 64 KiB，4 路，16 B line，write-back |
| 主存延迟 | 20 周期 |

分支预测器由 TAGE、BTB、Target Cache 和 RAS 组成。分支误预测、JALR 目标错误和
store-load 顺序违例统一交给 `FlushArbiter` 恢复。

MUL 使用 radix-4 Booth 部分积和 CSA 压缩树；DIV 使用 SRT radix-4 递推。
数据通路不使用宿主 `*`、`/` 或 `%` 计算乘除法结果。

未实现 `fence`、CSR、`ecall/ebreak`、特权级、异常、中断、原子、压缩、浮点和
向量扩展。

## 周期模型

每个周期先调用所有模块的 `work()`，再统一调用 `sync()`：

```text
Module::work() -> sync all Register/Wire -> next cycle
```

| 类型 | 用途 |
| --- | --- |
| `Register<N>` | 读取当前值，`<=` 写入下一周期值 |
| `Wire<N>` | 组合连线，每周期求值一次 |
| `Bit<N>` | 定宽组合中间值 |
| `Module<In, Out, Inner>` | 模块端口和内部状态 |
| `dark::CPU` | 模块调度和统一同步 |

基本约束：

- 跨周期状态使用 `Register`，跨模块通信使用 `Wire`。
- `Wire` 只接线一次，一个 `Register` 每周期最多写一次。
- 同步数组使用 `std::array`。
- `work()` 中用明确的 `if/else` 表示写优先级。
- `Register`、`Wire` 和 `Bit` 的最大位宽为 32 bit。

定义 `_DEBUG` 后，框架会检查 Register 双写、Wire 重复接线和未接线读取。
仓库的构建、运行、测试、框架 API 与常见错误统一见
[`docs/help.md`](docs/help.md)。

## 目录

```text
include/             Register/Wire/Bit/Module 框架
src/CPU/             顶层接线与运行循环
src/include/         CPU 模块声明和公共常量
src/                 各流水线模块实现
data/testcases/      课程测试镜像
data/testcases_rv32im/  RV32M A/B 测试镜像
docs/                使用、架构、迁移回顾和 fmax 文档
```

主要设计文档：

- [`docs/frontend.md`](docs/frontend.md)：取指、译码和分支预测。
- [`docs/backend.md`](docs/backend.md)：发射、执行、写回、提交、MUL/DIV。
- [`docs/memory.md`](docs/memory.md)：LQ/SQ、转发和访存顺序。
- [`docs/cache.md`](docs/cache.md)：ICache、DCache、IMEM、DMEM。
- [`docs/progress.md`](docs/progress.md)：模板迁移回顾、验证政策和关键经验。
- [`docs/fmax-critical-path-analysis.md`](docs/fmax-critical-path-analysis.md)：结构性关键路径分析。
- [`../docs/benchmarks.md`](../docs/benchmarks.md)：测试结果和性能数据的根级 SSOT。
- [`../docs/non-synthesizable-loops.md`](../docs/non-synthesizable-loops.md)：数据通路循环综合策略。

当前参数以 `include/common.h` 和 `src/include/` 中的定义为准。

## 测试

行为回归：

```bash
./test.sh gcd
./test.sh
```

RV32M 硬件/软件 A/B：

```bash
./test_M.sh gcd
QUICK=1 ./test_M.sh
```

`reorder_test` 已随旧测试脚手架退役。框架仍提供 `run_once_shuffle()`，但它不再是仓库的日常回归入口。

## 调试

`VERBOSE` 支持以下主题：

```text
issue,exec,wb,commit,lsq,mem,clock,branch,prf,mdp,bpmiss,icache,dcache
```

多个主题用逗号分隔，`VERBOSE=all` 启用全部输出。`branch` 会产生较多事件日志。

## 限制

- 不支持 ELF 直接加载。
- IMEM 和 DMEM 是独立副本，不支持自修改代码。
- `0x0ff00513` 不能作为普通 `addi` 使用。
- 顶层没有最大周期限制，缺少 HALT 的程序不会自动结束。
- 模型使用 lambda、虚函数和动态对象，不能直接综合。
