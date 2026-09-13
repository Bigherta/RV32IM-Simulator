# 主频（fmax）受限分析：全模块关键路径诊断与补救

> 分析对象：`RISC-V-Simulator-Template`（25 个 `dark::Module`，全 Wire 化总线）
> 视角：把当前 C++ 组合云按 1:1 映射到 RTL（`always_comb` + `always_ff`）后的**静态时序分析**
> 日期：2026-09-08　基线：迁移收官版（阶段 0–9 完成，MUL 已接线）

---

## 0. 结论先行

| 排名 | 降频主因 | 位置 | 估算级数(GE) | 严重度 |
|---|---|---|---|---|
| **1** | **Dispatch → 读寄存器 → 执行 → 写回 全挤在同一拍**，AGU 池 12 项串行折链 | `StaticArbiter.cpp:selectOldest` + `CPU.cpp:434-503` + `ALU.cpp:evaluate` | **~136** | ★★★★★ |
| **2** | **MUL stage3 的 64 位 CPA**（`sOld + cOld`，默认推断=行波进位） | `MUL.cpp:68` | **~128（若RCA）** | ★★★★★ |
| **3** | **ROB 提交 `seen[20]` 线性去重**（顺序语义，不可树化） | `ROB.cpp:123-129` | **~84** | ★★★★ |
| **4** | **BPU `refoldView` 现场折叠 48 位 GHR**（TAGE 经典瓶颈） | `BPU.cpp:11-23 / 298-307` | **~47 + 2×SRAM** | ★★★★ |
| **5** | **SQ `planDataForward` 16 项环形 CAM**，且 8 字段 × 4 槽重复展开 | `SQ.cpp:78-146` + `CPU.cpp:1626-1689` | **~14（RTL）/ 32×（仿真）** | ★★★ |

**估算 fmax**（28 nm 典型库，含线+扇出按 20 ps/GE；**结构估算，非实测**）：

| 口径 | 当前 | 做完 P0 补救后 |
|---|---|---|
| ASIC 28 nm | **≈ 350–400 MHz** | ≈ 800–900 MHz |
| Xilinx Artix-7 (-1) | **≈ 50–65 MHz** | ≈ 130–160 MHz |
| 仿真吞吐（WSL/Release） | 25.7 K 拍/s | 见 §7 |

**好消息**：全机**无纯组合环路**（所有跨模块回环都被 `Register` 打断，`FlushArbiter` 的 3 拍请求队列是关键隔离点），综合不会报 combinational loop。

---

## 1. 方法论与延迟模型

模板语义 = 一个时钟周期内：

```
Register._M_old (Q端) ──► [ 组合云 ] ──► Register._M_new (D端)
                            ▲
        Wire lambda / const 桥接访问器 / 无状态 Module 的 wire_output()
        / work() 内的算术与扫描 —— 全部是组合逻辑
```

所以 **fmax = 1 / max(所有 Reg→Reg 路径的组合云深度)**。

**级数(GE)折算口径**（1 GE = 1 个 2 输入 NAND，FO4）：

| 结构 | GE |
|---|---|
| 2 输入 AND/OR/NAND/XOR | 1–2（XOR=2） |
| N:1 mux（树化） | 2·⌈log₄N⌉ |
| k 位全等比较 | ⌈log₂k⌉ + 1 |
| k 位行波加法 (RCA) | 2k |
| k 位前缀加法 (KS/CLA) | 2·⌈log₂k⌉ + 2 |
| N 项串行优先链（winner 逐级更新） | N × (比较 + mux) ≈ 7N |
| SRAM 宏读（1024×8b / 256×32b） | 单独计 ~ 0.4–0.6 ns |

> 关键区分：**`for` 循环在 C++ 里是顺序求值，但它有两种 RTL 命运**——
> - 无跨迭代状态依赖（如 `for i: if (match[i]) out = i` 取首个）→ 可树化为优先编码器，**O(log N)**
> - 有跨迭代状态依赖（`winner` 逐级更新、`Found` 标志置位后跳过、`seen[]` 去重）→ **RTL 只能 O(N) 串联**
> 本文件里所有 ★★★★ 以上条目都属于后者。

---

## 2. 关键路径全景

