# 数据通路循环的综合策略与当前审计

> 范围：主树 `src/` 与模板树 `RISC-V-Simulator-Template/src/` 的循环形状。模板框架头
> `RISC-V-Simulator-Template/include/` 的循环不在本文口径内，其性质与边界另见 §4 末尾。
> 本文是源码级策略与当前问题清单，不是综合认证。后端架构与多周期单元语义以
> [`backend.md`](backend.md) 为 SSOT，尤其是 §4.4 的 SRT radix-4 DIV。

## 1. 证据口径

| 证据 | 能说明什么 | 不能说明什么 |
| --- | --- | --- |
| 行为回归（x10、clock、断言） | 改写前后功能与周期行为是否一致 | 不能证明工具接受源码，也不能证明面积、端口或时序符合预期 |
| 宿主编译器输出（如 GCC `-O2 -S`、回边计数） | 常量传播和展开是否对该版本宿主编译器可见，可作为发现运行期边界的辅助证据 | 不是 HLS/RTL 综合结果；无回边不等于可综合，有回边也不代表目标综合器一定不能展开 |
| 目标转换/综合报告与网表 | 工具是否接受硬件顶层，以及循环展开、FSM、存储器、端口、资源和时序的实际结果 | 仍需行为或形式等价验证确认功能 |

本轮核对（2026-09-20）以**两树工作树源码**为准，包含尚未提交的改动，不绑定某一次 commit。
核对方式是正则枚举全部 `for`/`while`/`do`，按边界表达式归类（字面量、容量常量/模板参数、
`.size()`、运行期标识符），再逐项人工确认归类结果，并单独核对 `break`/`continue`/循环内
`return`。计数结果：主树 `src/` **102** 个循环，模板树 `src/` **141** 个循环；两树
`do { } while` 均为 **0** 个。

本文没有附目标综合工具的报告。因此，下文的“已完成”只表示源码已改成预期的硬件形状并通过现有行为验证，不能写成“已经由综合证明”。真正的综合结论必须来自选定工具、顶层、约束和版本下可复现的报告。

## 2. 当前策略

### 2.1 固定边界的组合迭代

同一周期内完成的扫描、归约、字节拼接或多路选择，循环边界必须是字面量、`constexpr`、模板参数或固定容量 `std::array::size()`。运行期信号只能控制 lane 的有效位或写使能，不能控制 trip count。

```cpp
for (int i = 0; i < CAP; ++i) {
  if (i < activeCount)
    consumeLane(i);
}
```

这段源码表达的是 `CAP` 路固定组合网络，而不是“执行 `activeCount` 次”的可变循环。固定数组的动态索引表达 mux/decoder，本身不是问题。固定边界也不等于成本可忽略：大容量嵌套扫描可能形成不可接受的比较器树、扇出或关键路径，必须以综合报告确认展开方式和代价。

判定细节：`std::array::size()` 是 `constexpr`，即使容器本身由运行期下标选出（如主树 `DCache::AllocateLine` / `PrRd` / `PrWr` 的 `cacheSets[set_idx].lines.size()`），trip count 仍是编译期常量，不应误判为运行期边界。反之，模板树已统一改用 `NUM_OF_WAYS` 这类容量常量，两树在这一处只是写法差异。

不要用 GCC 是否主动 unroll 作为判据。宿主编译器保留一个常量边界回边，只是宿主代码生成选择；目标硬件是否展开、是否意外生成控制器，要看目标综合工具的报告。

### 2.2 有界多周期 FSM

当第 `k+1` 次计算依赖第 `k` 次结果，或实际迭代次数由运行期数据决定时，不应强行做成单周期循环。应显式保存状态、固定宽度计数器和 valid/state，每拍执行一次迭代，完成后转入下一状态。

SRT DIV 是当前范本：商位递推由 `loopTimes` 和阶段 valid 驱动，每拍复用一份 QDS/CSA 数据通路。它是有界多周期 FSM，不是不可综合的宿主 `while`。两树的形态一致：主树 `DIV::tick` 按 `fullAdderValid → loopValid → prepareValid → dispatch` 的固定优先级调用 `loop()`/`prepare()`/`calculateResult()`，模板树在 `work()` 里以同一优先级写 `loopTimes <= loopTimes - 1` 与 `loopValid`/`fullAdderValid`；全树 `DIV.cpp` 没有任何 C++ 循环。算法与延迟定义见 [`backend.md`](backend.md) §4.4。

