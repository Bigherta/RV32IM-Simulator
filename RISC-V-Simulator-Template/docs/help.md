# RISC-V Simulator Template 使用指南

本文是 `RISC-V-Simulator-Template/` 的实际使用入口。除特别说明外，命令都在该目录执行。

## 1. 仓库用途

本目录包含两部分：

- `include/` 是 header-only 的 C++20 周期建模框架，提供 `Bit`、`Register`、`Wire`、
  `dark::Module` 和 `dark::CPU`。
- `src/` 是框架上的完整 RV32IM Tomasulo 风格乱序 CPU 模型，用于验证 ISA 结果、逐周期时序、
  分支恢复、Cache、MUL/DIV 和性能取舍。

它是面向 RTL 的周期模型，不是只执行指令语义的功能模拟器，也不能把整份 C++ 直接交给 HLS
综合。组合逻辑是受支持的：通常由 `Wire` lambda、普通纯函数以及无状态 `Module` 表达；
`Register` 则表示时序状态。

## 2. 环境与构建

### 2.1 前置条件

- CMake 3.10 或更高版本，建议使用较新的 CMake。
- 支持 C++20 的宿主编译器，推荐 GCC 12 或更高版本。
- Linux 或 WSL。回归脚本还使用 Bash、`awk`、`grep`、`sed` 等常见命令行工具。
- 长用例必须使用 Release；未优化构建会显著变慢。

项目通过 CMake 把 `src/include/` 和 `include/` 加入头文件搜索路径。使用框架时采用当前写法：

```cpp
#include "tools.h"
```

不要写旧文档中的 `#include "include/tools.h"`。

### 2.2 Release 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

未显式指定 `CMAKE_BUILD_TYPE` 时，本目录的 CMake 也会默认选择 `Release`。默认可执行文件不是
`build/code`，而是模板目录根部的：

```text
./code
```

现代 CMake 可给构建命令追加 `--parallel`。若希望产物留在构建目录，配置 cache 变量
`CPU_RUNTIME_OUTPUT_DIRECTORY`：

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCPU_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-release"
cmake --build build-release
./build-release/code < data/testcases/gcd.data
```

不同 build 目录若都使用默认输出位置，会依次覆盖同一个 `./code`；并行维护 Release 与检查版时，
建议为每个构建指定独立输出目录。

### 2.3 `_DEBUG` 框架检查版

`_DEBUG` 启用框架自己的检查，包括未接线 `Wire`、`Wire` 重复接线、同周期 `Register` 双写和
动态切片越界。它与 CMake 的 `Debug` 构建类型、标准 C/C++ 的 `NDEBUG` 不是同一个开关。
推荐在 Release 优化下单独定义 `_DEBUG`：

```bash
cmake -S . -B build-assert \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS=-D_DEBUG \
  -DCPU_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-assert"
