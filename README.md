# RV32IM Out-of-Order Processor Simulator

周期语义级（cycle-accurate）的 RISC-V **乱序执行（out-of-order）处理器仿真器**。
以 C++20 对 Tomasulo 风格微架构（重命名 / 物理寄存器堆 / 重排序缓冲 / 保留站）做
逐周期建模，模型按**可综合硬件的纪律**书写：模块每周期只写自己的状态、跨模块信息
一律走"周期初快照 + 组合总线"——19 个流水级以**任意顺序**调用均产生逐位相同的
结果与时钟数。既可作为后续 RTL 移植的参考模型，也可用于微架构方案的量化对比。

| | |
|---|---|
| **架构** | Tomasulo 乱序执行 · 按序提交（取指/提交有序，执行/写回/访存乱序） |
| **ISA** | RV32I 全集 + RV32M 乘法族（`mul/mulh/mulhu/mulhsu`，硬件 Booth 乘法器） |
| **语义** | 逐周期双相模型：`comb()` 组合求值 + `tick()` 沿采样，19 级顺序无关 |
| **实现** | 单体 C++20，无外部依赖；Linux / WSL / MSYS 均可构建 |
| **验证** | 行为回归（golden）+ 19 级重排一致性 + MUL 直驱对拍 + RV32M 双臂 A/B |

---

## 目录