```
                            ┌──────────── 周期边界 ────────────┐
 [A] 发射链                  │                                  │
 DecodeUnit.Reg ─► IssueArbiter ─► RS/ROB/PRF/RAT/LQ/SQ .Reg    │
                  (译码+RAT/PRF读+6路first-fit+freelist)  ~45GE  │
                                                                │
 [B] 派发→执行链 ★最差                                           │
 RS.Reg + PRF.ready ─► DispatchArbiter(selectOldest 12项折链)    │
        ─► grant.rsIndex ─► RS tag ─► PRF 128:1 读值             │
        ─► ALU/AGU/BRU/MUL 运算 ─► slot.Reg          ~136 GE     │
                                                                │
 [C] 取指预测链                                                  │
 FetchUnit.PC.Reg ─► BPU.predict(GHR折叠+TAGE+BTB+RAS)           │
        ─► fetchOut ─► FetchUnit.Reg / FQ / ICache.hit ─► IMEM   │
                                            ~47 GE + 2×SRAM      │
 [D] LSQ 转发链                                                  │
 RS.Reg+PRF.Reg ─► SQ.planDataForward(16项CAM) ─► SQ.data        │
        ─► LQ.storeNotifies ─► LQ.Reg              ~14 GE        │
                                                                │
 [E] 访存链                                                      │
 LQ.Reg ─► LoadDetect(16) ─► MemArbiter ─► DCache.probe(4way)    │
        ─► DCache.Reg / DMEM                       ~25 GE        │
                                                                │
 [F] CDB 广播（扇出 20+，非瓶颈但高扇出）                          │
 ALU/AGU/BRU/MUL head* ─► *CDB ─► PRF(ROB.newPhy查表)+ROB+BPU    │
                                                                │
 [G] squash 网（扇出 20+）                                        │
 FlushArbiter.Reg ─► needSquash/SquashPC/SquashTag ─► 全模块      │
                            └────────────────────────────────────┘
```

---

## 3. P0 关键路径详解

### P0-1　Dispatch → 执行 单拍串联（`~136 GE`）—— **头号降频点**

**位置**：`src/StaticArbiter/StaticArbiter.cpp` `selectOldest()` / `aguSelect()`，
接线在 `src/CPU/CPU.cpp:434-503`（ALU/MUL/AGU/BRU 的 `src1Value/src2Value/op`）

```cpp
// CPU.cpp:437 —— ALU 的操作数在一拍内走完全程
ALUModule.src1Value = [this]() {
  const auto &d = DispatchArbiterModule.alu;              // ① 组合仲裁结果
  return d.valid ? PRFModule.getOperandValue(             // ③ 128:1 ×32b mux
             RSModule.getIntSrc1(d.rsIndex)) : 0u;        // ② 用 rsIndex 再读 RS
};
```

而 `aguSelect()` 的池是 `LOADRS(4) ++ STORERS(4) = 12 项`，`selectOldest` 是**带跨迭代状态的折链**：

```cpp
// StaticArbiter.cpp —— winner 逐级更新 ⇒ RTL 中 12 级串联
for i in 0..N-1:
    if (busy[i] && rdy1[i] && rdy2[i])          // rdy = PRF 128:1 查表
        if (!v || isOlder(tag[i], tag)) { v=1; tag=tag[i]; idx=i; }   // 顺序依赖
```

**级数拆解**：

| 段 | GE |
|---|---|
| PRF ready 位图 128:1 查表（每槽 2 次，可并行） | 8（并行，不计入链长） |
| 12 项折链 × (busy AND 2×ready ≈ 4 + isOlder 比较 ≈ 3 + mux ≈ 2) | **~108** |
| grant.rsIndex → RS 读 tag / op | 6 |
| PRF `getOperandValue` 128:1 ×32b | 10 |
| `ALU::evaluate` 最坏（SLT：减法 + 符号判断；SLL：32 位桶形 5 级 mux） | 12 |
| **合计** | **≈ 136 GE → 2.7 ns → 370 MHz** |

**根因**：这是**没有 issue→RF→EX 流水**的 1 拍通路。现代乱序核这里是 2–3 拍。

---

### P0-2　MUL stage3 的 64 位 CPA（`~128 GE 若行波`）

**位置**：`src/MUL/MUL.cpp:63-90`

```cpp
const uint64_t res = sOld + cOld;   // ← 64 位全进位传播
...
// 同一拍还要做 MUL_CAP(=4) 槽 first-fit 扫描 + 半字选择
for (int i = 0; i < MUL_CAP; ++i) { ... filled = i; ... }
```

`+` 在无约束综合下默认推断 **RCA = 64×2 = 128 GE**，与 P0-1 同量级，二者共同决定 fmax。
而 MUL 只需 32 位结果：`MUL` 取 `res[31:0]`，`MULH` 取 `res[63:32]`——**做了 64 位加法却只用一半**。