cmake --build build-assert
./build-assert/code < data/testcases/gcd.data
```

Release 通常仍会定义 `NDEBUG`，因此普通 `assert` 可能关闭，而框架 `_DEBUG` 检查仍然开启。

## 3. 运行程序与查看结果

模拟器不接收程序路径参数，而是从标准输入读取课程使用的 Verilog hexadecimal `.data` 镜像：

```bash
./code < data/testcases/gcd.data
# 178
```

标准输出只有停机时架构寄存器 `x10` 的低 8 位，即 `x10 & 0xff`。`gcd.data` 的当前结果是
`178`。输入程序必须包含本仓库约定的 HALT 指令字 `0x0ff00513`；否则顶层会持续运行。

### 3.1 `VERBOSE` 诊断

诊断写到标准错误，不会污染标准输出的 x10：

```bash
VERBOSE=clock ./code < data/testcases/gcd.data
```

当前输出包含：

```text
clock: 1184
ipc: 0.355030 retired=420 cycles=1183
178
```

`clock` 是端到端仿真周期；IPC 的周期区间在 HALT 提交时冻结，所以示例中两者可以相差一拍。
只提取标准错误统计时可用：

```bash
VERBOSE=clock ./code < data/testcases/gcd.data 2>&1 >/dev/null
```

`VERBOSE` 接受逗号分隔的主题，`1` 或 `all` 打开全部主题：

| 主题 | 用途 |
| --- | --- |
| `clock` | 结束时打印 clock、retired 和 IPC |
| `branch` | 分支/squash 事件与结束汇总；长用例输出可能很大 |
| `icache`、`dcache` | 两个名字目前都打开同一组 ICache/DCache 汇总 |
| `exec` | 当前用于 MUL/DIV 写回等执行事件 |
| `prf` | 物理寄存器写入与链接值 |

解析器还接受 `issue`、`wb`、`commit`、`lsq`、`mem`、`mdp`、`bpmiss`，但当前源码没有对应的
有效打印点，可视为预留主题；设置它们不会自动产生诊断。

例如：

```bash
VERBOSE=clock,branch,icache ./code < data/testcases/gcd.data
```

只比较周期时应单独使用 `VERBOSE=clock`，不要在长用例上无意打开 `branch` 或 `all`。

## 4. 回归与性能脚本

模板目录中的三个脚本以父仓库文档为权威数据源。相对模板目录的根级 SSOT 路径是
`../docs/benchmarks.md` 和 `../docs/ipc_benchmarks.md`；模板 `docs/` 不再保留同名副本。

| 脚本 | 输入 | 根级 SSOT 或输出 | 行为 |
| --- | --- | --- | --- |
| `./test.sh [pattern]` | `data/testcases/*.data` | `../docs/benchmarks.md` | 检查退出码与 x10，报告 clock、retired、IPC、分支率；完整运行含慢用例 `pi` |
| `./test_M.sh [pattern]` | `data/testcases_rv32im/{M,I}` | `../docs/benchmarks.md` | 对比 RV32IM 硬件乘除与 RV32I 软件例程，检查两臂 x10 和 golden |
| `./test_IPC.sh` | `data/testcases_ipc/*/*.data` | 写入 `../docs/ipc_benchmarks.md` | 运行 IPC 语料并重新生成根级报告，不是只读测试 |

常用命令：

```bash
./test.sh gcd
./test.sh '*sort*'
./test.sh

./test_M.sh gcd
QUICK=1 ./test_M.sh        # 跳过 pi

BP_BIN=./build-assert/code ./test.sh gcd
BP_BIN=./build-release/code ./test_IPC.sh
```

`BP_BIN` 可覆盖待测二进制。`test.sh` 的判定硬门槛是 x10；表中的 cycles 同时作为报告和架构
对拍基线。`test_M.sh` 对 RV32M/RV32I 两臂的 clock 做 A/B 比较，但根级 benchmark 表只为这两臂
提供 x10 golden。运行 `test_IPC.sh` 前应确认确实要改写根级 IPC 报告。

## 5. 文档地图

当前实现说明优先阅读以下本地文档：

| 文档 | 内容 |
| --- | --- |
| [README](../README.md) | 总体架构、程序镜像、流水线和目录结构 |
| [backend.md](backend.md) | 发射、派发、写回、提交、MUL 与 SRT DIV；后端算法 SSOT |
| [frontend.md](frontend.md) | 取指、预译码、译码、TAGE/BTB/RAS 与恢复 |
| [cache.md](cache.md) | ICache、DCache、IMEM/DMEM 和延迟模型 |
| [memory.md](memory.md) | LQ/SQ、转发、违例检测与内存请求准入 |
| [progress.md](progress.md) | 从旧快照模型迁移到 `work()+sync()` 的回顾、验证政策和关键经验 |
| [fmax-critical-path-analysis.md](fmax-critical-path-analysis.md) | RTL 映射后的关键路径和 fmax 风险 |

跨两棵实现共用或由根级脚本生成的文档，以父仓库版本为准：

| 根级文档 | 内容 |
| --- | --- |
| [../../docs/benchmarks.md](../../docs/benchmarks.md) | x10、cycles、IPC、分支和 Cache 的回归 SSOT |
| [../../docs/non-synthesizable-loops.md](../../docs/non-synthesizable-loops.md) | 不定长循环与可综合性审计 |
| [../../docs/ipc_benchmarks.md](../../docs/ipc_benchmarks.md) | `test_IPC.sh` 生成的 IPC 报告 |

## 6. 一拍到底发生什么

框架的核心周期语义如下：

```text
本拍开始：所有 Register 读取 _M_old
    |
    +-- dark::CPU 先递增 cycles
    +-- 依次调用每个 Module::work()
    |     Wire 首次读取时执行 lambda，此后本拍读缓存
    |     reg <= value 只写 _M_new，立即重读 reg 仍是 _M_old
    |
