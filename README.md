# RV32IM Out-of-Order Processor Simulator

周期语义级（cycle-accurate）的 RISC-V **乱序执行（out-of-order）处理器仿真器**。
以 C++20 对 Tomasulo 风格微架构（重命名 / 物理寄存器堆 / 重排序缓冲 / 保留站）做
逐周期建模，模型按**可综合硬件的纪律**书写：模块每周期只写自己的状态、跨模块信息
一律走"周期初快照 + 组合总线"——19 个流水级以**任意顺序**调用均产生逐位相同的
结果与时钟数。既可作为后续 RTL 移植的参考模型，也可用于微架构方案的量化对比。

| | |
|---|---|
| **架构** | Tomasulo 乱序执行 · 按序提交（取指/提交有序，执行/写回/访存乱序） |
| **ISA** | RV32I 全集 + RV32M（乘法 `mul/mulh/...` Booth 乘法器、除法 `div/divu/rem/remu` SRT radix-4 除法器） |
| **语义** | 逐周期双相模型：`comb()` 组合求值 + `tick()` 沿采样，19 级顺序无关 |
| **实现** | 单体 C++20，无外部依赖；Linux / WSL / MSYS 均可构建 |
| **验证** | 行为回归（对 `docs/benchmarks.md`）+ 双树 x10+clock 逐位一致 + RV32M 双臂 A/B（reorder / MUL 直驱已于 2026-09-10 退役） |

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
   跨模块总线全部在组合阶段（快照边）求值。由此，流水级调用顺序可任意交换
   （曾由 `reorder_test` 作硬约束回归；该测试已于 2026-09-10 随 `test/` 清理退役）。
2. **面向 RTL 移植的参考模型**。`Phase` 状态机、双口读/写通道、单写端口、寄存器级
   流水（Booth 乘法器）、回写总线冲突等均以 Verilog 习惯的 C++ 呈现，并有独立的
   `RISC-V-Simulator-Template/` 重建线把同一架构逐步改写为可综合风格。
3. **可量化对比的研究平台**。多路结果总线写回 vs 单总线、DCache 命中自答、M 扩展硬件
   乘法等方案的收益都落在同一套回归体系上，用 clock 逐位对拍作结论依据。

### 1.2 特性一览

| 特性 | 说明 |
|------|------|
| 乱序执行核心 | ROB 64 / PRF 128 / RAT；issue 侧完成 rename，commit 按序释放 |
| 四路结果总线 | `aluCDB` / `lqCDB` / `mulCDB` / `divCDB`，各源独立、无跨单元仲裁 |
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
   组合总线：`FetchDecision`（取指决策 + GHR 移位标记）、四条结果总线候选（每源
   `build()` 取唯一队首并过 squash 门）、`DispatchArbiter` 派发、`IssueArbiter::build`
   （发射包：rename + 各队 push 载荷）、`MemArbiter` 访存准入（store 优先互斥）、
   store 就绪广播、DCache 应答线等。
2. **`tick()`（沿采样）**——19 个模块各自 tick。总线信号在 comb 中打包进对应
   `Input`；模块 tick **只读快照、只写自己（活体）状态**，跨模块写为零。

因此阶段调用顺序可任意交换（曾由 `reorder_test` 19 级乱序验证，现该环节已退役）；停机条件 = halt
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
操作数就绪即乱序派发到 ALU/MUL/DIV/AGU/BRU；结果经四根独立总线并行写回 PRF/ROB；
ROB 头按序提交。误预测与记忆违例统一进 `FlushArbiter` 排队（最老优先），各模块
从 ROB 条目 checkpoint 恢复。