**stage 划分现状**（已 3 段流水，正确）：stage1 Booth radix-4 → stage2 CSA 树(19→2, 7 级) → stage3 CPA。
stage2 的 CSA 树只有 ~14 GE，不是瓶颈。

---

### P0-3　ROB 提交的 `seen[20]` 线性去重（`~84 GE`）

**位置**：`src/ROB/ROB.cpp:123-129`

```cpp
int seen[20]; int nSeen = 0;
auto markReady = [&](int slot) {
  for (int k = 0; k < nSeen; ++k) if (seen[k] == slot) return;  // ← 顺序扫描，跨调用状态
  seen[nSeen++] = slot;                                          // ← 顺序依赖
  ROBqueue[slot].isCommitReady <= true;
};
```

`markReady` 每拍最多调用 **1(BRU) + 8(MEMQ_SCAN_WINDOW) + 3(ALU/LQ/MUL CDB) = 12 次**，
每次扫最多 20 项 → 严格 12 级 × (6 位比较 5 GE + 控制 mux 2 GE) ≈ **84 GE**。

**这是纯软件写法**：`seen` 只为保证"同一 slot 单写"（满足 Register 单写口纪律）。
硬件里所有源都是**写 1**，天然幂等，根本不需要去重。

---

### P0-4　BPU 现场折叠 GHR（`~47 GE + 2×SRAM`）

**位置**：`src/BPU/BPU.cpp:11-23`（`refoldView`）、`288-375`（`predict`）

```cpp
for (int i = 0; i < TAGE_NTABLES; ++i) {          // 4 个表，hist = {6,12,24,48}
  idx[i]  = refoldView(ghr, h, 10) ^ p2;          // 48→10: 5 项 XOR 树
  tags[i] = refoldView(ghr, h, 8) ^ refoldView(ghr, h, 7) ^ p2;  // 48→8: 6/7 项 XOR 树 ×2
}
```

|TAGE_HIST|=48 的表要现场做 **3 棵 XOR 树**（5 项 + 6 项 + 7 项），每棵 ~6 GE，加索引 XOR/与/仲裁 ≈ 47 GE，
再串 **TAGE SRAM 读（1024×12b ×4）** 与 **BTB SRAM 读（256×32b）**。

业界标准解法是**折叠历史寄存器**（Seznec 的 `cs`/`cf`）：每拍随 GHR 移位做增量更新，
预测时**直接读寄存器**，把这段组合延迟降到 0。本项目每次预测都重算，是教科书级的 fmax 杀手。

---

### P0-5　SQ `planDataForward` 16 项环形 CAM（`~14 GE`，但重复展开 32 份）

**位置**：`src/SQ/SQ.cpp:78-146`，调用点 `src/CPU/CPU.cpp:1626-1689`

```cpp
// CPU.cpp:1627 —— 8 个输出字段各自独立调用一次 planDataForward，共 4 槽 ⇒ 32 次全展开
SQModule.data.valid[i]   = ... planDataForward(memSlot(...), getOperandValue(...)).valid
SQModule.data.storeTag[i]= ... planDataForward(...).storeTag
... （×8 字段 × 4 槽）
```

函数体是 16 项环形扫描 + 32 位地址比较 + "首次置位"优先语义（`FoundKnownSame` 顺序依赖）。

- **RTL**：综合器会 CSE 掉重复调用 → 实际 ~14 GE，**不是** fmax 瓶颈，但面积是 512 个 32 位比较器量级
- **仿真**：Wire 是 lazy 且**无 CSE**，32 次全量重算 → 这是仿真吞吐的主要黑洞之一（见 §7）

---

## 4. 全模块逐模块诊断（25 个 Module）