本拍结束：依次 Module::sync()
          Register: _M_old = _M_new
          Wire: 清除本拍 lazy cache
          _DEBUG: 清除 Register 的本拍已写标记
```

因此，正确模块只读取本拍稳定的旧状态，通过 `<=` 安排下拍状态。跨模块组合通路可以由多层
Wire 组成，只要它是一张无环 DAG；组合回路会递归求值，不能使用。

## 7. 值类型

三种值类型的位宽范围都是 1 到 32 bit。原生整数写入较窄类型时会像 Verilog 一样截断；两个
框架位类型之间运算或赋值通常要求位宽一致。读取框架值需要显式转换，或用一元 `+` 取得同宽
`Bit`。

### 7.1 `Bit<N>`：组合中间值

`Bit` 是可复制的定宽临时值，适合组合计算、切片和拼接，不保存跨拍状态：

```cpp
Bit<8> value = 0x1ff;                 // 截断为 0xff
value.set<7, 4>(0xAu);                // 写 [7:4]
Bit<4> low = value.range<3, 0>();     // 固定区间
Bit<4> mid = value.slice<4>(0);       // 从 bit 0 起取 4 bit
Bit<8> joined{value.range<7, 4>(), low};
```

还可使用 `sign_extend`、`zero_extend`、`to_signed` 和 `to_unsigned`。`range` 的上下界是模板参数，
`slice` 的起点可在运行时给出。

### 7.2 `Register<N>`：old/new 双缓冲寄存器

```cpp
Register<8> count;
count <= count + 1;                   // 读 old，写 new
```

- 默认构造后 old/new 都是 0，不能通过构造函数直接设置非零初值；非零复位值应通过明确的
  boot/reset 周期写入。
- 本拍 `count <= ...` 后再次读取 `count`，看到的仍是 `_M_old`。
- 周期末 `sync()` 才把 `_M_new` 提交为 `_M_old`。
- 本拍不写时保持上次 `_M_new`，也就是保持寄存器值。
- `_DEBUG` 下同一寄存器一拍只能执行一次 `<=`，即使两次写入相同值也算双写。
- `Register` 禁止复制和移动，不能用 `memcpy`、`memset` 或按值传参处理。

双写通常应改成先计算唯一的 next value，再在单一位置执行一次 `<=`；不要依赖 C++ 语句顺序让
“最后一次写胜出”。

### 7.3 `Wire<N>`：一次接线、每拍懒求值

`Wire` 保存的是求值函数，不是一个可在每拍反复赋值的变量：

```cpp
Register<8> source;
Wire<8> direct;
Wire<8> plusOne;

direct = source;                       // 直接连接同宽 Register
plusOne = [&] { return source + 1; };  // 组合逻辑，返回 Bit<8>
```

- 每根 Wire 在初始化/接线阶段连接一次，通常放在模块构造函数或顶层 `wire()` 中。
- 不要在 `work()` 中给 Wire 重新赋 lambda；`_DEBUG` 会报告 `Wire is assigned twice`。
- 每拍第一次读取时才执行 lambda，并缓存结果；同拍后续读取复用缓存。
- 周期末模块 `sync()` 只清除 Wire 缓存，不改变连接，下一拍首次读取会重新求值。
- 未连接 Wire 被读取时，`_DEBUG` 会报告 `Empty wire is called`。
- Wire 同样禁止复制和移动。lambda 必须捕获生命周期足够长的对象。

返回 `Register` 本身会尝试复制时，可直接使用 `wire = reg`，或显式返回引用/`Bit`：

```cpp
Wire<8> byRef = [&]() -> const auto & { return source; };
Wire<8> byBit = [&] { return +source; };
```

Wire 必须属于已注册模块的 Input/Output/Inner，或由调用者显式执行 `sync_member`。把裸 Wire 放在
CPU 普通成员中却不参与同步，会导致 lazy cache 跨拍不清、读到陈旧值。

## 8. 聚合、同步与 `Module`

### 8.1 可同步聚合

`sync_member(x)` 递归支持：

- 自身提供 `sync()` 的对象，例如 `Register`、`Wire` 和 `Module`。
- `std::array`，元素继续递归同步。
- 满足 `std::is_aggregate_v` 的简单聚合，成员继续递归同步。
- 使用 `SyncTags<Base...>` 声明同步基类的类型。

普通反射聚合的有效上限是 64 个直接成员。超过时应按语义嵌套子结构；一个 `std::array` 只算
一个直接成员，数组元素会递归处理。C 风格数组不受支持，统一改用 `std::array`。

Input、Output 和 Inner 应使用简单 `struct`，避免用户自定义构造函数、私有数据成员等破坏聚合
属性。需要同步私有基类时使用标签：

```cpp
struct Ports { Wire<1> ready; };
struct State { Register<8> count; };