同类还有存储侧的多周期单元，同样以“剩余拍数 + busy 标志”表达，而不是循环：

- 主树 `DMEM::tick` 的 `readBusy`/`writeBusy` 配合 `remainCycle--`（`MEM_LATENCY`），读回与写入各是独立的两段状态。
- 主树 `DCache::tick` 的 `Phase::READY / Phase::WAIT` 两段式 FSM（模板树为 `DCACHE_WAIT` + `busy`）。
- 主树 `IMEM::tick`、`ICache::tick` 的请求队列与 `remainCycle`。

这些单元用“常量上界的计数器 + 阶段标志”换取单份数据通路的复用，是 §2.1 的固定边界网络之外唯一被认可的可变次数表达方式。

### 2.3 仿真外壳

以下循环属于宿主仿真，不应进入硬件顶层：

- `Memory::load_ins()` 解析标准输入中的 RV32IM 测试镜像（两树一致，`while (std::cin.get(c))`）。
- `debug::parseVerbose()`（`src/include/util.hpp`）解析 `VERBOSE` 环境变量并驱动宿主日志（`while (true)` + `strchr` 切分）。
- 主树 `CPU::run()` / 模板树 `CPU::run(bool)` 循环调用每周期仿真入口，直到测试程序停机；结束后的 clock/IPC/branch 报告块也是 host-only。
- 模板树 `dark::CPU::run_once()` / `run_once_shuffle()`（`RISC-V-Simulator-Template/include/cpu.h`）按模块列表遍历调用 `work()`，其中 `run_once_shuffle()` 使用 `std::shuffle` + `std::default_random_engine`；`sync_all()` 同样是模块列表遍历。
- 框架的 `synchronize.h`（`sync_member` 的 range-for）与 `misc.h`（`std::array` 逐元素比较）是宿主反射/同步辅助。
- 主树 `BPU::dumpBpMiss()` 的 top-16 选择（`VERBOSE=bpmiss` 下的报告代码，嵌套 `BTB_CAP` 扫描）。

这些代码当前没有 `#ifndef SYNTHESIS`、`synthesis off` 等已实现的源码隔离，不能把这种隔离描述成现状。两树存在的条件编译边界只有 `DCache` 内 `#ifdef _DEBUG` 包住的断言块（主树在 `tick` 里做“命中值与实时 DMEM 对照”，模板树在 `PrRd`/`PrWr` 路径上做同类检查），它们是调试开关，不是综合边界；`data/testcases_ipc/common/*.h` 与 `build/` 里的 `#ifdef` 属于测试框架与 CMake 探针，与硬件无关。综合工程必须通过硬件顶层和源文件选择排除上述循环，或以后真实实现并验证明确的仿真/综合边界。在完成前，“硬件数据通路可转换”不等于“把整个仓库交给综合器即可”。

### 2.4 固定边界内的数据相关早退

固定边界只保证 trip count 的**上界**是常量，不保证每次都跑满。以下三种写法令实际执行次数依赖数据，需要与 §2.1 分开判断：

| 形状 | 典型写法 | 现有落点 |
| --- | --- | --- |
| 前置守卫 | `if (!cond) continue;` | `LQ`/`SQ` 的环扫描与转发条件、两树 `FlushArbiter` 的 compaction 与插入定位 |
| 首个匹配/首个空槽后退出 | `return;` 或 `break;` | `AGU`/`ALU`/`BRU` 的 `push()` 落位、`MUL::remove()`、`MUL::calculateMulRes()`、`LQ::applyStoreForward()` |
| 谓词早返 | `if (x) return false;` | `AGU`/`ALU`/`BRU`/`MUL` 的 `isFull()`/`isEmpty()` |

处理方式，按代价从低到高：