| # | 模块 | 关键路径贡献 | 估算 GE | 严重度 | 主因定位 |
|---|---|---|---|---|---|
| 1 | **DispatchArbiter** | 4 路 `selectOldest`，AGU 池 12 项折链 | **~108** | ★★★★★ | `StaticArbiter.cpp` `selectOldest/aguSelect` |
| 2 | **MUL** | stage3 64 位 CPA + 4 槽 first-fit | **~128** | ★★★★★ | `MUL.cpp:68,73-81` |
| 3 | **ROB** | `seen[]` 去重 + SQ 窗口扫描(8) + 64:1 mux | **~84** | ★★★★ | `ROB.cpp:123-148` |
| 4 | **BPU** | TAGE 现场折叠 + 双 SRAM + BTB/RAS/TC | **~47+SRAM** | ★★★★ | `BPU.cpp:11-23,298-362` |
| 5 | **ALU** | 32 位运算（SLT/SLL 最坏）+ 4 项最老扫描 | ~20（自身） | ★★★ | `ALU.cpp:5-33,55-90`；**被串在 P0-1 尾部** |
| 6 | **SQ** | `planDataForward`/`replyToLoadRequest`/`canDispatchLoad` 16 项 CAM | ~14 ×3 路 | ★★★ | `SQ.cpp:78-212` |
| 7 | **LQ** | `LoadDetect`/`CDBDetect` 16 项优先扫描（各被调用 6/4 次） | ~12 | ★★ | `LQ.cpp:75-110` |
| 8 | **IssueArbiter** | 译码 + RAT/PRF 读 + 6 路 first-fit(最长 8) + freelist | ~45 | ★★★ | `StaticArbiter.cpp` `wire_output` |
| 9 | **DCache** | `probe` 4-way tag 比较 + `allocateWay` + PLRU + 数据择路 | ~18 | ★★ | `DCache.cpp:69-110` |
| 10 | **MemArbiter** | LQ/SQ 条件汇聚 + 输出 mux | ~12 | ★★ | `StaticArbiter.cpp` |
| 11 | **FlushArbiter** | LQ 16 项违例扫描 + `robPC/robCkptId` 64:1 mux | ~20 | ★★ | `DynamicArbiter.cpp:172-201` |
| 12 | **PRF** | 128:1 ready/value 读；CDB 写口需 `ROB.newPhy[64:1]` 查表 | ~10（读）/ ~12（写） | ★★ | `PRF.hpp:71-87`, `CPU.cpp:624-664` |
| 13 | **RS** | 6 个 `tryAlloc*` first-fit（最长 8）+ 28 槽 release/flush 比较 | ~15 | ★★ | `RS.cpp:7-36` |
| 14 | **AGU** | 地址加法 + 4 项最老扫描 ×5 个 head* | ~18 | ★★ | `AGU.cpp:21-70` |
| 15 | **BRU** | 比较 + PC 加法 + 4 项最老扫描 | ~18 | ★★ | `BRU.cpp:42-95` |
| 16 | **ICache** | 512 项直接映射 hit（**串在 BPU 输出之后**） | ~8（附加在 C 链尾） | ★★ | `CPU.cpp:143-146` |
| 17 | **AluCDB / LqCDB / MulCDB** | 纯 mux + squash 年龄门 | ~8 | ★ | `CDB.cpp` |
| 18 | **RAT** | 32 项映射表读 + ckpt 恢复 | ~6 | ★ | `RAT.cpp` |
| 19 | **DecodeUnit** | 译码（组合，无循环） | ~5 | ★ | `Decoder.cpp` |
| 20 | **FetchUnit** | PC 选择 + halt 门 | ~4 | ★ | `FetchUnit.cpp` |
| 21 | **IMEM** | valid[] popcount 归约（**已按手法 #10 去计数**） | ~5 | ★ | `IMEM.cpp` |
| 22 | **FQ (InstructBuffer)** | 8 项队列 head/tail 组合 | ~5 | ★ | `InstructBuffer.cpp` |
| 23 | **DMEM** | 双口存储 + 倒计时 | ~5 | ★ | `DMEM.cpp` |

---

## 5. 其他降频因素（非时序但相关）

### 5.1 高扇出网络（fanout > 20）

| 信号 | 扇出 | 影响 |
|---|---|---|
| `flushArbiter.needSquash` | ~22 个模块/仲裁器 | 需缓冲树；布线拥塞 |
| `flushArbiter.SquashTag` (7b) | ~20 | 同上 |
| `SquashPC` (32b) | 2（FetchUnit/DCache） | 无 |
| `AluCDB.*` | PRF/ROB/BPU/FlushArbiter/ALU | 32b 数据总线扇出 5 |
| `IssueArbiterModule.core.valid` | ~8 | 中 |

**补救**：综合时设 `max_fanout` 约束 + 插入 buffer tree；RTL 侧把 `needSquash` 做成每模块本地寄存的"上一拍 squash 标志"（若语义允许）。

### 5.2 冗余 / 死逻辑（**零时序影响**，lazy Wire 不展开，但属代码债）