struct Combined : private State, public Ports {
  friend class dark::Visitor;
  using Tags = dark::SyncTags<Ports, State>;
};
```

### 8.2 定义模块

模块由 Input、Output 和可选 Inner 三个聚合组成：

```cpp
#include "tools.h"

struct CounterInput {
  Wire<1> enable;
};

struct CounterOutput {
  Wire<8> value;
};

struct CounterInner {
  Register<8> count;
};

struct Counter : dark::Module<CounterInput, CounterOutput, CounterInner> {
  Counter() {
    value = [this] { return +count; }; // Output Wire 只接一次
  }

  void work() override {
    if (static_cast<bool>(enable))
      count <= count + 1;
  }
};
```

`Module::sync()` 是 final，会自动同步继承的 Input、Output 和 Inner，不应手工逐成员提交。
Input/Output 是 public 基类，Inner 是 protected 基类。顶层接线必须写到模块实例继承的 Input 上：

```cpp
Counter counter;
counter.enable = [] { return 1u; };
```

不要另建一个 `CounterInput input` 再给它接线；那是完全不同的对象，模块自己的 Wire 仍为空。

纯组合模块同样受支持。当前无状态仲裁器采用
`dark::Module<Input, Output>`，在构造函数调用一次项目自定义的 `wire_output()` 完成 Output 接线，
并提供空的 `work()`。`wire_output()` 是本仓库约定的普通成员函数，不是框架自动调用的 API。

### 8.3 本仓库的端口约定

- Input Wire 表示跨模块输入，Output Wire 或只读访问器表示模块对外组合视图，Inner Register
  表示模块私有状态。
- 单生产者信号直接从生产模块 Output 接到消费者 Input；仲裁类多输入组合云使用独立无状态
  Module。
- Wire 最大 32 bit；宽总线应拆成逐字段 Wire 聚合体，而不是塞进一个超宽 Wire。
- 每索引端口使用 `std::array<Wire<N>, CAP>`，避免 C 数组和隐藏的软件容器协议。
- checkpoint 是需要恢复的状态副本，应存入 Register 数组，不应伪装成 Wire。
- IMEM/DMEM 的大块 Memory 是模型外部存储，不应机械地变成逐字节 Register 并参与同步。

## 9. `dark::CPU`：所有权与运行

`dark::CPU` 保存模块调用列表，并可选择拥有模块：

```cpp
dark::CPU cpu;
Counter counter;