- [1. 项目概览](#1-项目概览)
- [2. 系统架构](#2-系统架构)
- [3. 仓库结构](#3-仓库结构)
- [4. 构建与运行](#4-构建与运行)
- [5. 性能数据](#5-性能数据)
- [6. 验证与回归](#6-验证与回归)
- [7. 参考资料](#7-参考资料)
- [8. 开发状态与路线图](#8-开发状态与路线图)

---

## 1. 项目概览

### 1.1 设计目标

1. **逐周期、可重排的硬件语义**。每个硬件模块有且仅有一个写口，只写自己的状态；
   跨模块总线全部在组合阶段（快照边）求值。由此，流水级调用顺序可任意交换，
   `reorder_test` 把这一性质作为硬约束回归（含 DMEM 镜像与 DCache 行阵指纹）。
2. **面向 RTL 移植的参考模型**。`Phase` 状态机、双口读/写通道、单写端口、寄存器级
   流水（Booth 乘法器）、回写总线冲突等均以 Verilog 习惯的 C++ 呈现，并有独立的
   `RISC-V-Simulator-Template/` 重建线把同一架构逐步改写为可综合风格。
3. **可量化对比的研究平台**。三总线写回 vs 单总线、DCache 命中自答、M 扩展硬件
   乘法等方案的收益都落在同一套回归体系上，用 clock 逐位对拍作结论依据。

### 1.2 特性一览

| 特性 | 说明 |
|------|------|
| 乱序执行核心 | ROB 64 / PRF 128 / RAT；issue 侧完成 rename，commit 按序释放 |
| 三路结果总线 | `aluCDB` / `lqCDB` / `mulCDB`，各源独立、无跨单元仲裁 |
| 指令前端 | 8 KB 直映 ICache + 50 周期主存；FQ/IQ 双缓冲 + 预译码 + TAGE 预测 |
| 访存系统 | LQ/SQ（store→load 转发、MDP 违例）+ 64 KB 4 路回写 DCache + 双口 DMEM |
| 专用乘法 | radix-4 Booth + CSA 进位保存，3 级寄存器流水 |
| 精确异常语义 | 误预测与记忆违例统一经 `FlushArbiter` 排队，按最老优先整窗恢复 |

---

## 2. 系统架构

### 2.1 数据通路总览

```
                    ┌──────────── 前端 Front-End ────────────┐
 FetchDecision ───► │ FetchUnit(PC/halt) → ICache → IMEM     │
 (TAGE+BTB+RAS       │      │  8KB 直映      50 周期主存回填   │
  预测 + 预译码)     │      ▼                                  │
                    │ FetchQueue(FQ,8) ──► DecodeUnit → IQ(16)│
                    └───────────────────┬────────────────────┘
                                        ▼
              ┌────────────── 重命名 / 发射 Issue ──────────────┐
              │ IssueArbiter：RAT rename + PRF(128) pop+LINK    │
              │              + ROB(64) push + RS/LQ/SQ 入队     │
              └──────────────┬──────────────────────────────────┘
                             ▼
   ┌───────────────────────────────────────────────────────────┐
   │ 保留站 RS ──派发──► 执行单元 ──► 结果总线（每单元各一根）    │
   │   Integer 8  ──► ALU / AGU / BRU                           │
   │   Multiply 4 ──► MUL（Booth 三级流水）                      │
   │   Load 4 / Store 4 ──► LQ / SQ ──► DCache ──► DMEM(双口)   │
   └──────────────────────────────┬────────────────────────────┘
                                  ▼
              ROB 按序提交；误预测 / 记忆违例 ──► FlushArbiter
              （RAT/PRF/GHR/RAS/队列 从 ROB 条目 checkpoint 恢复）
```

取指与提交保持程序序；执行、写回与访存完全乱序。访存延迟来自存储层次的硬件
延迟模型：L1 命中零延迟（ICache 当拍组包、DCache 命中 1 拍自答），缺失回填与
脏逐出统一走 **50 周期主存延迟**（IMEM/DMEM）。

### 2.2 周期模型与一致性纪律

每个周期两个阶段，对应硬件时钟语义：

1. **`comb()`（组合求值）**——全部模块 memcpy 进**快照**，在快照边计算跨模块
   组合总线：`FetchDecision`（取指决策 + GHR 移位标记）、三条结果总线候选（每源
   `build()` 取唯一队首并过 squash 门）、`DispatchArbiter` 派发、`IssueArbiter::build`
   （发射包：rename + 各队 push 载荷）、`MemArbiter` 访存准入（store 优先互斥）、
   store 就绪广播、DCache 应答线等。
2. **`tick()`（沿采样）**——19 个模块各自 tick。总线信号在 comb 中打包进对应
   `Input`；模块 tick **只读快照、只写自己（活体）状态**，跨模块写为零。

因此阶段调用顺序可任意交换（由 `reorder_test` 19 级乱序验证）；停机条件 = halt
已提交 且 FQ/IQ/ROB 全空，并做**访存 drain**：SQ 空、DCache 空闲、DMEM 读/写口
空闲——保证停机瞬间不存在"已提交但未写达缓存"的在途 store。

### 2.3 子系统划分

处理器分为**前端 / 后端 / 访存 / 缓存**四个子系统。README 只保留各子系统精简
简介（核心功能 + 关键规格）；每个子系统的详细设计独立成文于 `docs/` 下，读者
可按需深入。

#### 前端 Front-End — 取指 · 预译码 · 译码 · 分支预测

**功能**：由 BPU 预测下一个 PC，沿预测路径取指（L1I 命中当拍组包、缺失经 IMEM
回填），FQ 尾对 jal/jalr 做静态 call/ret **预译码**，随后按序译码进 IQ，把顺序
指令流交给后端。预测采用 TAGE（方向）+ BTB/Target Cache（目标）+ SARAS（返回
栈），误预测由后端按 checkpoint 整窗恢复。

**关键规格**：每周期至多取指/发射 1 条；FQ 8 / IQ 16；方向预测 T0 1024×2b +
LHT 512×12b + T1–T4 各 1024×8b tag（hist {6,12,24,48}）；BTB 256、Target
Cache 128（BHR 256×8b）、RAS 8 + SARAS 16；ckptId 池 64（≥ ROB 64）。

→ 详细设计见 [`docs/frontend.md`](docs/frontend.md)

#### 后端 Back-End — 发射 · 乱序执行 · 写回 · 提交 · 恢复

**功能**：从 IQ 头发射（RAT rename + PRF 分配 + ROB/RS/LQ/SQ 入队）；保留站
操作数就绪即乱序派发到 ALU/MUL/AGU/BRU；结果经三根独立总线并行写回 PRF/ROB；
ROB 头按序提交。误预测与记忆违例统一进 `FlushArbiter` 排队（最老优先），各模块
从 ROB 条目 checkpoint 恢复。

**关键规格**：ROB 64 / PRF 128 / RAT 32；保留站 Integer 8 · Multiply 4 · Load
4 · StoreAddr 4 · StoreValue 4 · Branch 4；每周期至多 1 次发射；执行队列各 4 槽、
每源每周期 1 个队首结果；结果总线 3 根（`aluCDB`/`lqCDB`/`mulCDB`，无仲裁）；
MUL = Booth radix-4 + CSA 三级流水（`mul/mulh/mulhu/mulhsu`）；FlushArbiter 4 项。

→ 详细设计见 [`docs/backend.md`](docs/backend.md)

#### 访存 Memory-Access — LQ/SQ · 转发 · 违例 · 准入

**功能**：Load/Store 发射即入 LQ/SQ，地址与数据两段就绪；store 地址/数据就绪以
组合事件广播，命中窗口内直接**转发**给 load；地址更老的 store 发现更年轻 load
越界执行即触发 **MDP 违例**整窗恢复；每周期经 `MemArbiter` 准入**一个**访存
请求（store 优先）交给 DCache，load 完成经 `lqCDB` 写回。

**关键规格**：LQ 16 / SQ 16；转发基于 SQ 快照组合求值（携带"同址更老/地址未知
store"存在性）；DCache busy 时停发（store 已弹出不反悔）；store 在提交点落缓存。

→ 详细设计见 [`docs/memory.md`](docs/memory.md)

#### 缓存 Cache Hierarchy — L1I / L1D / 片上主存

**功能**：两级 + 主存存储层次：ICache（L1I，直映）与 DCache（L1D，4 路写回 +
写分配）提供零/低命中延迟；两缓存缺失回填与脏逐出写回统一走 IMEM/DMEM **50
周期**行级读写（DMEM 读/写双口并行在飞）。DCache 以 READY/WAIT 两段 FSM 服务，
命中 load 1 拍自答。

**关键规格**：ICache 8 KB（512×16 B 直映）；DCache 64 KB（1024 组 × 4 路 ×
16 B，tree-PLRU）；IMEM/DMEM 各 128 KB；主存延迟固定 50 周期；行阵/存储阵列
不进快照（重排一致性另以指纹校验）。

→ 详细设计见 [`docs/cache.md`](docs/cache.md)

### 2.4 指令集支持

- **RV32I** 全集（x0 恒零语义正确）；`ecall/ebreak` 不在集内，程序以**停机字**
  约定结束（见 [§4.2](#42-运行与终止约定)）。
- **RV32M 乘法族**：`mul/mulh/mulhu/mulhsu` 内联执行（Booth + CSA 三级流水，
  见 [`docs/backend.md`](docs/backend.md)）；收益用同工具链双臂 A/B 语料量化
  （见 [§6.4](#64-rv32m-扩展评测)）。
- **DIV/REM 族**：译码表按 funct3 预留完整空间，单元未实现——对 funct3 4..7
  显式停发（宁可 stall 不静默错算），程序中 `/` `%` 走编译器软例程。

---

## 3. 仓库结构

### 3.1 目录树

```
RISC-V-Tomasulo-CPU-Simulator/
├── CMakeLists.txt                    # 根构建（CMake），产物 ./code
├── README.md                         # 本文档（总览 + 子系统精简简介）
├── issue.pdf                         # 题目与评测说明（ISA 约束 / 口径 / 数据来源）
├── AGENTS.md                         # 开发账本：架构决策 / 模块归属 / 验证流程
├── test.sh                           # 行为回归脚本（x10 vs golden + 分支/时钟统计）
├── code                              # 构建产物：Release 可执行（WSL/Linux ELF）
│
├── src/                              # 模拟器源码（comb()/tick() 逐周期快照双缓冲）
│   ├── main/
│   │   └── main.cpp                  # 入口：读镜像 → CPU::run()
│   ├── CPU/
│   │   └── CPU.cpp                   # comb() 组合求值 + 19 级 tick 调度 + 概要统计
│   │
│   ├── include/                      # 全部头文件（25 个：模块声明 / 公共类型 / 常量）
│   │   ├── common.hpp                # 容量常量 + 公共结构（SquashInfo / MemRequest / Uop / DMEMRequest…）
│   │   ├── CPU.hpp                   # systemState（活体模块）+ CPU（快照成员 + Input 接线）
│   │   ├── Memory.hpp                # 存储基类：128KB 字节阵列 + 镜像流式解析
│   │   ├── util.hpp                  # VERBOSE 主题调试开关
│   │   │
│   │   ├── FetchUnit.hpp             # 前端 PC 寄存器 + halt 闩锁
│   │   ├── InstructBuffer.hpp        # 取指队列 FQ（8 项 + 预译码 lastPush 缓存）
│   │   ├── Decoder.hpp               # 译码器 + 指令队列 IQ（UopQueue，16 项）
│   │   ├── ICache.hpp                # 8KB 直接映射指令缓存（512×16B）
│   │   ├── IMEM.hpp                  # 指令内存：50 周期主存延迟 + 整行突发返回
│   │   │
│   │   ├── RS.hpp                    # 保留站族（Integer/Multiply/Load/StoreAddr/StoreValue/Branch）
│   │   ├── PRF.hpp                   # 物理寄存器堆（128：rename/freeList/完成写口）
│   │   ├── RAT.hpp                   # 架构寄存器→物理寄存器映射表
│   │   ├── ROB.hpp                   # 重排序缓冲（64：按序提交 / checkpoint 宿主）
│   │   │
│   │   ├── ALU.hpp                   # 算术逻辑执行单元（含 JALR 控制类载荷）
│   │   ├── MUL.hpp                   # M 扩展乘法单元（Booth→CSA→加法，3 级流水）
│   │   ├── AGU.hpp                   # load/store 地址计算单元
│   │   ├── BRU.hpp                   # 条件分支执行单元
│   │   │
│   │   ├── LQ.hpp                    # 加载队列（转发接收 / 违例报告 / 完成总线）
│   │   ├── SQ.hpp                    # 存储队列（转发源 / canDispatchLoad 把关）
│   │   ├── DCache.hpp                # 数据缓存 64KB/4 路（READY/WAIT 两段 FSM）
│   │   ├── DMEM.hpp                  # 数据内存：读/写双口，行级请求
│   │   │
│   │   ├── BPU.hpp                   # 分支预测器（TAGE + BTB/TargetCache + SARAS）
│   │   ├── StaticArbiter.hpp         # 无状态仲裁器族（Dispatch/Mem/Issue + 发射包）
│   │   ├── DynamicArbiter.hpp        # FlushArbiter：squash 请求队列（四阶段检测）
│   │   └── CDB.hpp                   # 三路结果总线载荷（aluCDB/lqCDB/mulCDB）+ build 工厂
│   │
│   ├── FetchUnit/  InstructBuffer/   # 各模块 tick 实现（与 include/*.hpp 一一对应）
│   ├── Decoder/  ICache/  IMEM/
│   ├── RS/  PRF/  RAT/  ROB/
│   ├── ALU/  MUL/  AGU/  BRU/
│   ├── LQ/  SQ/  DCache/  DMEM/
│   ├── BPU/  StaticArbiter/  DynamicArbiter/  CDB/
│   └── ...
│
├── data/                            # 镜像 / 语料 / 回归基线
│   ├── sample/                      # 最小可运行示例（sample.{c,data,dump}）
│   ├── testcases/                   # 18 个基准（.data 镜像 + .dump 反汇编；多含 .c 源码）
│   │   └── io.inc                   # 18 个 .c 共享的校验头（x10 语义来源，勿删）
│   ├── testcases_rv32im/            # RV32M 扩展 A/B 双臂语料（M=rv32i_zmmul / I=rv32i）
│   │   ├── M/                       # 4 用例：bulgarian / statement_test / pi / multiarray
│   │   ├── I/                       # 同用例 rv32i（软乘 __mulsi3）对照臂
│   │   └── test_m.sh                # 双臂对拍脚本（x10 / 跨臂 / 收益）
│   └── golden/                      # 回归基线：18 个 {case}.golden，每行 <x10> <clock>
│
├── test/                            # 一致性 / 单元测试（独立构建，不影响根目标）
│   ├── CMakeLists.txt
│   ├── reorder_test.cpp             # 19 级任意顺序一致性测试（含内存/缓存指纹）
│   ├── mul_unit_test.cpp            # MUL 单元直驱自测（Booth/CSA/结果级对拍）
│   ├── test_reorder.sh              # 乱序一致性运行脚本
│   └── patch_mul.py                 # （已废弃）M 扩展补丁法 → 被 rv32im 重编译管线取代
│
├── docs/                            # 设计文档（子系统详析，见 §2.3）
│   ├── benchmarks.md                # 性能画像与逐用例实测（cycles / 命中率 / 准确率）
│   ├── frontend.md                  # 前端：取指 / 预译码 / 译码 / 分支预测
│   ├── backend.md                   # 后端：发射 / 乱序执行 / 写回 / 提交 / squash 恢复
│   ├── memory.md                    # 访存：LQ/SQ / 转发 / MDP / 请求准入
│   └── cache.md                     # 缓存与存储层次：L1I / L1D / 主存接口
│
├── ppt/                             # 讲义（lec1.pdf … lec4.pdf）
├── reference/                       # 参考资料（RISC-V 规范 / 指令卡 / 教材 PDF）
│
├── RISC-V-Simulator-Template/       # RTL 化重建线（Register/Wire 模块框架，见 §8）
│   ├── include/  src/  test/        # 模板头 + 逐模块重写的可综合风格模型
│   ├── data/  docs/  AGENTS.md
│   └── CMakeLists.txt  test.sh
└── build/                           # 构建缓存目录（cmake 产物）
```

> 命名口径：`src/include/*.hpp` 为模块声明，`src/<Module>/<Module>.cpp` 为实现；
> 主树为 comb()/tick()/memcpy 快照参考实现（文件布局与模板树同构，类为纯 C++，
> 未用 Register/Wire）。19 个流水级站名：rat·lq·sq·decode·agu·bru·bp·dmem·alu·
> rs·rob·prf·arb·imem·fq·icache·fetchunit·dcache·md。

### 3.2 模块清单

| 模块（头文件） | 职责 |
|------|------|
| `FetchUnit` | 前端 PC 寄存器 + halt 闩锁（haltFetched） |
| `ICache` | 8 KB 直映指令缓存；命中组包、缺失整行回填 |
| `IMEM` | 指令内存：50 周期主存延迟 + 整行突发返回（`LineReturn` 4 字总线） |
| `InstructBuffer`（FQ） | 取指队列（8 项，含预译码 `lastPush` 缓存） |
| `Decoder` / `DecodeUnit` | 译码器 + 指令队列 IQ（UopQueue，16 项） |
| `PRF` | 物理寄存器堆：循环序号自由表、完成写口、checkpoint 恢复 |
| `RAT` | 架构寄存器 → 物理寄存器映射 |
| `ROB` | 按序提交、checkpoint 快照宿主、squash 边界 |
| `RS` | 五类保留站（Integer/Multiply/Load/StoreAddr/StoreValue/Branch） |
| `ALU` | 算术/逻辑/移位 + JALR 目标（控制类载荷） |
| `MUL` | 专用乘法单元：Booth → CSA → 最终加（3 级流水） |
| `AGU` | load/store 地址计算（含队首 store 地址广播） |
| `BRU` | 条件分支执行（pcFrom/pcResult） |
| `LQ` | 加载队列：store→load 转发、违例报告、load 完成总线 |
| `SQ` | 存储队列：转发源、`canDispatchLoad` 把关 |
| `DCache` | 数据缓存：READY/WAIT FSM、命中 1 拍自答、脏逐出写回 |
| `DMEM` | 数据内存：读/写双口独立在飞、行级读写请求 |
| `BPU` | TAGE 方向 + BTB/TargetCache 目标 + SARAS，双口训练 |
| `StaticArbiter` | 无状态仲裁器族：Dispatch/MemArbiter/IssueArbiter + 发射包 |
| `DynamicArbiter` | `FlushArbiter`：squash 请求队列 + 检测/恢复 |
| `CDB` | 三路结果总线载荷的 build 工厂 |
| `Memory` | 字节存储基类（128 KB + 镜像流式解析） |
| `common.hpp` | 容量常量与公共结构（SquashInfo/MemRequest/Uop/…） |
| `util.hpp` | VERBOSE 主题调试 |

---

## 4. 构建与运行

### 4.1 构建

```bash
# CMake
cmake -S . -B build && cmake --build build

# 或直编（无 CMake 依赖）
g++ -std=c++20 -O2 -Isrc/include src/main/main.cpp src/CPU/CPU.cpp \
  src/Decoder/Decoder.cpp src/DMEM/DMEM.cpp src/DCache/DCache.cpp src/ROB/ROB.cpp \
  src/RS/RS.cpp src/ALU/ALU.cpp src/MUL/MUL.cpp src/AGU/AGU.cpp src/BRU/BRU.cpp \
  src/DynamicArbiter/DynamicArbiter.cpp src/StaticArbiter/StaticArbiter.cpp \
  src/CDB/CDB.cpp src/IMEM/IMEM.cpp src/ICache/ICache.cpp src/FetchUnit/FetchUnit.cpp \
  src/LQ/LQ.cpp src/SQ/SQ.cpp src/RAT/RAT.cpp src/InstructBuffer/InstructBuffer.cpp \
  src/BPU/BPU.cpp src/PRF/PRF.cpp -o code
```

> 调试版（双写断言 + clean 命中对照 DMEM）：追加 `-D_DEBUG`。
> 全量回归建议在 **WSL/Linux** 跑 Release（-O0 慢数十倍；Windows 进程启动开销会
> 显著拉长墙钟）。

### 4.2 运行与终止约定

程序从 stdin 读**镜像**（Verilog-hex 格式）：`@<hex>` 行切换基址，其后空格分隔的
字节依次写入连续地址：

```
@00000000
37 01 02 00 EF 10 00 04 13 05 F0 0F ...
@00001000
37 17 00 00 83 27 C7 06 ...
```

模拟器不含 OS：取到**停机字** `0x0ff00513`（镜像中"此后无代码"哨兵）即闩锁 halt，
后续不再取指；提交到 halt 后停机。进程退出值 = 停机时 `x10` 的低 8 位（0–255）。

```bash
./code < data/testcases/gcd.data                  # stdout：x10 低 8 位
VERBOSE=branch,clock ./code < data/testcases/gcd.data   # 统计走 stderr
```

`VERBOSE`（stderr，逗号分隔）主题：`issue` `exec` `wb` `commit` `lsq` `mem`
`clock` `branch` `prf` `mdp` `bpmiss` `icache` `cdb`，或 `all`。`branch` 输出
正确/总数与准确率，`clock` 输出总时钟，`icache`/`cdb`/`bpmiss` 输出命中率、
总线争用等概要。

---

## 5. 性能数据

> 各基准的逐项实测数据已外置到 [`docs/benchmarks.md`](docs/benchmarks.md)，
> 不再在本表重复维护。数据口径与文档一致：**主存延迟固定 50 周期、L1 命中
> 零延迟、8 KB 指令缓存 + 64 KB 数据缓存（写回 + 写分配）**；每个用例记录
> cycles、按控制流类型拆分的预测准确率（cond / jal / jalr / branch）以及
> I$/D$ 命中率。行为回归（退出码 / x10 对照 golden / 崩溃检测）仍由
> [`./test.sh`](#6-验证与回归) 执行。

要点（详见 `docs/benchmarks.md` 表注）：

- 小基准（naive/lvalue2 等）分支基数小，准确率波动属正常；大基准（basicopt1/
  hanoi/qsort/superloop/tak）均 ≥95%。
- pi 的剩余误预测集中在软件除法例程的数据相关分支（RV32I 无硬件除法），属历史
  窗口外熵墙；RAS 冷启动漏栈问题已由预译码修复。

---

## 6. 验证与回归

验证分三层：**行为回归**（x10/分支/clock/golden）、**重排一致性**（19 级任意
顺序逐位相同）、**单元级对拍**（MUL 直驱 + 扩展双臂 A/B）。

### 6.1 行为回归 — `test.sh`

以 `data/testcases/*.data` 为输入运行 `./code`，校验退出码（崩溃检测）与
`x10&0xFF`（对照 golden），汇总分支正确率与总时钟；任一 FAIL/CRASH 非 0 退出
（pi 排最后）。

```bash
./test.sh                 # 全量
./test.sh 'q*'            # 通配符过滤
BP_BIN=./code ./test.sh   # 指定二进制
```

### 6.2 重排一致性 — `test_reorder.sh`

`reorder_test` 以**任意顺序**调用 19 个流水级，要求各排列给出相同的
`x10&0xFF`、相同时钟，并额外对 **DMEM 最终镜像与 DCache 行阵做指纹比对**
（这两者刻意不在管线快照里，是全系统一致性的最强约束）。全排列 19! ≈ 1.2×10¹⁷
不现实，按单位点耗时自动分档：单次 <10 s 一律随机 100 组、>10 s 仅参考序 1 组
（如 pi）。

```bash
cd test
cmake -S . -B build && cmake --build build
./test_reorder.sh                 # 全部测试点
./test_reorder.sh 'gcd'           # 单点
./test_reorder.sh --count 1000    # 强制 N 组（调试）
```

`diff` 模式对两种显式顺序逐周期打印首个状态分叉点（cycle/字段/值），用于定位
重排不一致根源。

### 6.3 MUL 单元自测

直驱乘法器内部级（Booth 部分积 → CSA → 结果）与整单元，含 10 万组 LCG 随机
对拍（断言默认开启）。构建时把 `src/main/main.cpp` 换成 `test/mul_unit_test.cpp`：

```bash
./test/mul_unit_test 100000
```

### 6.4 RV32M 扩展评测

为量化 M 扩展（硬件乘法）收益，另建一套**同工具链、同链接布局**的双臂语料
`data/testcases_rv32im/`：`M` 臂 = `-march=rv32i_zmmul`（乘法内联为硬件 `mul`），
`I` 臂 = `-march=rv32i`（软例程 `__mulsi3`）。两臂仅乘法实现不同，clock 差即
**纯 M 扩展收益**。镜像经 `objcopy -O verilog` 生成，产出须为 **LF 行尾**
（CRLF 残留会被解析为字节 token，导致第二段起镜像错乱）。

| 检查项 | 说明 |
|------|------|
| 返回值正确性 | `x10&0xFF` 对照 golden 第一列（第二列为课程布局 clock，与自制镜像不同，不比） |
| 跨臂一致性 | 同语义两套乘法实现，M/I 结果不同必是 bug |
| 崩溃检测 | 退出码非 0 记 `CRASH` |
| 收益 | `Δclock(M/I)%` 与 speedup，附 `.dump` 静态 `mul` 条数 |

```bash
./data/testcases_rv32im/test_m.sh          # 全量（pi 双臂各需数分钟）
QUICK=1 ./data/testcases_rv32im/test_m.sh  # 跳过 pi
```

实测结果（2026-09-07 · WSL/Release，4/4 通过）：

| 用例 | 臂 | x10 | Clock | 分支正确/总数 | 准确率 | 耗时 | 静态 mul |
|------|:--:|:---:|------:|--------------:|-------:|-----:|:---:|
| bulgarian | M | 159 | 250,023 | 73,462/76,279 | 96.31% | 0.54s | 8 |
| bulgarian | I | 159 | 253,809 | 73,703/76,892 | 95.85% | 0.55s | 0 |
| statement_test | M | 50 | 1,040 | 108/166 | 65.06% | 0.03s | 3 |
| statement_test | I | 50 | 1,701 | 187/293 | 63.82% | 0.03s | 0 |
| pi | M | 137 | 111,687,574 | 32,342,091/37,031,603 | 87.34% | 216.28s | 1 |
| pi | I | 137 | 137,852,245 | 37,033,569/43,100,792 | 85.92% | 267.01s | 0 |

| 用例 | I 臂 Clock | M 臂 Clock | Δclock | speedup | 静态 mul |
|------|-----------:|-----------:|-------:|--------:|:---:|
| bulgarian | 253,809 | 250,023 | −1.49% | 1.015× | 8 |
| statement_test | 1,701 | 1,040 | **−38.86%** | **1.636×** | 3 |
| pi | 137,852,245 | 111,687,574 | **−18.98%** | **1.234×** | 1 |
| **合计** | **138,107,755** | **111,938,637** | **−18.95%** | **1.234×** | — |

- **收益与乘法动态占比正相关**：statement_test −38.86%（乘法集中、省掉整个
  `__mulsi3`）；bulgarian −1.49%（乘法只是内层循环一步）；pi 仅 1 条静态 `mul`
  但位于最热收敛循环 → −18.98%（约省 2600 万拍）。
- **分支准确率同步提升**（pi 85.92→87.34%、statement_test 63.82→65.06%）：
  `__mulsi3` 的移位-判定循环是数据相关的，硬件乘法把该段整块消除。
- **multiarray 为阴性对照**：常量乘法被 gcc 优化为移位+加法（静态 `mul`=0），
  M/I 双臂 clock 逐拍相同（1,073），证明 Δclock 全部来自 `mul` 本身。
- **口径**：对照用 `zmmul` 子集而非完整 `rv32im`，除法/取模两臂同走软例程，
  避免 DIV 差异污染；实现 DIV/REM 后可改 `-march=rv32im` 去软例程复测。

---

## 7. 参考资料

- `reference/reference-card.pdf` — RISC-V 指令速查卡
- `reference/riscv-spec-20191213.pdf` — RISC-V 官方规范（RV32I/M 精确定义）
- `reference/RISC-V-Reader-Chinese-v2p1.pdf` — 《RISC-V 读者》中文版
- `reference/CAAQA5.pdf` — 计算机组成与设计：硬件/软件接口（对应讲义参考架构）
- `ppt/` — 讲义 lec1–lec4

---

## 8. 开发状态与路线图

- **RTL 化重建线**：`RISC-V-Simulator-Template/` 以 `Register<N>/Wire<N>/
  dark::Module` 框架把同一架构逐模块改写为可综合风格（Register 双缓冲 + 周期末
  sync、Wire 懒求值组合接线）。两树共用同一套 golden，clock 逐位对拍一致是迁移
  硬门禁。
- **DIV/REM**：译码/发射已预留；实现后启用 `-march=rv32im` 并去掉软除法例程。
- **TAGE-SC**：统计校正器曾试装后移除（简化版全线退化），后续按 Seznec 论文补
  充分历史长度计数器 + 滞回再试。
- **取舍复核**：`VERBOSE=icache/cdb` 的命中率与总线争用画像长期保留，供缓存
  几何、总线拆分、预测器容量等取舍参考。