| 项 | 位置 | 说明 |
|---|---|---|
| `ROBModule.sq.sqHasOlderUnresolvedAddressStore[128]` | `CPU.cpp:1370-1376` + `ROB.hpp:85` | **128 个 Wire 无任何消费者**。lazy + 无负载 ⇒ 仿真/综合均零开销，但一旦被误接就是 128×16 CAM 的面积灾难。建议直接删 |
| `BPU::predict` 每拍调用 **2 次** | `BPU.cpp:395,406`（`mid.predPC` 与 `mid.packed` 各调一次） | 仿真侧 x2 开销；RTL 会 CSE。见 §7 |
| `LQ.LoadDetect()` 被调用 **6 次**、`CDBDetect()` **4 次** | `CPU.cpp:208-222,290-322` | 同上，纯仿真开销 |

### 5.3 结构性问题：缺少流水线站

当前**全部逻辑都在 1 拍内完成**（这是周期精确模型的必然，也是 fmax 低的根本原因）：

| 通路 | 现状 | 应有站数 |
|---|---|---|
| dispatch → RF read → EX | 1 拍 | 2–3 拍 |
| BPU predict → 取指 | 1 拍 | 1–2 拍（或预测缓存/上一拍预测） |
| LSQ 转发 → LQ 更新 | 1 拍 | 1–2 拍 |
| Issue 分配 → 写 ROB/RS | 1 拍 | 1–2 拍 |

---

## 6. 补救方案

### 第一档：零周期代价（**不改 clock，golden 不变，可立即做**）— 优先级最高

| 方案 | 针对 | 做法 | 收益 |
|---|---|---|---|
| **R1. `selectOldest` 二叉化** | P0-1 | winner 折链改成**并行前缀/锦标赛树**：先两两比 `isOlder`，log₂N 层。`req[i]` 的 ready 位图查表并行做在树外 | 108 GE → ~25 GE |
| **R2. MUL CPA 改前缀加法器** | P0-2 | `res = sOld + cOld` 不用 RCA：低 32 位 KS 加法 + 高 32 位双版本(cin=0/1) 由 cout 选择 | 128 GE → ~20 GE |
| **R3. ROB `seen[]` → one-hot OR 归约** | P0-3 | 所有 ready 源组成 64 位 one-hot（tag 译码），`isCommitReady[i] <= isCommitReady[i] \| or_onehot[i]`。**写 1 幂等，天然无冲突** | 84 GE → ~4 GE |
| **R4. BPU 折叠历史寄存器** | P0-4 | 新增 `cs[N]`/`cf[N]`（每表各 10b/8b/7b），随 `shiftGHR` 增量更新：`f <= (f<<1 \| newbit) ^ (f>>...)`；`predict` 直接读。squash 恢复随 `bpCkpt` 一起回卷 | 47 GE → ~12 GE |
| **R5. 重复调用 CSE** | P0-5, §5.2 | 在模块内加 `Inner` 级 Wire 或 `Plan` 缓存：`planDataForward` 结果只算 1 次，8 个字段从同一结果取；`LoadDetect/CDBDetect` 同理 | 仿真吞吐 +，RTL 面积 - |
| **R6. 删死线 `sqHasOlderUnresolvedAddressStore[128]`** | §5.2 | 删 `ROB.hpp:85` 字段 + `CPU.cpp:1370-1376` 接线 | 代码债清零 |

> R1/R3 需要**逐位等价验证**：R1 的等 tag 保持早槽（注释说 RTL 下是死逻辑）、R3 的写 1 幂等性，
> 都可用现有 `run_once_shuffle` + 双树 x10+clock 门禁兜住。

### 第二档：加流水站（**+1~2 拍 CPI 代价，需重写 `docs/benchmarks.md` 的 `cycles` 列**）

| 方案 | 针对 | 做法 | 代价 |
|---|---|---|---|
| **R7. dispatch→EX 拆 2 拍** | P0-1 | DispatchArbiter 输出打一拍"发射寄存器"（`issueReg`），下拍读 PRF + 执行 | ALU/AGU/BRU/MUL 各 +1 拍延迟；clock 全面上升 |
| **R8. BPU 预测打一拍 / 上一拍预测** | P0-4 | 取指 PC 提前一拍送 BPU，或做"预测结果寄存器 + 覆盖检查" | 分支分辨率敏感，clock 变化大 |
| **R9. MUL stage3 再切半拍** | P0-2 | 32 位 CPA 与 64 位 CPA 分拍 | MUL +1 拍（本就 3 拍 → 4 拍） |