cpu.add_module(&counter); // 非 owning
cpu.run_once();
```

裸指针重载不接管所有权，模块必须比 `cpu` 中保存的指针活得更久。也可交出 `unique_ptr`：

```cpp
auto owned = std::make_unique<Counter>();
cpu.add_module(owned);    // 当前 API 会 move，调用后 owned 为空
```

运行 API 的准确语义如下：

- `run_once()`：`cycles` 加一，按注册顺序调用全部 `work()`，然后同步全部模块。
- `run_once_shuffle()`：每拍随机排列 `work()` 调用顺序，然后同步；用于暴露错误的软件顺序依赖。
- `run(max_cycles, shuffle)`：反复选择上述一种单拍函数，直到总 `cycles` 达到 `max_cycles`。
  `max_cycles == 0` 表示不设上限。

通用 `dark::CPU::run()` 没有 finish predicate，也不会识别 HALT。当前模拟器外层的 `::CPU::run(bool)`
自行在每拍开始采样 HALT/排空状态，再调用 `dcpu.run_once()` 或 `run_once_shuffle()`；这是应用逻辑，
不是框架 `run()` 的隐藏功能。

正确设计中，所有跨模块读都来自 Register old view 或当拍组合 Wire，模块 `work()` 顺序不应影响
结果。`run_once_shuffle()` 是检查这一约束的工具，不是修复顺序依赖的机制。

## 10. RTL 数据通路约束

数据通路代码禁止直接使用宿主 `*`、`/`、`%`。允许的基本运算是加减、移位、按位运算、比较、
位选取和拼接。

- `Bit`/`Register`/`Wire` 参与的 `operator*`、`operator/` 已在框架中删除，误用会编译失败。
- 普通 C++ 整数仍可能让这些运算通过编译，但在 `src/` 数据通路中同样违规。
- 乘 2 的幂用左移，除 2 的幂用右移，2 的幂取模用掩码并配二次幂 `static_assert`。
- 真乘法只能由 MUL 的 Booth + CSA 数据通路产生；真除法和余数只能由 DIV 的 SRT 递推产生。
- host-only 输入解析、结束统计、编译期常量和明确包在断言条件中的参考模型不属于综合数据通路，
  但应保持清晰标注。

不要因为代码是 C++ 就假设编译器会替你保持目标 RTL 结构。具体审计结论见根级
[不可综合循环审计](../../docs/non-synthesizable-loops.md)。

## 11. 常见错误速查

| 现象 | 原因与处理 |
| --- | --- |
| 位宽不匹配编译失败 | 两个框架位类型必须同宽；明确切片、扩展或转换，不要靠隐式猜测 |
| `Register is double assigned` | 同拍存在两个写口；合并分支并在一个位置写一次，包含“同值双写” |
| `Wire is assigned twice` | 把接线放进了 `work()` 或重复调用 `wire_output()`；接线只做一次 |
| `Empty wire is called` | 模块实例自己的 Input/Output 未接；检查是否误接到独立 Input 对象 |
| Wire 下一拍仍是旧值 | Wire 没有位于注册模块中，因而缓存未被 `sync()` 清除 |
| lambda 报复制构造已删除 | `Register`/`Wire` 被按值返回或捕获；改用引用、`wire = reg` 或返回 `+reg` |
| 写寄存器后立即读取还是旧值 | 这是正确的 old/new 语义；新值只在周期末 `sync()` 后可见 |
| `a <= cond ? x : y` 编译异常 | 运算符优先级错误，必须写 `a <= (cond ? x : y)` |
| 随机模块顺序结果变化 | 存在 plain mutable 跨模块状态、未同步缓存或拍内先写后读依赖 |
| 组合求值递归/栈溢出 | Wire 连接形成组合环；用 Register 切断反馈路径 |
| 聚合同步编译失败 | 使用了 C 数组、非聚合成员或超过 64 个直接成员；改为 `std::array`/嵌套聚合 |
| Release 正常、`_DEBUG` 失败 | Release 隐藏了双写、重复接线或越界；应修正模型，不要关闭检查规避 |

另外遵守以下仓库约定：

- `work()` 按 RTL `always_ff` 风格组织，避免提前 `return` 跳过本拍其他状态更新，使用明确的
  `if/else` 或 enable 条件。
- 不复制、移动、`memcpy` 或 `memset` 含 Register/Wire 的模块。
- 循环接线时按值捕获索引，例如 `[this, i]`，不要捕获循环变量引用。
- Wire lambda 内先做 valid/ready 门控，再访问可能越界或会抛错的载荷。
- 裸指针注册模块时保证生命周期；`unique_ptr` 注册后不要继续使用已被 move 的指针。

## 12. VS Code IntelliSense

不要手写一套容易过期的 `includePath` 和宏。让 C/C++ 扩展读取 CMake 生成的
`compile_commands.json`：

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

本目录已有：

- `.vscode/settings.json`：CMake Configure 时自动加入 `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`。
- `.vscode/c_cpp_properties.json`：指向 `${workspaceFolder}/build/compile_commands.json`。

在 VS Code 中打开包含本目录 `CMakeLists.txt` 的 `RISC-V-Simulator-Template/`，不要只打开
`src/`。安装 C/C++ 与 CMake Tools，选择和命令行构建相同的 compiler kit，然后执行
`CMake: Configure`。若改用其他 build 目录，同步修改 `compileCommands` 路径。

如果编译数据库由 WSL 编译器生成并包含 `/mnt/...` 路径，应使用 VS Code Remote WSL 打开目录，
并把扩展安装到 WSL 侧。配置更新后仍报红时，执行 `C/C++: Reset IntelliSense Database` 或
`Developer: Reload Window`。