**关键规格**：ROB 64 / PRF 128 / RAT 32；保留站 Integer 8 · Multiply 4 · Load
4 · StoreAddr 4 · StoreValue 4 · Branch 4；每周期至多 1 次发射；执行队列各 4 槽、
每源每周期 1 个队首结果；结果总线 4 根（`aluCDB`/`lqCDB`/`mulCDB`/`divCDB`，无仲裁）；
MUL = Booth radix-4 + CSA 三级流水（`mul/mulh/mulhu/mulhsu`）；DIV = SRT radix-4 迭代
（单实例，`canAccept()` 背压）；FlushArbiter 4 项。

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
- **DIV/REM 族**：`div/divu/rem/remu` **已内联执行**（SRT radix-4 迭代除法器，见
  [§8](#8-开发状态与路线图)）——专用 `divideRS` + **第四路独立结果总线 `cdbOfDiv`**，
  与 ALU/LQ/MUL 各源独立、无跨单元仲裁。验证见 [§6.5](#65-divrem-硬件除法验证)。

---

## 3. 仓库结构

### 3.1 目录树

```
RISC-V-Tomasulo-CPU-Simulator/
├── CMakeLists.txt                    # 根构建（CMake），产物 ./code
├── README.md                         # 本文档（总览 + 子系统精简简介）
├── issue.pdf                         # 题目与评测说明（ISA 约束 / 口径 / 数据来源）
├── AGENTS.md                         # 开发账本：架构决策 / 模块归属 / 验证流程
├── test.sh                           # 课程语料行为回归（x10 / 分支 / clock / retired / IPC）
├── test_M.sh                         # RV32M 双臂 A/B（M vs I：clock / IPC / 分支 / 收益）
├── test_IPC.sh                       # RV32IM IPC 语料回归并生成 docs/ipc_benchmarks.md
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
│   │   ├── DIV.hpp                   # M 扩展除法单元（SRT radix-4，已接入流水线）
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
│   │   └── CDB.hpp                   # 结果总线载荷（aluCDB/lqCDB/mulCDB/divCDB）+ build 工厂
│   │
│   ├── FetchUnit/  InstructBuffer/   # 各模块 tick 实现（与 include/*.hpp 一一对应）
│   ├── Decoder/  ICache/  IMEM/
│   ├── RS/  PRF/  RAT/  ROB/
│   ├── ALU/  MUL/  DIV/  AGU/  BRU/
│   ├── LQ/  SQ/  DCache/  DMEM/
│   ├── BPU/  StaticArbiter/  DynamicArbiter/  CDB/
│   └── ...
│
├── data/                            # 镜像 / 语料
│   ├── testcases/                   # 18 个基准（.data 镜像 + .dump 反汇编；多含 .c 源码）
│   │   └── io.inc                   # 18 个 .c 共享的校验头（x10 语义来源，勿删）
│   ├── testcases_rv32im/            # RV32M 扩展 A/B 双臂语料（M = rv32im 硬乘+硬除 / I = rv32i + libdiv.S 软乘软除）
│   │   ├── M/                       # 18 用例：rv32im 重编译（硬件 mul/div/rem 内联，链接行去 libdiv.S）
│   │   └── I/                       # 同用例 rv32i + libdiv.S（软乘 __mulsi3 / 软除 libdiv.S）对照臂
│   ├── testcases_ipc/               # RV32IM IPC 基准（median/multiply/qsort/rsort/towers/vvadd）
│
├── test/                            # （2026-09-10 整体清理删除；reorder_test / mul_unit_test 等均已移除）
│
├── docs/                            # 设计文档（子系统详析，见 §2.3）
│   ├── benchmarks.md                # 逐用例实测 + 行为回归 golden 数据源（x10 / clock / 命中率 / 准确率）
│   ├── ipc_benchmarks.md            # test_IPC.sh 生成的 RV32IM IPC 基准结果
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
│   └── CMakeLists.txt  test.sh  test_M.sh  test_IPC.sh
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
| `DIV` | 专用除法单元：SRT radix-4 迭代递推（单实例，`canAccept()` 背压；第四路 CDB） |
| `AGU` | load/store 地址计算（含队首 store 地址广播） |
| `BRU` | 条件分支执行（pcFrom/pcResult） |
| `LQ` | 加载队列：store→load 转发、违例报告、load 完成总线 |
| `SQ` | 存储队列：转发源、`canDispatchLoad` 把关 |
| `DCache` | 数据缓存：READY/WAIT FSM、命中 1 拍自答、脏逐出写回 |
| `DMEM` | 数据内存：读/写双口独立在飞、行级读写请求 |
| `BPU` | TAGE 方向 + BTB/TargetCache 目标 + SARAS，双口训练 |
| `StaticArbiter` | 无状态仲裁器族：Dispatch/MemArbiter/IssueArbiter + 发射包 |
| `DynamicArbiter` | `FlushArbiter`：squash 请求队列 + 检测/恢复 |
| `CDB` | 结果总线载荷的 build 工厂（alu / lq / mul / div 四路） |
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
  src/RS/RS.cpp src/ALU/ALU.cpp src/MUL/MUL.cpp src/DIV/DIV.cpp src/AGU/AGU.cpp src/BRU/BRU.cpp \
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
正确/总数与准确率；`clock` 输出端到端总时钟、HALT 提交时冻结的 IPC 时钟、
实际退休指令数与 IPC；`icache`/`cdb`/`bpmiss` 输出命中率、总线争用等概要。

---

## 5. 性能数据

> 各基准的逐项实测数据已外置到 [`docs/benchmarks.md`](docs/benchmarks.md)，
> 不再在本表重复维护。数据口径与文档一致：**主存延迟固定 50 周期、L1 命中
> 零延迟、8 KB 指令缓存 + 64 KB 数据缓存（写回 + 写分配）**；每个用例记录
> cycles、retired/IPC、按控制流类型拆分的预测准确率（cond / jal / jalr / branch）
> 以及 I$/D$ 命中率。行为回归（退出码 / x10 对照本表 / 崩溃检测）仍由
> [`./test.sh`](#6-验证与回归) 执行。

要点（详见 `docs/benchmarks.md` 表注）：

- 小基准（naive/lvalue2 等）分支基数小，准确率波动属正常；大基准（basicopt1/
  hanoi/qsort/superloop/tak）均 ≥95%。
- pi 的剩余误预测集中在软件除法例程的数据相关分支（RV32I 无硬件除法），属历史
  窗口外熵墙；RAS 冷启动漏栈问题已由预译码修复。

---

## 6. 验证与回归

验证分三层：**行为回归**（x10/分支/clock/IPC，基准见 `docs/benchmarks.md`）、
**扩展双臂 A/B**（rv32im 重编译管线，见 §6.4）、**IPC 语料回归**（见 §6.7）。
三个入口分别为 `test.sh`、`test_M.sh`、`test_IPC.sh`；模板树内提供同名脚本，默认运行模板
自己的 `code` 和 `data/`，同时复用仓库根 `docs/` 中的 golden/报告。原"重排一致性"（`reorder_test`）
与"MUL 单元直驱自测"两节已随 2026-09-10 `test/` 清理退役（保留于 §6.2/§6.3 作历史）。

### 6.1 行为回归 — `test.sh`

以 `data/testcases/*.data` 为输入运行 `./code`，校验退出码（崩溃检测）与
`x10&0xFF`（对照 `docs/benchmarks.md` 的 `result` 列），汇总分支正确率、总时钟、
IPC 时钟、退休指令数与加权 IPC；任一 FAIL/CRASH 或 IPC 统计缺失均非 0 退出（pi 排最后）。

```bash
./test.sh                 # 全量
./test.sh 'q*'            # 通配符过滤
BP_BIN=./code ./test.sh   # 指定二进制
```

### 6.2 重排一致性 — ~~`test_reorder.sh`~~（已退役）

`reorder_test` 曾以**任意顺序**调用 19 个流水级，要求各排列给出相同的 `x10&0xFF`
与相同时钟，并额外对 DMEM 最终镜像与 DCache 行阵做指纹比对——是全系统一致性的
最强约束（全排列 19! ≈ 1.2×10¹⁷，按单位点耗时自动分档采样；`diff` 模式逐周期
打印首个状态分叉点）。

> ⚠️ 该测试与脚本已于 **2026-09-10 随 `test/` 整体清理删除**（`reorder_test.{cpp,exe}`、
> `test_reorder.sh`、探针脚本与产物一并移除）。现行验证闭环 = ① 双树 x10+clock 逐位
> 一致 → ② `docs/benchmarks.md` 的 `result`/`cycles` → ③ 仅架构性改动才允许 clock 变差。

### 6.3 MUL 单元自测 — ~~`mul_unit_test`~~（已退役）

曾直驱乘法器内部级（Booth 部分积 → CSA → 结果）与整单元，含 10 万组 LCG 随机
对拍。该文件（`test/mul_unit_test.cpp`）与配套 `patch_mul.py` 已于 **2026-09-10
随 `test/` 清理删除**；乘法正确性现由 §6.4 的 rv32im 双臂 A/B 与全量回归覆盖。

### 6.4 RV32M 扩展评测 — `test_M.sh`

为量化 M 扩展（硬件乘+除）收益，用统一双臂语料 `data/testcases_rv32im/` 逐例对拍：

- **M 臂** = `-march=rv32im`（乘法 `mul/mulh/...` 与除法 `div/divu/rem/remu` 全部内联为
  硬件单元，链接行去掉 `libdiv.S`）；
- **I 臂** = `-march=rv32i` + `libdiv.S`（软例程 `__mulsi3` 负责 `*`、软除法例程负责 `/` 与 `%`）。

两臂同 `crt0` / 同链接脚本 / 同 `-O1`（避 `magic.c` 的 `-O2` UB），故 clock 差即**纯 M 扩展
（乘+除）收益**；镜像经 `objcopy -O verilog` 生成，须为 **LF 行尾**（CRLF 残留会被解析为字节
token，导致第二段起镜像错乱）。

```bash
./test_M.sh                 # 全量：18 用例 × M/I 双臂（含 pi，I 臂约数分钟）
QUICK=1 ./test_M.sh         # 跳过 pi
./test_M.sh gcd             # 只跑名字匹配该 glob 的用例
BP_BIN=/path/to/code ./test_M.sh   # 指定模拟器二进制（默认 ./code）
```

脚本逐例打印 `Exit / Clock / IPC Clock / Retired / IPC / Time / x10 / Golden / Br% / Cond% / Jal% / Jalr% / I$% / D$% / mul / div`
（分支四项分型口径与 `docs/benchmarks.md` 一致），并汇总：

| 检查项 | 说明 |
|------|------|
| 返回值正确性 | `x10&0xFF` 对照 `docs/benchmarks.md` 的 `result` 列（表中 `cycles` 为课程原镜像口径，自制镜像 layout 不同，不比） |
| 跨臂一致性 | 同语义两套乘/除实现，M/I 结果不同必是 bug |
| 崩溃检测 | 退出码非 0 记 `CRASH` |
| 收益 | `Δclock(M/I)%`、speedup 与两臂**实际运行时间**，附 `.dump` 静态 `mul`/`div` 条数 |
| TOTAL 汇总 | 两臂总 clock、加权 IPC（`Σretired/Σipc-cycles`）、总实际运行时间、**加权分支正确率**（`Σcorrect/Σtotal`）及跨臂差 |

实测 **18/18 跨臂 x10 一致**；逐用例明细、总时钟、总分支正确率与收益见
[`docs/benchmarks.md`](docs/benchmarks.md) 的 `## RV32M A/B`。

---

### 6.5 DIV/REM 硬件除法验证

`div/divu/rem/remu` 内联执行后，用`data/testcases_rv32im/M` 臂做端到端验证：
与 `M`/`I` 臂同 `crt0`、同链接脚本、同选项，仅两处不同 —— `-march=rv32im`，且**链接行去掉
`libdiv.S`**（软除法例程）。于是程序里的 `/` `%` 直接编译成硬 `div/rem` 指令。

```bash
# 生成（WSL）
riscv64-unknown-elf-gcc -march=rv32im -mabi=ilp32 -O2 \
  -fno-tree-loop-distribute-patterns -nostdlib -nostartfiles \
  -I data/testcases -T ~/rv32im_course/link_course.ld \
  ~/rv32im_course/crt0_course.S data/testcases/<case>.c -lgcc -o /tmp/<case>.elf
riscv64-unknown-elf-objdump -d /tmp/<case>.elf > data/testcases_rv32im/M/<case>.dump
riscv64-unknown-elf-objcopy -O verilog /tmp/<case>.elf data/testcases_rv32im/M/<case>.data
```

**验收口径**：`x10 & 0xFF` 对 `docs/benchmarks.md` 的 `result` 列（该表的 `cycles` 是课程原
rv32i 镜像口径，自制镜像不可比）。

| 检查项 | 说明 |
|------|------|
| 返回值正确性 | 18/18 全对（2026-09-12 实测，见下表） |
| 静态 div/rem | `.dump` 中 `div/divu/rem/remu` 条数，证明该臂确实走硬件路径 |
| 零回归 | 原 `data/testcases/` 18 例 x10 + clock 逐位与 golden 一致 |

| 用例 | x10 | 静态 div/rem | 用例 | x10 | 静态 div/rem |
|------|-----|------|------|-----|------|
| array_test1 | 123 | 1 | magic | 106 | 1 |
| array_test2 | 43 | 1 | manyarguments | 40 | 1 |
| basicopt1 | 88 | 3 | multiarray | 115 | 1 |
| bulgarian | 159 | 9 | naive | 94 | 0 |
| expr | 58 | 1 | pi | 137 | 7 |
| gcd | 178 | 4 | qsort | 105 | 1 |
| hanoi | 20 | 1 | queens | 171 | 1 |
| lvalue2 | 175 | 1 | statement_test | 50 | 2 |
| — | — | — | superloop | 134 | 1 |
| — | — | — | tak | 186 | 1 |

**18/18 通过**（`naive` 无 `div/rem`，作阴性对照）。

> ⚠️ `magic.c` 需单独用 `-O1` 编译：其 `make[x-1][j]` 在 `x==0` 时是未定义行为（源码靠
> `x==0` 的短路保护），gcc 13.2 在 `-O2` 下会优化出非法访存地址（触发 DCache 断言
> `PrRd(addr=13)`）。`-O0/-O1/-Os` 下均正常（x10=106）。**该崩溃与 DIV 无关** ——
> `rv32i` 与 `rv32im` 臂同样崩，属既有工具链 UB，不计入 DIV 账。

### 6.6 M 扩展（乘+除）硬件化收益数据

统一 M vs I 双臂的逐用例收益（clock、实际运行时间、分支四项分型、I$/D$ 命中率、
静态 `mul`/`div` 条数）与 TOTAL 汇总（总时钟、总运行时间、**加权分支正确率**及跨臂
收益），已全部写入 [`docs/benchmarks.md`](docs/benchmarks.md) 的 `## RV32M A/B`，
由 §6.4 的 `./test_M.sh` 产出，本文件不再重复维护。

> ⚠️ **跨臂 brAcc 不可直接比较**：两臂编译出的代码不同，软例程会引入大量额外分支
> （如 `gcd` 的 M 臂仅 7 次分支、I 臂 125 次），差值反映**代码差异**而非预测器退化。
> 预测器回归判据仍是「同一镜像跨模拟器版本」对 `docs/benchmarks.md` 主表四列
> （2026-09-12 复核：18/18 逐列零漂移）。

### 6.7 RV32IM IPC 语料 — `test_IPC.sh`

脚本遍历 `data/testcases_ipc/*/*.data`，逐例检查模拟器退出码以及 x10、clock、IPC
输出格式，并将结果原子写入 `docs/ipc_benchmarks.md`。该语料当前包含
`median`、`multiply`、`qsort`、`rsort`、`towers`、`vvadd` 六个 RV32IM 基准。

```bash
./test_IPC.sh                         # 使用根目录 ./code，运行全部 IPC 基准并更新报告
BP_BIN=/path/to/code ./test_IPC.sh   # 指定待测模拟器二进制

cd RISC-V-Simulator-Template
./test.sh gcd                         # 模板树课程语料单例
QUICK=1 ./test_M.sh                   # 模板树 RV32M 双臂，跳过 pi
./test_IPC.sh                         # 模板树 IPC 语料，报告仍写到根 docs/
```

`test_IPC.sh` 不读取 golden；它验证统计行完整性并记录当前实现的结果。行为正确性仍由
`test.sh` 的 `docs/benchmarks.md` 对照和 `test_M.sh` 的 M/I 跨臂一致性负责。


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
  sync、Wire 懒求值组合接线）。两树共用同一套 golden（= `docs/benchmarks.md`），clock 逐位对拍一致是迁移
  硬门禁。
- **DIV/REM（已落地，2026-09-12）**：SRT radix-4 硬件除法器，算法 SSOT = `docs/backend.md` §4.4。
  `decodeOp` 解出 funct3 4..7 → `issue_Divide` → 专用 `divideRS` → `DispatchArbiter` DIV 通道
  （`canAccept()` 背压，单实例无输出缓冲）→ `cdbOfDiv` 第四路总线 → ROB/PRF。验收：课程镜像
  18/18（x10+clock 逐位一致）+ `data/testcases_rv32im/M` 臂 18/18 x10 全对。
  **待办**：同步回 `RISC-V-Simulator-Template/` 树 + rv32im 第三臂的 clock 收益 A/B。
- **TAGE-SC**：统计校正器曾试装后移除（简化版全线退化），后续按 Seznec 论文补
  充分历史长度计数器 + 滞回再试。
- **取舍复核**：`VERBOSE=icache/cdb` 的命中率与总线争用画像长期保留，供缓存
  几何、总线拆分、预测器容量等取舍参考。