> 这类改动会**改变 clock**。按 `AGENTS.md` 的 golden 政策：同一宏观架构下 clock 越小越好，
> 加站通常会让 clock 变差（IPC 降）但 fmax 升——**净收益 = fmax 提升% − clock 恶化%**，
> 建议先在 `gcd / hanoi / superloop / basicopt1` 上做 A/B 再决定。

### 第三档：架构级（**大改，留作后续**）

| 方案 | 说明 |
|---|---|
| **R10. PRF ready 位图 → 每槽预计算** | 现在每槽发射时查 128:1 位图 ×2；改为 RS 内维护 `src1Ready/src2Ready` 位，由 CDB 广播更新（广播时做 128 项 tag 比较，只算 1 次/拍而非 32 次） |
| **R11. age 比较改"年龄矩阵"** | `ROB::isOlder` 在折链里出现 N 次；改用 ROB 的 age 位（tag MSB 反转位 + 相等判断），每次比较降到 2 GE |
| **R12. SQ CAM → 分 bank / 只比地址高位** | 16 项全 32 位比较 → 先比 `addr[31:4]`（行粒度）命中再比低 4 位 |
| **R13. DCache 组相联 → 读 tag 与读 data 并行** | 现状 `probe` 串行于决策之后；改成 tag/data 阵列同拍读出、命中后择路 |

---

## 7. 附：仿真侧"降频"（模拟器吞吐），可选优化

`AGENTS.md` 记录：模块化后 WSL/Release ≈ **25.7 K 拍/s**（旧快照架构 ~8 min/pi，现 ~44 min，≈5×）。

主因与补救（**不影响 RTL 时序，纯 C++ 侧**）：

| 项 | 位置 | 说明 | 补救 |
|---|---|---|---|
| Wire lazy 但**无跨 Wire CSE** | 全局 | 同一函数在多个 lambda 里被重复求值 | 加 `Inner` 级 Wire 缓存（= R5） |
| `BPU::predict` ×2/拍 | `BPU.cpp:395,406` | `mid.predPC` 与 `mid.packed` 各一次 | 合成单个 packed 输出，只调 1 次 |
| `planDataForward` ×32/拍 | `CPU.cpp:1626-1689` | 4 槽 × 8 字段 | 同上 |
| `LoadDetect` ×6、`CDBDetect` ×4 | `CPU.cpp:208-222,290-322` | 每字段重算 | 同上 |
| 444 个 wire lambda（CPU.cpp）+ 117（StaticArbiter） | 全局 | 每拍大量 std::function 间接调用 | 高频路径换模板化 `Wire` 直连或预解析依赖 |

**预计收益**：R5 + predict 单例 保守估计可回 1.5–2× 吞吐（**需实测**，勿引用未验证数字）。

---

## 8. 落地顺序建议

```
Step 1  R6（删死线）+ R5（CSE）            零风险，先清场
Step 2  R3（ROB one-hot）                  84GE→4GE，收益最大、改动最小
Step 3  R2（MUL 前缀加法）                 独立模块，易验证
Step 4  R1（selectOldest 二叉化）          需 clock 逐位门禁
Step 5  R4（BPU 折叠寄存器）               需 clock 门禁 + BPU 准确率不退化校验
Step 6  R7/R8/R9（加流水站）               A/B 评估后再定，需重写 benchmarks.md
```

每步门禁（照 `AGENTS.md`）：
```bash
grep CMAKE_BUILD_TYPE build/CMakeCache.txt      # 必须 Release
cmake --build build && rm -f code && cmake --build build
./test.sh                                        # 18 用例（pi ~44 min）
VERBOSE=clock ./code < data/testcases/gcd.data 2>&1 >/dev/null | grep clock   # 双树比对
./test/reorder_test 20 < data/testcases/gcd.data
```

---

## 9. 不确定性声明

- 所有 GE / fmax 数字是**结构级估算**（按 §1 模型手推），**未经过真实综合**。
  要坐实必须出网表 + 时序报告；本报告的作用是把 probe 点提前指到位。
- SRAM 宏延迟（TAGE 1024×12b ×4、BTB 256×32b、DCache 1024 set ×4 way、ICache 512×16B）
  取决于所选工艺/编译方式（寄存器堆 vs 真 SRAM），未计入 GE，可能主导取指链。
- R1/R3 的"逐位等价"需 `run_once_shuffle` + 双树 x10+clock 双门禁确认后才可合并。