1. **`continue` / 前置守卫保留原样**。它等价于 enable 门控，不改变 trip count，只减少无意义的组合活动，无需改写。
2. **`return` / `break` 做“首个空槽、首个匹配”时，改成 `found` 标志 + 全遍历 + 只写命中槽**。语义上这是一条固定遍历加优先编码，但 `break` 把“谁被选中”交给了宿主语言的逐次执行；改写成标志后，固定边界与选择逻辑都在源码里显式可见。模板树 `MUL::work()` 的空槽扫描已经是这个形状（`filled` + `found`，无 `break`），主树 `MUL::calculateMulRes()` 仍是 `break`——同一硬件的两种写法并存，是判断该项是否完成的直接对照。
3. **谓词早返（`isFull`/`isEmpty`）可以保留**，但要清楚这是宿主语言的短路求值写法，真实硬件是固定遍历的归约，其代价必须由综合报告给出。

限制：早退不改变“每拍每端口至多一次写”的纪律，也不引入运行期 trip count 常量，所以不能只凭“边界是常量”就判定它已可综合。它是否被展开成固定网络，只能由综合报告确认。

### 2.5 数据通路禁用形状

- 禁止以运行期计数、输入长度、容器当前长度或算法收敛条件作为组合数据通路的循环边界。
- 禁止在一次 `work()`/`tick()` 中用 `while` 或数据相关 `break` 隐式完成可变次数的迭代。
- 有固定硬上界时，改成固定边界加 valid/enable；需要跨拍复用时，改成显式 FSM。
- “底层数组容量固定”不够；实际循环条件也必须固定。`Plan::tab[32]` 配 `q < nTab` 仍是运行期边界（该容器早期为 `tab[64]`，缩容并不改变问题性质）。

## 3. 当前审计状态

| 项目 | 状态 | 当前硬件形状 |
| --- | --- | --- |
| 字节装配与 store 合并 | 已完成 | 主树 DMEM/DCache 与模板树 DCache 使用固定四 lane；`n`/`n_bytes` 只控制 lane 使能。1B/2B 符号扩展使用显式掩码（主树在 `DCache::PrRd` 与 `DMEM::load_n_bytes` 内联，模板树集中在 `DCache::extractValue`） |
| FlushArbiter 有序插入 | 已完成 | 两树均以 `FLUSHARBITER_CAP`（=4）固定扫描和固定后移网络实现；`scanning`、`w`、`pos` 只参与选择和写使能（主树在 `FlushArbiter::receive`，模板树在匿名命名空间的 `insertPlain`；模板树的 `selectOldest()` 供四个输出 Wire 共用） |
| ROB ready 去重 | 已完成 | 模板树使用零初始化的逐槽 `readyWrite[]/readyData[]` 写意图幂等归约，再固定遍历 ROB 槽，每槽至多一次寄存器写；已移除 `seen[]/nSeen` 可变长度查找。主树仍是逐次 `setROBCommitReady()` 的幂等写，遍历边界同样固定（`ROB_CAP` 与 `MEMQ_SCAN_WINDOW`） |
| Tournament 方向预测 | 已完成 | 两树 local/global/selector 均为固定 256 项表（`BHT_CAP`/`SELECTOR_CAP`），16-bit GHR 直接参与 gshare 索引；折叠历史的重建/增量循环已随旧 TAGE 方案一并消失（该循环曾记作审计项 C-6），源码中已无 TAGE 折叠视图 |
| SRT DIV 递推 | 设计正确，无需改写为组合展开 | 两树均以固定宽度计数器和阶段 valid 实现多周期 FSM，`DIV.cpp` 内无 C++ 循环 |
| MUL 空槽选择 | 两树形状不一致 | 模板树 `MUL::work()` 已是 `found` 标志全遍历；主树 `MUL::calculateMulRes()` 仍以 `break` 在首个空槽退出 |
| LQ store-forward 环扫描 | **仍需处理** | 两树均以 `if (!isActive(cur)) break;` 在环上首个空槽早退（主树 `LQ::applyStoreForward`，模板树 `work()` 的两处转发循环） |
| 队列扫描与早退 | 已完成（形态可接受） | `AGU`/`ALU`/`BRU`/`MUL` 的 `isFull()`/`isEmpty()`/`remove()` 与 `RSUnit::tryAlloc*` 均为固定容量遍历；早退只影响宿主的短路求值，不改变端口写纪律，展开方式待综合确认 |
| 宿主侧统计与转储 | 已完成（host-only） | 主树 `BPU::dumpBpMiss()` 的报告循环、模板树 `dark::CPU::run_once()` 的模块遍历；按 §2.3 必须排除在硬件顶层之外 |
| 模板 BPU `Plan::nTab` | **仍需处理** | merge、查重和 apply 仍有 `q < src.nTab`、`m < merged.nTab` 等运行期循环边界 |

