# RV32IM Out-of-Order Processor Simulator

周期级（cycle-accurate）RISC-V **乱序执行处理器仿真器**。以 C++20 对 Tomasulo 风格微架构
（重命名 / 物理寄存器堆 / 重排序缓冲 / 保留站）逐周期建模，并按**可综合硬件纪律**书写：
模块单写口、跨模块信息走"周期初快照 + 组合总线"——19 个流水级以**任意顺序**调用均产生
逐位一致的结果与时钟数。既作 RTL 移植的参考模型，也用于微架构方案的量化对比。

| | |
|---|---|
| **架构** | Tomasulo 乱序执行 · 按序提交（取指/提交有序，执行/写回/访存乱序） |
| **ISA** | RV32I 全集 + RV32M（Booth 乘法、SRT radix-4 除法） |
| **实现** | 单体 C++20，无外部依赖；Linux / WSL / MSYS 均可构建 |
| **验证** | RV32IM 行为与周期回归（golden = `docs/benchmarks.md`）+ 双树 x10/clock 逐位一致 |

## 目录

- [1. 系统架构](#1-系统架构)
- [2. 仓库结构](#2-仓库结构)
- [3. 构建与运行](#3-构建与运行)
- [4. 验证与性能](#4-验证与性能)
- [5. 开发状态与路线图](#5-开发状态与路线图)
- [6. 参考资料](#6-参考资料)

## 1. 系统架构

```text
                    ┌──────────── 前端 Front-End ────────────┐
 FetchDecision ───► │ FetchUnit(PC/halt) → ICache → IMEM     │
 (Tournament+BTB+RAS │      │  8KB 直映      20 周期主存回填   │
   预测 + 预译码)    │      ▼                                  │
                     │ FetchQueue(FQ,4) ───► DecodeUnit → IQ(4)│
                     └───────────────────┬────────────────────┘
                                         ▼
               ┌────────────── 重命名 / 发射 Issue ──────────────┐
               │ IssueArbiter：RAT rename + PRF(64) pop+LINK     │
               │              + ROB(16) push + RS/LQ/SQ 入队     │
               └──────────────┬──────────────────────────────────┘
                              ▼
   ┌───────────────────────────────────────────────────────────┐
   │ 保留站 RS ──派发──► 执行单元 ──► 结果总线（每单元各一根）    │
   │   Integer 4 ──► ALU；Multiply 2 ──► MUL；Divide 1 ──► DIV   │
   │   Load 4 / StoreAddr 4 ──► AGU；Branch 4 ──► BRU           │
   │   StoreValue 4 ──► SQ；LQ / SQ ──► DCache ──► DMEM(双口)   │
   └──────────────────────────────┬────────────────────────────┘
                                  ▼
              ROB 按序提交；误预测 ──► FlushArbiter
              （RAT 从 archRAT 基线 + ROB 条目重放恢复；PRF/GHR/RAS 走 checkpoint）
```

取指与提交保持程序序，执行/写回/访存完全乱序。L1 命中零延迟（ICache 当拍组包、
DCache 命中 1 拍自答），缺失回填与脏逐出统一走 **20 周期主存延迟**（IMEM/DMEM）；
误预测经 `FlushArbiter` 排队，按最老优先整窗恢复（RAT 重放到 `SquashTag`，
PRF/BPU 取边界 checkpoint，队列整体清空）。

### 1.1 周期模型

每周期两阶段，对应硬件时钟语义：

1. **`comb()`**——全部模块 memcpy 进快照，在快照边求值跨模块总线：`FetchDecision`、
   四路结果总线候选、`DispatchArbiter` / `IssueArbiter` 仲裁包、`MemArbiter` 访存准入、
   store 就绪广播、DCache 应答等。
2. **`tick()`**——19 个模块各自沿采样，只读快照、只写自身状态。

阶段顺序可任意交换。停机条件 = halt 已提交 且 FQ/IQ/ROB 全空，并完成**访存 drain**
（SQ 空、DCache 空闲、DMEM 读/写口空闲），保证停机瞬间无"已提交但未写达缓存"的在途 store。

### 1.2 子系统

处理器分**前端 / 后端 / 访存 / 缓存**四个子系统，详细设计见 `docs/` 同名文档。

| 子系统 | 功能 | 关键规格 | 详设 |
|---|---|---|---|
| 前端 | 分支预测 → 取指 → 预译码 → 译码 | 8 KB 直映 ICache；FQ 4 / IQ 4；Tournament 方向 + BTB/RAS/SARAS | [`frontend.md`](docs/frontend.md) |
| 后端 | 发射 rename → 乱序执行 → 写回 → 按序提交 | ROB 16 / PRF 64 / RAT 32；RS 七个物理池共 23 槽；四路独立结果总线；MUL Booth 三级流水、DIV SRT 单实例背压 | [`backend.md`](docs/backend.md) |
| 访存 | LQ/SQ、store→load 转发、保守访存准入 | LQ 8 / SQ 8；每周期 1 个请求（store 优先）；store 提交点落缓存 | [`memory.md`](docs/memory.md) |
| 缓存 | L1I / L1D / 片上主存 | L1I 8 KB 直映；L1D 64 KB 4 路写回+写分配；主存 20 周期 | [`cache.md`](docs/cache.md) |

### 1.3 指令集

- **RV32I** 全集（x0 恒零语义正确）；`ecall/ebreak` 不在集内，程序以**停机字**结束（见 §3.2）。
- **RV32M**：`mul/mulh/mulhu/mulhsu`（Booth + CSA 三级流水）与 `div/divu/rem/remu`
  （SRT radix-4，专用 `divideRS` + 第四路总线 `cdbOfDiv`）**全部内联硬件执行**；算法 SSOT
  见 [`docs/backend.md`](docs/backend.md) §4.4，收益评测见 §4.2。

## 2. 仓库结构

```text
.
├── CMakeLists.txt              # 根构建，产物 ./code
├── README.md
├── AGENTS.md                   # 开发账本：架构决策 / 模块归属 / 验证流程
├── test.sh  test_IPC.sh              # RV32IM 回归与 IPC 入口（见 §4）
├── src/                        # 模拟器源码（include/*.hpp 声明 + <Module>/ 实现）
├── data/
│   ├── testcases/              # 18 个 RV32IM 基准（.c + .data 镜像 + .dump）
│   └── testcases_ipc/          # IPC 基准（median/multiply/qsort/rsort/towers/vvadd）
├── docs/                       # 设计文档：frontend / backend / memory / cache / benchmarks
├── reference/                  # 参考资料（指令速查卡、RISC-V 规范、教材）
└── RISC-V-Simulator-Template/  # RTL 化重建线（git submodule，Register/Wire 框架，见 §5）
```

`src/include/*.hpp` 为模块声明，`src/<Module>/<Module>.cpp` 为实现。19 个流水级站名：
`rat·lq·sq·decode·agu·bru·bp·dmem·alu·rs·rob·prf·arb·imem·fq·icache·fetchunit·dcache·md`。

| 子系统 | 模块 |
|---|---|
| 前端 | `FetchUnit` `ICache` `IMEM` `InstructBuffer`(FQ) `Decoder`(+IQ) `BPU` |
| 后端 | `RAT` `PRF` `ROB` `RS` `ALU` `MUL` `DIV` `AGU` `BRU` `StaticArbiter` `DynamicArbiter` `CDB` |
| 访存 / 缓存 | `LQ` `SQ` `DCache` `DMEM` |
| 公共 | `Memory` `common.hpp` `util.hpp` |

## 3. 构建与运行

### 3.1 构建

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

> 调试版追加 `-D_DEBUG`（双写断言）。长用例务必用 **Release**（-O0 慢数十倍），建议在 WSL/Linux 下运行。

### 3.2 运行与终止约定

程序从 stdin 读 Verilog-hex 镜像：`@<hex>` 行切换基址，其后空格分隔的字节依次写入连续
地址。模拟器不含 OS：取到**停机字** `0x0ff00513` 即闩锁 halt，提交后停机；标准输出为
停机时 `x10` 的低 8 位，进程正常退出码为 0。

```bash
./code < data/testcases/gcd.data                        # stdout：x10 低 8 位
VERBOSE=branch,clock ./code < data/testcases/gcd.data   # 统计走 stderr
```

`VERBOSE`（stderr，逗号分隔）：`issue` `exec` `wb` `commit` `lsq` `mem` `clock`
`branch` `prf` `mdp` `bpmiss` `icache` `cdb`，或 `all`。`clock` 输出总时钟、IPC 时钟、
退休指令数与 IPC；`branch` 输出分支正确率；`icache` / `cdb` / `bpmiss` 输出概要画像。

## 4. 验证与性能

两个入口脚本对应 RV32IM 行为与周期回归（§4.1）和 IPC 语料（§4.3）。
**golden 唯一来源 = [`docs/benchmarks.md`](docs/benchmarks.md) 的 `Testcases` 表**
（`result` = x10、`cycles` = 严格周期门禁）；逐用例 IPC、分支四项分型、I$·D$ 命中率
也在该文档，本 README 不再重复。重排一致性与旧单元直驱脚手架已退役；现行闭环 =
① 两树分别对 golden 校验 x10+cycles → ② 双树结果逐位一致。

### 4.1 行为回归 — `test.sh`

以 `data/testcases/*.data` 中的 RV32IM 镜像运行 `./code`，校验退出码、`x10&0xFF` 和
cycles（均对照 golden），汇总分支正确率、总时钟、退休指令数与加权 IPC。缺失镜像、
缺失 golden、零用例、统计不完整或任一结果不一致都会非 0 退出。

```bash
./test.sh                 # 全量 18 用例（pi 排最后）
./test.sh 'q*'            # 通配符过滤
BP_BIN=./code ./test.sh   # 指定二进制
```

### 4.2 M 扩展路径

活动语料由 `-march=rv32im` 构建，乘法和除法直接执行硬件 M 指令。反汇编静态计数示例：
gcd 含 4 条 `div/rem`、bulgarian 含 6 条 `mul` 和 9 条 `div/rem`、pi 含 3 条 `mul`
和 7 条 `div/rem`；`naive` 不含 M 指令，作为基础整数路径的阴性对照。历史收益数据保留在
`docs/benchmarks.md`，不属于当前回归输入。

### 4.3 IPC 语料 — `test_IPC.sh`

遍历 `data/testcases_ipc/*/*.data`（median/multiply/qsort/rsort/towers/vvadd），检查退出码、
要求每例自校验结果 `x10 == 0` 并校验统计行格式，随后原子更新 `docs/ipc_benchmarks.md`。
脚本不读逐例 golden；任一自校验失败时保持原报告不变并以非 0 退出。

```bash
./test_IPC.sh                        # 全量并更新报告
BP_BIN=/path/to/code ./test_IPC.sh   # 指定二进制
```

模板树（`RISC-V-Simulator-Template/`）内提供同名两个脚本，默认运行自身的 `code` 与 `data/`，报告写回根 `docs/`。

## 5. 开发状态与路线图

- **RTL 化重建线**：`RISC-V-Simulator-Template/`（git submodule）以 `Register` / `Wire` /
  `dark::Module` 框架把同一架构逐模块改写为可综合风格；两树共用 golden，clock 逐位对拍一致是迁移硬门禁。
- **DIV/REM 已落地（2026-09-12）**：SRT radix-4 除法器完成接线、验证并同步接入模板树（见 §4.2）。
- **BPU 面积终态（2026-09-21）**：方向侧采用 local/global/selector 各 256×2-bit 的 Tournament
  预测器与 8-bit GHR；目标侧保留 BTB/RAS/SARAS。BTB 的 64 项以 `{PC[31:8], target[31:2], state}`
  收紧至 56 bit/项；间接目标缓存（BHT+Target Cache）在活动语料上无可观测收益，已按面积/效率权衡
  删除。完整模板 BPU Register 状态由 TAGE 基线的 24,357 bit 降至 7,542 bit（-69.04%），该变换
  不改时序（当时 18 例总 clock 为 12,237,892）。
- **保守 load 准入（2026-09-21）**：退役 MDP 投机（删除 load-violation 检测与 squash 通路），
  `SQ::canDispatchLoad` 要求更老未提交 store 地址均已知且无同址冲突才允许 cache 准入。仅
  `magic` 时序变化（573122→574512），总 clock **12,239,282**，IPC **0.553640**，分支正确率 **93.8356%**。
- **取舍复核**：`VERBOSE=icache` / `cdb` 的命中率与总线争用画像长期保留，供缓存几何、总线拆分、预测器容量等决策参考。

## 6. 参考资料

- `reference/reference-card.pdf` — RISC-V 指令速查卡
- `reference/riscv-spec-20191213.pdf` — RISC-V 官方规范（RV32I/M 精确定义）
- `reference/RISC-V-Reader-Chinese-v2p1.pdf` — 《RISC-V 读者》中文版
- `reference/CAAQA5.pdf` — 计算机组成与设计：硬件/软件接口