按 trip count 归类：模板树 `src/` 的 141 个循环里，以运行期量作边界的只有 `Plan` 相关的 3 处（merge 查重、merge 写入、apply），其余全部是容量常量、字面量或常量移位表达式；主树 `src/` 的 102 个循环里，以运行期量作边界的只有 §2.3 的 3 个宿主循环，数据通路侧没有任何以运行期量作 trip count 的循环。上表中 MUL/LQ 两行属于另一类问题——边界是常量，但循环体内存在数据相关早退（§2.4），两者不要合并计数。

当前已知的数据通路循环问题集中在模板 BPU 的 `Plan::nTab`。Tournament 改写已将容器收紧为
`Plan::tab[32]`，但 `nTab` 仍由当拍训练行为决定；嵌套的压缩列表扫描表达的是软件式可变
长度集合，不是明确的固定端口网络。注意主树 BPU 是 `comb()`/`tick()` 参考实现、根本没有 `Plan`
结构，因此该项只在模板树成立，不要在主树里找对应位置。

处理该项时应优先按资源拆成固定写意图、valid 位和明确优先级；最低要求是所有候选槽都按固定容量遍历，以 valid 作为条件。不能只把 `32` 换成另一个常量而保留 `q < nTab`。同时必须保持现有语义：

- 合并优先级 `fi > cdb > bru`。
- 同一物理 Register 每拍最多一次赋值。
- BTB 更新的既定覆盖顺序和双训练口共享周期初快照不变。
- 固定网络的面积与关键路径可接受；若 32×32 查重过大，应按表资源直接仲裁，而不是机械铺开平方级比较器。

## 4. 审计与验收流程

1. 先确定代码属于组合数据通路、多周期状态机还是仿真外壳。
2. 检查所有 `for`/`while`/`do` 的退出条件，并继续检查 lambda 和 helper 内部；固定容量数组不自动保证使用它的循环固定。逐条记录边界表达式的种类：字面量、容量常量、模板参数、`.size()`、运行期标识符。
3. 在同一批循环里单独过一遍 `break`/`continue`/循环内 `return`：`continue` 属门控，`break`/`return` 属早退。早退不改变 trip count 上界，但会改变实际执行次数，按 §2.4 的三种形状分别归类，不要与运行期边界混为一谈。
4. 组合逻辑采用固定边界加 enable，多周期算法采用寄存器、计数器和 FSM，宿主 I/O 留在硬件顶层之外。
5. 行为等价改写后跑双树 x10、断言和逐例 clock 对比。根目录 `test.sh` 会将 clock 与 `benchmarks.md` 的 `cycles` 严格比较，双树还需逐例确认结果一致。
6. 在目标转换/综合工具上检查 unsupported construct、循环展开/FSM 推断、寄存器和 RAM 推断、端口冲突、资源、关键路径与时序约束；保存工具版本和报告，才可把“源码意图”升级为“综合证明”。

本审计只覆盖循环及其直接数据结构。异常（如两树 `FlushArbiter` 重载时 `throw std::runtime_error`）、宿主 I/O（`std::cin`/`std::getenv`）、动态内存（`Memory` 的 `new` 与 DCache 的堆上 `cacheSets`）、库调用（`std::shuffle`、`std::default_random_engine`）、断言（`DCache`/`MUL`/`ROB` 的 `assert`）和顶层接口仍需在完整综合审计中分别确认。

模板框架头 `RISC-V-Simulator-Template/include/` 不在本文的 `src/` 口径内，但已核对：`cpu.h` 里存在两处宿主侧可变边界循环——`dark::CPU::run()` 的周期循环，以及 `run_once()`/`run_once_shuffle()`/`sync_all()` 对 `std::vector<ModuleBase *>` 的逐模块遍历（含 `std::shuffle` 引入的非确定性顺序）；`synchronize.h`、`misc.h` 的 range-for 都作用在 `std::array` 上，边界仍是编译期常量。前者与 §2.3 同属仿真外壳，必须与 §2.3 的循环一起排除在硬件顶层之外。
