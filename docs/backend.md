# 后端子系统：发射 · 乱序执行 · 写回 · 提交 · squash 恢复

> 负责乱序核心本体：从 IQ 发射（rename）→ 保留站就绪乱序执行 → 结果总线写回 →
> ROB 按序提交；误预测与记忆违例的排队、整窗恢复也在这里仲裁并触发。
> 保留站/标签广播源自 Tomasulo 算法，ROB 精确提交与物理寄存器重命名属于后续
> 现代化扩展 [[1]](#back-ref-1)[[2]](#back-ref-2)[[3]](#back-ref-3)。
> 相关实现：
> `StaticArbiter`（组合逻辑的静态仲裁器）、
> `RS`（去中心化保留站）、
> `PRF`（中心化物理寄存器堆）、
> `RAT`（基于物理寄存器实现的寄存器重命名表）、
> `ROB`（重排序缓冲队列）、
> `ALU`（基础运算单元）、
> `MUL`（3-cycle乘法器）、
> `DIV`（SRT-4除法器）、
> `AGU`（专供地址计算的运算单元）、
> `BRU`（专供分支运算的运算单元）、
> `CDB`（写回总线）、
> `DynamicArbiter`（时序逻辑的动态仲裁器）。

取指与译码在前端完成；访存队列（LQ/SQ）与缓存/主存在
[访存](memory.md) / [缓存](cache.md) 中描述。后端需要掌握"程序序边界"，因此
ROB 条目同时是前端预测 checkpoint 与 LQ/SQ 尾快照的宿主。

M 扩展的两个执行单元（**乘法**、**除法**）在 §4.3 / §4.4 给出实现细节
（乘法：Booth 编码部分积 + CSA 压缩树；除法：SRT radix-4）。

> 实现词汇：主树以周期初快照、组合总线和 `tick()` 表达时序更新；模板树以
> `Wire`、`Register` 和 `work()/sync()` 表达同一硬件语义。下文统一使用
> “组合选择”和“周期更新”，只在源码映射处标出两者的表示差异。

---

## 1. 边界与职责

```
 IQ（前端）─► IssueArbiter ─ rename ─► RS 占槽 / ROB push / PRF alloc / RAT 改名
                                      │    （含 LQ/SQ push，见 memory.md）
              ┌───────────────────────▼───────────────────────┐
              │ RS ──DispatchArbiter──► ALU · MUL · DIV · AGU · BRU │
              │        （乱序派发，就绪即发）                   │
              └───────────────────────┬──────────────────────┘
                                      ▼
     aluCDB / mulCDB / divCDB / lqCDB ──► PRF 完成写口 / ROB 置位 / 训练
                                      ▼
              ROB 按序提交 ──► FlushArbiter（误测/MDP 排队、最老优先）
```

| 结构/模块 | 职责 |
|------|------|
| `IssueArbiter`（StaticArbiter） | 组合构建每周期至多 1 个发射包（rename 决策） |
| `DispatchArbiter`（StaticArbiter） | 保留站 → 执行单元的乱序派发（五独立通道） |
| `RS` | 七个物理池：Integer 8 / Multiply 4 / **Divide 4** / Load 4 / StoreAddr 4 / StoreValue 4 / Branch 4 |
| `PRF` | 物理寄存器堆 128：循环序号自由表、完成写口、checkpoint 恢复 |
| `RAT` | 架构寄存器 → 物理寄存器映射 |
| `ROB` | 重排序缓冲 64：按序提交、checkpoint 快照宿主、squash 边界 |
| `ALU` | 算术/逻辑/移位 + JALR 目标（`isControl` 载荷） |
| `MUL` | M 扩展乘法单元（Booth → CSA → 最终加，3 级流水）——见 §4.3 |
| `DIV` | M 扩展除法单元（SRT radix-4，单实例迭代，非流水）——见 §4.4 |
| `AGU` | 访存地址计算（load/store；最老有效地址结果若为 store 则广播给 SQ） |
| `BRU` | 条件分支执行 |
| `CDB` | 四路结果总线载荷与门控（aluCDB/lqCDB/mulCDB/**divCDB**） |
| `FlushArbiter`（DynamicArbiter） | squash 请求队列：检测（BRU 误测/CDB 误测/MDP）、最老优先 |

> `RS` 的类别计数在 `RS.hpp` 中为 `integerRS[INTEGERRS_CAP]`、`multiplyRS[MULTIPLYRS_CAP]`、
> `divideRS[DIVIDERS_CAP]`、`loadRS`、`storeAddressRS`、`storeValueRS`；
> `Operation` 枚举含 `MUL/MULH/MULHU/MULHSU/DIV/DIVU/REM/REMU`；
> `RSType` 含 `Integer/Multiply/Divide/Branch/Load/StoreAddr` 六种**执行派发类型**；
> `StoreValue` 是第七个物理 RS 池，但数据就绪后直接广播给 SQ，不占执行通道；
> 逻辑派发总线含 `alu/bru/mul/div` 四个普通载荷，以及额外携带 `rsType` 的
> `agu` 载荷。

---

## 2. 发射（Issue / Rename）

`IssueArbiter` 的组合逻辑从 **IQ 头**解析一条指令，构造本周期发射决策：

- **容量门控**：ROB/PRF 自由表/对应 RS 类别/LQ/SQ 同时有空位才发射；
- **rename**：`allocDest` 时 PRF 分配新物理寄存器（`phy`），RAT 建立新映射，
  旧映射记入 ROB 条目（`oldPhy`）供提交释放；
- **操作数解析**：`Operand{tag, imm}`——立即数用 `tag == InvalidPhy` 编码，
  寄存器操作数记录其当前物理标签；
- **哨兵域**：`InvalidPhy = 0` 为全物理域唯一哨兵（P0 永不分配、永不映射）；
  真实物理标签恒在 `1..PRF_CAP-1`；`x0` 恒 0、不参与 rename。

发射决策由各状态模块在周期边界**各自 apply**（写自有纪律）：RAT 改名 / PRF `pop`+LINK /
ROB push / RS 占槽 / LQ/SQ push（访存指令）/ IQ pop。每周期**至多发射 1 条**
（单口 rename + 单口 ROB push 的硬件约束）。

**M 扩展的分类落点**（`decodeOp`，`StaticArbiter.cpp`）：`funct3` **0..3** 归乘法族
（`mul/mulh/mulhsu/mulhu`）→ 专用 `multiplyRS`；`funct3` **4..7** 归除法族
（`div/divu/rem/remu`）→ 专用 `divideRS`。`tryAllocDivide()` 分配槽位，
`p.divideRS.{op,src1,src2,robTag}` 填包。**两个单元各有一个专用保留站类别**，
因此 M 扩展的发射不占用 Integer RS 的槽位。指令语义与除零/溢出规则以 RISC-V
“M”扩展规范为准 [[4]](#back-ref-4)。

---

## 3. 物理寄存器与就绪模型

- PRF 以**循环序号**（headSeq/tailSeq）管理自由表，条目 `{value, ready}`；
- **保留站不缓存值**：就绪判定 `isOperandReady` 与取值 `getOperandValue` 直接
  查询 PRF（或返回立即数）。依赖唤醒的延迟表现为：结果经 CDB 写入 PRF 的下一
  拍，依赖它的保留站自然就绪——**总线数量不再是依赖链长度的上限**；
- 提交时释放 `oldPhy` 回自由表（循环序号语义天然支持 checkpoint `restoreHead`）。

---

## 4. 派发与执行

### 4.1 派发（Dispatch）

`DispatchArbiter` 每个周期给每个执行单元**独立**选一个就绪候选
（容量/接收门控 + 就绪 + 过 squash 门），五通道互不阻塞：ALU / MUL / DIV / AGU / BRU
各一个派发载荷。访存指令的地址就绪由 AGU 执行，store 数据就绪由
StoreValue RS 提供（见 [访存](memory.md) §2）。

**DIV 通道的背压**（`StaticArbiter.cpp` 的 DIV 分支）：先问 `div.canAccept()`，
再在 `divideRS` 里扫**最老**的就绪条目，选出后置 `dispatch.div.{rsIndex,robTag,rsType=Divide,valid}`；
命中 squash 窗口则撤销。因为除法器是**单实例迭代单元**，`canAccept()` 为假时整条
DIV 通道停发；MUL 则只检查自己的输出缓冲是否已满（见 §4.3）。

### 4.2 执行单元总表

| 单元 | 输入候选 | 结果保持 | 行为 |
|------|----------|----------|------|
| `ALU` | Integer RS 8 | 4 槽输出缓冲 | 算术/逻辑/移位；**JALR** 目标计算（`isControl` 载荷，经 aluCDB 供 FlushArbiter/BPU 消费） |
| `MUL` | Multiply RS 4 | 3 级流水 + 4 槽输出缓冲 | RV32M 乘法族：radix-4 **Booth** 19 行部分积 → **3:2 CSA 压缩树**（17 cell）→ 最终全宽加法 [[5]](#back-ref-5)[[6]](#back-ref-6)[[7]](#back-ref-7)。**详见 §4.3** |
| `DIV` | Divide RS 4 | 单个 `resultValid` 结果寄存器 | RV32M 除法族（`div/divu/rem/remu`）：**SRT radix-4** 数字递推，carry-save 冗余表示 + 常数法 QDS + on-the-fly 商转换 [[9]](#back-ref-9)[[10]](#back-ref-10)[[12]](#back-ref-12)[[13]](#back-ref-13)；**单实例、非流水**（三阶段 valid 归约拒绝新指令）。**详见 §4.4** |
| `AGU` | Load RS 4 + StoreAddr RS 4 | 4 槽输出缓冲 | load/store 地址 = base+offset；最老有效结果为 store 时组合广播地址事件 |
| `BRU` | Branch RS 4 | 4 槽输出缓冲 | 条件分支：最老有效 `BranchResult{pcFrom, pcResult, robTag}` 供误预测检测 |

派发不是 FIFO：每个通道扫描相应 RS，按 `robTag` 选择**最老的就绪项**；AGU
在 Load 与 StoreAddr 两池之间统一比较年龄。ALU/MUL/AGU/BRU 的 4 槽输出缓冲
同样按 `robTag` 选最老有效结果，而不是按插入槽位或固定队首；DIV 只有一个结果
寄存器。四路结果总线据此保证“每源每周期至多一个结果”。

### 4.3 MUL 实现：radix-4 Booth + CSA pseudo-Wallace Tree

本节的编码基础是 Booth 有符号乘法与 MacSorley 的 modified/radix-4 Booth
重编码 [[5]](#back-ref-5)[[6]](#back-ref-6)；固定的 3:2 压缩网络属于
Wallace-style CSA 树 [[7]](#back-ref-7)[[8]](#back-ref-8)，本文的 19→2 拓扑与流水级
划分是项目实现，不宣称复现某一篇论文的门级树形。

采用一条**三级流水线**实现 3-cycle 乘法：

```
第 1 级  calculateBooth(dispatch)      → 19 行部分积
第 2 级  calculateSC(partialRes)       → (S, C)
第 3 级  calculateMulRes(scRes)        → outputBuffer[slot]（结果 + robTag）
```

周期更新只读各级的旧状态并同时前推。主树在 `tick()` 内逆序调用
`calculateMulRes` → `calculateSC` → `calculateBooth`；模板树在 `work()` 中读取
`Register` 旧值达到同样效果。空级会清 valid，避免气泡被误当作结果。
`flush(tag)` 同时清 `partialRes/scRes` 的 valid 与 `outputBuffer` 中不早于 tag 的槽位。

**(a) 部分积的 19 行**（`PartialProductResult::partialProduct[19]`，全部 64 bit 全宽）：

| 行 | 内容 |
|---|---|
| `[0..15]` | `op2` 的 16 个 radix-4 Booth 数字行。3 bit 窗口 `y[2i+1],y[2i],y[2i-1]`，row 0 补 `y[-1]=0`；数字 `+1/+2/-1/-2/0` 分别取 `A` / `A<<1` / `~A` / `~(A<<1)` / 0，行内**左移 `2i`** 落位 |
| `[16]` | `MULHU && signA` 时的无符号修正：`(uint32)op2 << 32` |
| `[17]` | `(MULHU ‖ MULHSU) && signB` 时的修正：`(int64)op1 << 32` |
| `[18]` | **稀疏 +1 补位行**：每个负数字行在列 `2i` 置 `1`，把该行的反码补成补码（经典 Booth neg bit） |

> **全 64 bit 域运行 ⟹ 不需要"截断字段还款行"**：反码本身自带符号扩展，
> `partialProduct[18]` 承担的那个 `+1` 就足以完成二补数转换。

**(b) 3:2 CSA 与压缩树**（`csa3`）：

```cpp
// 3:2 compressor: sum = a^b^c, carry = majority(a,b,c) << 1
r.sum = a ^ b ^ c;
r.carry = ((a & b) | (a & c) | (b & c)) << 1;   // ← 括号必需：`|` 优先级低于 `<<`
```

压缩树 `19 → 13 → 9 → 6 → 4 → 3 → 2`，共 **17 个 `csa3`**（源码里命名为 a0–a5 / b0–b3 /
c0–c2 / d0–d1 / e0 / f0），末级输出 `scRes.S / scRes.C`。

**(c) 结果截断与写回**：`res = sc.S + sc.C`（全 64 bit 乘积，模 $2^{64}$）。
- `MUL` → `(int32_t)res`（低 32 位）；
- `MULH / MULHSU / MULHU` → `(int32_t)(res >> 32)`（高 32 位）。

结果写入任一空闲 `outputBuffer` 槽（`slotValid[]` 标位），输出选择逻辑按
**最老** robTag 取唯一的 mulCDB 写回候选；槽位本身没有 FIFO 次序。

**(d) 为什么 MUL 不需要背压门控**：`MUL_CAP=4` $>$ 在飞指令数（3 级流水 $\le3$），
且 `cdbOfMul` 每拍排空最老结果（被 squash 清掉的结果除外），故槽位永远够用 ——
派发通道只需 `!mul.isFull()` 这一道门（`StaticArbiter.cpp` 的 MUL 分支），
MUL 侧**不提供 `canAccept()`**。`calculateMulRes` 里那个
`assert(best != -1 && "MUL slot overflow: MUL_CAP must exceed in-flight stages")`
就是这条不变量的运行时守护：缓冲满了即代表派发跑得比总线排空快。
**任何可能破坏该不变量的改动（缩 `MUL_CAP`、给流水加级），都必须先保留这条断言。**

### 4.4 DIV 实现：SRT radix-4

> **SSOT 边界**：本节与 §4.5 是 SRT 算法、位宽和参数的设计 SSOT；各树的
> `src/DIV/DIV.cpp` 与 `src/include/DIV.hpp` 是可执行实现的源码 SSOT。主树用
> `uint64_t` 保存 35/36-bit 中间量并在 `tick()` 更新，模板树受 32-bit `Register`
> 上限约束而拆成 lo/hi 寄存器并在 `work()/sync()` 更新；两者实现同一递推和周期接口。

#### 4.4.1 几何与参数

SRT 数字递推、冗余商数字集与收敛区间的经典来源见 Robertson、Tocher 以及
Ercegovac/Lang [[9]](#back-ref-9)[[10]](#back-ref-10)[[12]](#back-ref-12)；以下位宽、
首拍奇偶处理、共享 ulp 判据与常数配置均是本项目针对 RV32M 的具体推导。

递推 $W[j+1]=4\,W[j]-q_{j+1}D$，数字集 $q\in\{-2..2\}$，冗余因子

$$\rho=\frac{a}{r-1}\Big|_{a=2,\,r=4}=\frac23,\qquad
\text{不变式 } \lvert W[j]\rvert\le\rho D$$

**重叠区 $(2\rho-1)D=D/3$ 是整个截断方案的容错预算**——落进去取哪个数字都对，
这是"常数法 QDS 可行"的唯一来源。商数字数 $k=\lceil align/2\rceil+1$，满宽
$align=31$ → **17 个商数字**。`prepare` 已产生 $q_1$，所以同构 `loop` 迭代次数是
$k-1=\lceil align/2\rceil$；当 $align=0$ 时该次数确实为 0，但仍要经过前置、收尾
和结果广播，不能把“0 次迭代”写成“0 周期延迟”。

| 项 | 取值 |
|---|---|
| 基数 / 数字集 | $r=4$（每拍 2 bit），$q\in\{-2..2\}$，$a=2$ |
| 冗余因子 / 重叠 | $\rho=2/3$；重叠宽 $D/3$ |
| 拍数 | $k=\lceil align/2\rceil+1$；$align=\mathrm{clz}(d)-\mathrm{clz}(x)$ |
| 首拍输入 | $P_1=X\cdot2^{-shiftD}$（$shiftD=align\&1$），由 $D_{\text{dp}}=D\ll shiftD$ 无损实现 |
| 截断对象 | 只截 QDS 的**输入**（$P$、$D$）；$q$ 不截；**$W$ 递推通路绝不截** |
| QDS 硬件 | 4 次 $\le9$ bit 比较，**无 ROM、无乘法、无除法** |
| 全长进位传播 | **迭代环路 0 次**；后处理 1～2 次 |

#### 4.4.2 三类位宽的辨析：$B_d$、$SW$、$PW$

$$\boxed{B_d=\text{位宽}(D_{\text{dp}})=32+shiftD}\qquad SW=B_d+1\qquad PW=SW+2=B_d+3$$

| 量 | 位宽 | 说明 |
|---|---|---|
| $D=d\ll clzD$ | 恒 32 | 归一化后 bit31=1 |
| $D_{\text{dp}}=D\ll shiftD$ | $\mathbf{B_d}$（32/33） | 数据通路实际使用的除数 |
| $W=S+C$（**只在 $W$ 域，不进寄存器**） | $SW=B_d+1$ | $W$ 域的补码宽度，**不是任何寄存器的位宽** |
| $P=4W$（**$(S,C)$ 实际持有的值**） | $\mathbf{PW=SW+2=B_d+3}$（35/36） | $P$ 域：左移 2 位 ⟹ 位宽 $+2$ |

⚠️ $B_d$（数据通路除数位宽，整数）与 on-the-fly 的 $B$ 寄存器（商的第二副本）**撞名**，
靠上下文区分。

$dSlice$ 取 $D_{\text{dp}}$ 的前导 1 及其后 4 位：$dSlice=(D_{\text{dp}}\gg(B_d-5))\ \&\ 31$，
**恒在 $[16,31]$**（5 bit 最高位必为 1），整次除法不变。相对误差 $<1/16$。

> 🟢 **移位量其实可以写死**：恒等式 $(D\ll shiftD)\gg(27+shiftD)=D\gg27$（低 $shiftD$ 位恒 0，
> 右移无损），故**在归一化 $D$ 上取 $dSlice$ 时移位量恒为 27**，与 $shiftD$ 无关（实测
> 156,513 组逐组一致）。必须随 $shiftD$ 变的是 $estP$ 的 ulp 与切片位置，不是 $dSlice$。
> 但若把 $D_{\text{dp}}$ 上的移位量写死 27（`Ddp >> 27`）则错 76,828 组。

> 🔴 **常见错误：把 $B_d$ 写死成 32**。$shiftD=1$ 时判据整体偏移一位 = 把除数看小一半，
> **结构性后果 = $shiftD{=}1$ 档必错**：最小反例 $7\div2$ 的 $dSlice$ 由 16 塌成 **0**，
> 判据退化成"永远选 $+2$"。

#### 4.4.3 首拍奇偶：$2^{-shiftD}$ 往哪放

数字权重是 **4 的幂**，而商的二进制标度 $2^{align}$ **只有 $align$ 偶时才是 4 的幂**；
$align$ 奇时必然多出一个因子 $2^{-1}$，总得落到别处——**这不是精度问题，是整除对齐问题**
（实测 $align=0..31$ 逐个核对 $2^{align+shiftD}=4^{k-1}$，**32/32 成立**）。

$$\frac{2^{align}}{4^{k-1}}=2^{-shiftD}\quad\Longrightarrow\quad \boxed{P_1=X\cdot2^{-shiftD}}$$

三个可落位置（同一套 CSA/截断 QDS/on-the-fly，只改这一个变量）：

| 变体 | 落点 | 实现 | 位宽 | 实测 |
|---|---|---|---|---|
| **V0（定稿）** | **分母** | $D_{\text{dp}}=D\ll shiftD$，$P_1=X$ | $B_d=33$，$PW=36$ | ✅ **0 错**（仅 +1 bit 宽度，零额外逻辑） |
| V1 | 分子 | $P_1=X\gg shiftD$ | $B_d=32$ | ✗ 只错在"$shiftD{=}1$ 且 $X$ 为奇"档（偶 $X$ 时逐拍同构） |
| V2 | 输出（裸） | 照 $shiftD=0$ 走，末了 $\gg shiftD$ | $B_d=32$ | ✗ 商 0 错、余数在"绕圈"档必错 |
| V2b | 输出（补救） | 再把 $(R\&1)\cdot D$ 补进余数 | 同上 | ✅ 0 错，但多一个 32 bit 加法 |
| V3 | 都不放 | 数据通路照 $shiftD=0$ | $B_d=32$ | ✗ $shiftD{=}1$ 档 **100%** 错 |

> V0 是**唯一"零额外硬件"的精确解**——代价只有 $B_d=32+shiftD$ 这 1 bit 位宽。

**"都不放 + 末了把商 >>1"行不行？商行、余数不行。** 都不放 ≠ 结果整体 ×2，而是
**被除数被换成了 $2x$**：整条通路实际在做 $2^{shiftD}x/d$（实测恒等式 156,513 组 0 例外）。
余数是**取模**，翻倍之后会**绕圈**：$(2x)\bmod d=2\,\mathrm{rem}-d\cdot[2\,\mathrm{rem}\ge d]$
⟹ $2\mathrm{rem}<d$ 档 `R>>1` 恰好还原（0 错），$2\mathrm{rem}\ge d$ 档**必错**（偏差恒 $\lceil d/2\rceil$）。
最小反例 **$5\div2$**：通路算 $10\div2$ → 商 5、**余数 0**（翻倍把余数"吃"掉了）；
`>>1` 后商 $2$ ✓、余数 $0$ ✗（真余数 1）。补救 = $\mathrm{rem}=(R_{\text{naive}}+[Q_{\text{naive}}\&1]\cdot d)\gg1$，
实测 0 错，但那是**一个 32 bit 条件加法器**，比 V0 的 1 bit 位宽贵得多。

**首拍不左移**（先减后移）：两种奇偶下 $P_1/D\subset(-8/3,8/3)$，**同一套判据直接吃下**。

#### 4.4.4 QDS：常数法 + 共享 ulp

截断部分余数/除数进行高基数商数字选择的理论背景见 Atkins 与
Ercegovac/Lang [[11]](#back-ref-11)[[12]](#back-ref-12)。本节的 5-bit `dSlice`、
9-bit `estBits` 和四比较器常数法是项目穷举后冻结的实例。

不想比较 $P$ 与 $(q-\rho)D$（ρ 是分数，要乘法器），改为判 $P$ vs $mid_kD$，窗口
$mid_k\in[k-\rho,\ k-1+\rho]$，**窗口宽 = 重叠宽**。取中点 $mid_k=k-\frac12$（与 $\rho$ 无关、
离两端最远、唯一最优），判据退化为四舍五入：

$$q_{j+1}=\mathrm{clamp}\big(\mathrm{round}_{\frac12}(P/D),-a,+a\big)\iff 2P\ \text{vs}\ (2k-1)D$$

radix-4 只需 $D,3D$ 两个奇倍数 ⟹ **4 次比较**。

**共享 ulp 判据**：让 $estP$ 与 $dSlice$ 共用同一个 ulp $=2^{\,B_d-5}$，判据**恒为**
$2\,estP$ vs $\pm dSlice,\pm3\,dSlice$：

| 条件 | $q$ |
|---|---|
| $2\,estP\ge\ \ 3\,dSlice$ | $+2$ |
| $2\,estP\ge\ \ \ \ dSlice$ | $+1$ |
| $2\,estP\ge-dSlice$ | $0$ |
| $2\,estP\ge-3\,dSlice$ | $-1$ |
| else | $-2$ |

```cpp
// DIV 单元内部 —— 不得出现 * / %
const int  Bd = 32 + shiftD;                      // 运行时决定（每条指令不同）
constexpr int bitsD = 5;                          // D 参与 QDS 的位宽（设计常量）
constexpr int estBits = 9;                        // 估计保留位数（定稿 9；穷举下界 8）
constexpr int estPShift = estBits - bitsD - 3;    // 定稿 = +1，即 estP = slice >> 1
u32 dSlice = (Ddp >> (Bd - 5)) & 0x1Fu;           // 5 bit，第 0 拍算一次，整次除法不变

// 每拍（寄存器装的是 P=4W：恒按 P 域 PW-estBits 切，没有首拍例外、没有域切换）
u32 Ah = (S >> (PW - estBits)) & ((1u << estBits) - 1);
u32 Bh = (C >> (PW - estBits)) & ((1u << estBits) - 1);
i32 estP  = (estPShift >= 0) ? (i32)(fold(Ah + Bh, estBits) >> estPShift)
                             : (i32)(fold(Ah + Bh, estBits) << (-estPShift));
i32 L  = estP << 1, T3 = dSlice + (dSlice << 1);
int q  = (L >= T3) ? 2 : (L >= (i32)dSlice) ? 1
       : (L >= -(i32)dSlice) ? 0 : (L >= -T3) ? -1 : -2;
```

**为什么"共享"是正确性而非余量**：$2P\gtrless(2k-1)D$ 是**比值**判据，两端必须同除
**同一个**数。若刻度差 $\lambda$，整条判据的阈值被乘 $\lambda$，等效于把除数读成 $D/\lambda$
——实测 $\lambda=2$ 时 $q$ 非法占比 **49.3%**、$\lambda=4$ 时 75.2%，只有 $\lambda=1$ 是 **0%**。
（结构性结论：非法 $v=P/D$ 区间在 $\lambda=2$ 时是 $\pm(\frac23,1)\cup\pm(\frac53,\frac83]$；
百分比是分布相关的，引用必须带采样描述。）

**为什么刻度挂在 $B_d$ 上**：归一化保证 $D_{\text{dp}}$ 的 MSB 恒在 bit $B_d-1$，右移 $B_d-5$
恰好剩 5 位且 MSB=1 ⟹ $dSlice\in[16,31]$ **与 $shiftD$ 无关**。只共享、不挂 $B_d$
（两边都钉 $2^{27}$）判据仍精确，但 $shiftD{=}1$ 档量程整体翻倍 ⟹ 比较器要写两套系数，
正是"Pentium 类漏格"的温床。

**一次拿三样**：判据一套（与首拍奇偶解耦）、位宽一套（$dSlice$ 恒 5 bit、$3dSlice$ 恒 6 bit、
$estP$ 恒 8 bit signed、4 次 $\le9$ bit 比较）、余量分析一次。**代价 0 条额外的线**
（随 $shiftD$ 动的只有切片位置，一根 1 bit 选择线，而 $shiftD$ 本来就要算）。

#### 4.4.5 每拍成本

| 项 | 结论 |
|---|---|
| **加法** | 必有——$estBits+1$ 位估计加法器，是 CSA 的伴随成本。实测约 **42% 的迭代真的溢出 $estBits$ 位**，那个 `& ((1<<estBits)-1)` 不是摆设 |
| **可变移位** | **不存在**。$shiftD$ 进循环前算一次、迭代里只读不写 ⟹ 一次性 MUX（随 $shiftD$ 变的只有切片起始位，差 1 位） |
| **$estP$ 的右移** | **可以完全消掉**。$2\lfloor slice/2^{estPShift}\rfloor\ge T\iff slice\ge2^{estPShift}\lceil T/2\rceil$，右边全是编译期常量 ⟹ 阈值预乘即可（端到端 0/20,408 差异） |

> ⚠️ $\lceil T/2\rceil$ 对 $\pm T$ **不对称**（$dSlice=31$ 时 $\lceil31/2\rceil=16$ 而
> $\lceil-31/2\rceil=-15$），负阈值必须单独算；写成 $-2^{estPShift}\lceil|T|/2\rceil$ 会系统性错一格。
>
> ⚠️ $shiftD$ **不能每拍重算**：迭代中 $|P|\le\frac83D_{\text{dp}}>D_{\text{dp}}$，$\mathrm{clz}(P)$ 会漂，
> 重算等于丢归一化 —— 这正是它必须锁存的原因。
>
> ⚠️ **切片必须两套**。把切片位置写死成 $shiftD=0$ 那套：$shiftD=1$ 档 7,697/10,038 组
> （76.7%）端到端错，而 $shiftD=0$ 档 0/10,241。

**每拍关键路径** = 9 bit 估计加法器 + 9 bit 比较 + MUX + 1 级 CSA，**$O(1)$，与操作数位宽无关**。

#### 4.4.6 迭代体：自足伪代码（口径 = 实现）

命名规则：**代码名照搬公式符号**（$B_d\to$`Bd`、$D_{\text{dp}}\to$`Ddp`、$P_1\to$`P1`）。
`DIV.cpp` 的 `prepare + loopTimes=(align+1)>>1` 即此口径。

```cpp
// ── 阶段 0：算法前置（每条除法一次；实现中由 receive/prepare 锁存）────
x = |x0|;  d = |d0|                          // 取幅值；符号留到阶段 2
clzX = clz(x);  clzD = clz(d);  align = clzD - clzX;  shiftD = align & 1
k   = ((align + 1) >> 1) + 1                     // 总拍数 = ⌈align/2⌉+1
Bd  = 32 + shiftD;  PW = Bd + 3                   // 位宽；SW 仅文档标签，不进代码
D   = d << clzD                                 // 归一化除数（恒有 D[31] = 1）
Ddp = D << shiftD                                 // 数据通路除数 = 2^shiftD·D
P1  = x << clzX                                 // 首拍入口 = X，**不右移**（先减后移）
dSlice   = (Ddp >> (Bd - 5)) & 31                 // 5 bit ∈[16,31]，整条除法不变
MASK  = (1ull << PW) - 1                     // CSA 回卷掩码（PW 位，硬下界）
fold(value, width) = ((value >> (width-1)) & 1) ? (value & ((1ull<<width)-1)) - (1ull<<width)
                                        : (value & ((1ull<<width)-1))
                                             // **先取模、再折叠**（width 位补码 → 有符号）

// ── 阶段 0 尾：吃掉 q₁，并把 (S,C) 种成 P₂（寄存器持 P = 4W）──────────
slice = fold(P1 >> (PW - estBits), estBits);  estP = slice >> 1     // 切片位与迭代拍**同一个**
L  = estP << 1;  T3 = dSlice + (dSlice << 1)     // 2·estP 与 3·dSlice，纯移位
q = (L >= T3) ? +2 : (L >= dSlice) ? +1 : (L >= -dSlice) ? 0 : (L >= -T3) ? -1 : -2
qd = |q| * Ddp                               // q₁·Ddp ∈ {0, Ddp, 2Ddp}
S  = (q == 0) ? (P1 << 2) & MASK
              : ((P1 ^ ~qd ^ 1) << 2) & MASK // a-b = a + ~b + 1；+1 折在移位**之前**
C  = (q == 0) ? 0
              : (((P1 & ~qd) | (P1 & 1) | (~qd & 1)) << 3) & MASK
q > 0 : A = q     ;  B = q - 1               // on-the-fly 自 q₁ 起步
q = 0 : A = 0     ;  B = 3
q < 0 : A = 4 + q ;  B = 3 + q               // 本方案 q₁ ≥ 0，此行不可达
// 至此 S + C = 4(P₁ − q₁·D_dp) = P₂ —— 那个 ×4 就是"寄存器持 P"的全部后果

// ── 阶段 1：迭代（k-1 = ⌈align/2⌉ 拍；**每拍完全同构，没有首拍特例**）──────
for j = 2 .. k:
  // ① 估计：寄存器本来就是 P，切片恒取 PW-estBits —— **全流程不存在域切换**
  slice = fold((S >> (PW - estBits)) + (C >> (PW - estBits)), estBits)   // 兼做进位丢弃
  estP = slice >> 1                                       // estPShift = estBits-bitsD-3 = 1
  // ② 判决：4 次 ≤9 bit 比较，无 ROM、无乘法
  L  = estP << 1;  T3 = dSlice + (dSlice << 1)
  q = (L >= T3) ? +2 : (L >= dSlice) ? +1 : (L >= -dSlice) ? 0 : (L >= -T3) ? -1 : -2
  // ③ 递推：一级 3:2 CSA；**减数带全局 4 倍**（P = 4W 的直接后果）
  qd = |q| * (Ddp << 2)                            // {0, 4D_dp, 8D_dp}：纯移位 + MUX
  S4 = (S << 2) & MASK;  C4 = (C << 2) & MASK      // 每拍都左移 2 位（首拍也不例外）
  T  = (q > 0) ? (MASK ^ qd) : qd                  // 负数取补；+1 走 C 的 bit0
  S  = (S4 ^ C4 ^ T) & MASK
  C  = (((S4 & C4) | (S4 & T) | (C4 & T)) << 1 | (q > 0)) & MASK
  // ④ 转换（与 ③ 并行）：三段拼接，字段恒 ∈[0,3]
  q > 0 : A' = (A<<2) + q      ;  B' = (A<<2) + (q-1)
  q = 0 : A' = (A<<2)          ;  B' = (B<<2) + 3     // 双特殊：源换 B、字段取 3
  q < 0 : A' = (B<<2) + (4+q)  ;  B' = (B<<2) + (3+q)

// ── 阶段 2：收尾（迭代环路 0 次全长 CPA；本阶段 1～2 次）────────────
Pk = fold(S + C, PW)                             // = 4·W[k]；第 1 次全长进位传播
(Q, R) = (Pk >= 0) ? (A, Pk) : (B, Pk + (Ddp << 2))   // 商余必须配套修正
rem = ((R >> 2) >> shiftD) >> clzD                       // 先把 P 域的 /4 折回来，再对阶
```

**位宽与类型速查**（照抄用；两个掩码职责不同，见 §4.6 陷阱二）：

| 代号 | 位宽 · 类型 | 纪律 |
|---|---|---|
| `S, C, S4, C4, T` | $PW$（35/36）· `u64`（**须 $\ge PW$**） | **寄存器持 $P=4W$**；每拍 `& MASK` 回卷；$(S,C)$ 只在模 $2^{PW}$ 下有定义 |
| `MASK`（回卷） | $PW$ 位，$2^{PW}-1$ | **硬下界 = $PW$，一位不能少**（临界值恰为 $PW$，与 $SW$ 无关） |
| `slice` | $estBits$ · `u32` | `fold(slice, estBits)` **内含取模** ⟹ 进位自然丢弃（陷阱二） |
| `estP` | 8 有符号 · `i32` | 域内 $\lvert estP\rvert\le86$；定稿 `estP = slice >> 1` |
| `q` | 3 有符号 | $\{+2,+1,0,-1,-2\}$ |
| `dSlice` / `3dSlice` | 5 / 6 · `u32` | 与有符号的 $2\,estP$ 比较前须显式同号 |
| `A, B` | $\le32$ · `u32` | on-the-fly 从高位往低位构造，中间值不超最终商 |
| `S` 的切片 | — | **在 P 域切**：恒取 $S\gg(PW-estBits)$；按 $SW-estBits$ 切则 $slice$ 读数整体 ×4（陷阱一） |
| `qd` | $PW{+}2$ · `u64` | 迭代拍 $\lvert q\rvert\cdot4D_{\text{dp}}\in\{0,4D_{\text{dp}},8D_{\text{dp}}\}$；$q>0$ 时取 `MASK ^ qd` |

**CSA 的两处实现要点**：

- **负号的 `+1` 由 `cin` 承担，不需要减法器**：硬件做 $S_4+C_4+(\overline{qd}+1)$，$qd$ 全是
  移位 + MUX（$q<0$ 直接给正的 $qd$，**不取反**）。那个 $+1$ 写成 **`cin` 塞进 $C$ 的 bit0**
  最省：进位项左移一位后 $C[0]$ 恒为 0，往 bit0 写 1 就是精确 $+1$；`cin = [q>0]` 顺带
  把正负两分支合成一条通路。
- ⚠️ **阶段 0 的种子走另一条等价路径，别把两处 `+1` 互换**：种子把 $+1$ 折在**移位之前**
  （`(P1 ^ ~qd ^ 1) << 2`，因为 $P_1=X$ 本身可以是奇数）；迭代拍才用 `cin` 写法。
  两条路径实测逐组一致（0/20,279 差异）。
- $q=0$ 时喂真 0；喂 $\overline{0}$ 在模 $2^{PW}$ 下同样等价，只是白走一趟巨量抵消，**无收益**。

#### 4.4.7 on-the-fly 商转换（`A`/`B` 双寄存器）

on-the-fly 冗余商转换的通用方法见 Ercegovac/Lang [[13]](#back-ref-13)。本实现维护
$A[j]=Q[j]$、$B[j]=Q[j]-1\,\mathrm{ulp}$，避免末拍全长减法（radix-4 每拍拼 2 bit）：

$$A[j{+}1]=\begin{cases}(A[j],\,q) & q\ge0\\ (B[j],\,r-|q|) & q<0\end{cases}\qquad
B[j+1]=\begin{cases}(A[j],\,q-1) & q>0\\ (B[j],\,r-1-|q|) & q\le0\end{cases}$$

- **初值 $A[0]=B[0]=0$ 合法**：归一化后 $x,d>0\Rightarrow q_1\ge0$，故 $A$ 的推进在首拍不依赖 $B$。
  ⚠️ **但不变式 $A=B+1$ 并非从 $j=1$ 起成立**：$align$ 奇且 $X<D$ 时 $q_1=0$
  （均匀 32 bit 下约占 $shiftD{=}1$ 样本的一半），此时 $A=0,\ B=(B_0\ll2)+3=3$，$A-B=-3$。
  该无效 $B$ 不会被读——**首零之后的首个非零数字必为正**，由 $q>0$ 分支从 $A$ 重建 $B$。
  正确表述是"**从首个非零商数字之后**成立"；**不能每拍断言 $A=B+1$**。
- **末拍判一次符号即可**：$|Q-Q[k]|=|W[k]|/D\le\rho<1$，故只有取 $A$ 或 $B=A-1$ 两种可能。
- **拼接的安全边界**：正确表的字段恒在 $[0,3]$（此时位拼 `|` 与算术 `+` 逐位等价），
  会炸的只有"把 $q=0$ 并进 $q>0$ 分支"那种写法。

**正确的 5 行 (源, 字段) 表**：

| $q$ | A 的源 | A 字段 | B 的源 | B 字段 |
|---|---|---|---|---|
| $+2$ | A | 2 | A | 1 |
| $+1$ | A | 1 | A | 0 |
| $\mathbf{0}$ | A | 0 | **B** | **3** |
| $-1$ | B | 3 | B | 2 |
| $-2$ | B | 2 | B | 1 |

表内负字段数 = **0**。"源 MUX"（A 侧 $q\ge0$ 用 A、$q<0$ 用 B；B 侧分界是 $q>0$）与
"字段"两件事各自独立；$q=0$ 那行是"双特殊"，也正是"$B=A-1$"这条**算术**恒等式
不能当**位操作**用的地方。

**推荐写法（分支自由，逐位恒等，状态级穷举 24,500 状态不等 = 0）**：

```c
u32 oldA = A, oldB = B;                     // 两条并行读旧值
A = ((q >= 0 ? oldA : oldB) << 2) | (q & 3);
B = ((q >  0 ? oldA : oldB) << 2) | ((q - 1) & 3);
```

**这不是"碰巧对"**：① 源选择是阶跃函数；② 字段也是函数——表内 `4+q`/`3+q` 两列与
`q&3`/`(q-1)&3` **逐行相同（5/5）**，`&3` 不是给负字段打的补丁，而是把三个分支里手算的
规约统一写成 `mod 4`；③ 字段 $\in[0,3]\Rightarrow$ `|` ≡ `+`。再抽象一层只有一句话：

$$\boxed{A' = 4A + q\ (\text{数字串拼接}),\qquad B' = A'-1\ (\text{悲观候选})}$$

**10 种写法的对拍**（11,911 组）：

| 写法 | 错例 | 首个反例 |
|---|---|---|
| 3 分支 + `+` / 3 分支 + `\|` / **分支自由 + `&3`（推荐）** / A 走带符号加 | **0** | — |
| 同样合并但用 `+` | 0 | 靠恒等式侥幸（首拍无效 $B$ 无人读），**不要依赖** |
| $q{=}0$ 并进 $q{\ge}0$ 分支 + `\|`($q{-}1$) | **5,252** | $1000\div7$ → `0xFFFFFFFE` |
| 只省 A 侧 MUX / 只省 B 侧 MUX / 两侧都省 | **9,502 / 7,081 / 10,093** | $1000\div7$ |
| 把 $B$ **寄存器**当字段拼进 A | **11,623** | $1000\div7$（97.6% 错） |

> **两道 source MUX 都不冗余**：$q=0$ 是唯一"两个源不同"的行，占全部商数字的 **23.4%**。
> 另：$A$、$B$ 全程最小值均为 **0**（无需符号处理）；"首个非零商数字之后 $B=A-1$"
> 11,911/11,911 成立。
> 最小复现是 **$11\div3$**（数字 $[+1,0]$）：第 2 拍 $B$ 被写成 `0xFFFFFFFF`，末拍取 $B$
> ⇒ 商 $-1$。**不是 $7\div2$**——后者的数字序列 $[+1,-1]$ 不含 $q=0$，对该陷阱免疫。

#### 4.4.8 非流水模型与延迟

| 层次 | 能否流水 | 原因 |
|---|---|---|
| 单条除法内部 | 不能按普通前馈流水重叠 | $W[j+1]$ 依赖 $W[j]$，存在环路携带依赖；可用展开、重定时或切分 QDS/CSA 提频 |
| 多条除法之间 | 可交错或复制上下文 | 需要保存每条指令私有的余数、`dSlice` 与迭代状态；本项目未实现 |
| 除法 vs 乘法 | 乘法天然可流水 | 乘法是单向 DAG，除法是反馈环 |

- `canAccept(){return !(prepareValid || loopValid || fullAdderValid) && !resultValid;}`——三个阶段任一在算、或结果还没广播出去，都得拒；独立 `busy` 状态已由三阶段 valid 的 OR 精确替代；
- 一般路径从派发到 divCDB 的延迟为 $3+k$；满宽 $k=17$ → **20 周期**。
  其中 `prepare` 产生 $q_1$，`loop()` 只运行 $k-1$ 次；$align=0$ 的一般路径因此
  是 **0 次 `loop()`**，但不是 0-cycle；
- RISC-V 特判路径在 `receive()` 直接锁存 `resultValid`，绕过 `prepare/loop/calculateResult`，
  即 **0 次 SRT 迭代**；结果仍须跨周期变为可见并经 divCDB 广播，也不是组合零延迟；
- 对照 MUL：乘法器是流水线 + 4 槽输出缓冲，派发只需 `!isFull()` 一道门，
  **MUL 侧无 `canAccept()`**（§4.3(d)）；DIV 则必须反过来用三阶段 valid 归约与 `resultValid`
  双门控——这是两者在派发准入上的差异，也是 `DispatchArbiter` 里 MUL 走 `isFull()`、
  DIV 走 `canAccept()` 的原因；
- 无输出缓冲（不像 MUL 有 `outputBuffer[MUL_CAP]`），结果靠 `resultValid` 单发。

SRT 可把 QDS 与 CSA 切级、展开多拍或交错多条指令；是否值得取决于综合关键路径、面积和
目标负载的动态除法占比。本项目选择最小面积的单上下文迭代实现，不把该取舍泛化为通用结论。

#### 4.4.9 定稿配置卡

| 量 | 值 | 说明 |
|---|---|---|
| $bitsD$ | **5** | $dSlice=(D_{\text{dp}}\gg(B_d-5))\ \&\ 31\in[16,31]$；4 无解，5 是必需不是保守 |
| $estBits$ | **9** | 估计器 9 bit 加法器（精确下界 8，取 9 为模型外误差留 1 bit） |
| `estPShift` | **+1** | $=estBits-bitsD-3$ ⟹ `estP = slice >> 1`（省掉这次右移即为 bug） |
| 比较器 | **9 bit** | $\lvert2\,estP\rvert\le172$（$\lvert estP\rvert\le86$，负端因 floor 多 1） |
| 单拍关键路径 | **9 bit 加法 + 9 bit 比较** | $O(1)$，与操作数位宽无关 |
| 误差窗口 | $[0,+2]$ ulp | $2^{\,4+bitsD-estBits}+1$，**单侧** |
| 违例 | **0** | 穷举 + 698,280 组端到端，双重确认 |

**下界为什么是 8 而不是 9**：误差是**单侧**的（两次算术右移只减不增，$t-estP\in[0,2^{4+bitsD-estBits}+1)$，
实测 951,898 次迭代中 $t-estP<0$ 共 **0** 次）。$bitsD{=}5$ 下精确穷举（$dSlice$/$wLow$/可达边界
有限枚举）得 $estBits\ge8$ 违例 **0**、$estBits{=}7$ 违例 11；而 $bitsD{=}4$ **无论 $estBits$ 多大
都不可行**（违例随 $estBits$ 增多 ⟹ $D$ 至少 5 bit）。旧文报的下界 9 是**双侧误差模型**的产物
——"保守模型会多要 1 bit"。

**端到端交叉验证**：$estBits{=}7$ 有真违例（首反例 $x=\text{0xf83bc957},d=\text{0xaf}$，商少 47,934）；
$estBits{=}8/9/10$ 在小操作数全域（523,776）+ 定向 213 + 随机 32 位（174,291）共 698,280 组上全 0 错。

> **为什么穷举是证明而非抽样**：整数离散，"寄存器取高 $estBits$ 位能否无错选商"可**判定**。
> 固定 $(dSlice,A,\delta)$ 后 $s(V,D)=2D-3|V-qD|$ 是**凹**函数、可达域是**凸**多边形，
> 故枚举"最小整数盒角点 ∪ 可达边界格点 ∪ 各自 $\pm1$ ulp 四邻域"即**完备**。
> 最紧格永远是 $dSlice{=}16$（除数最小、重叠窗最薄），被推到 $|V|=\frac83D$ 的角点（零余量），
> **每个 $estBits$ 都有**，$estBits$ 增大消不掉。

**Pentium FDIV 三条纪律**（本项目据其失效模式归纳）[[14]](#back-ref-14)：① don't-care 必须**证明**不可达（可达性分析 / 整数格点穷举）；
② 生成脚本的输出必须与**独立实现**的参考逐格 diff，不能只抽样；③ 定向测试必须覆盖
$\varepsilon_{\max}$ 最小的那一列。**结构性好处**：没有表，就没有"漏填的格"——比较网络从结构上
**不可能漏格**，正确性退化为一个可穷举证明的整数不等式。

#### 4.4.10 常量与源码落点

`src/include/DIV.hpp` 的 `constexpr`：

| 常量 | 值 | 含义 |
|---|---|---|
| `estBits` | 9 | 估计器位数 |
| `ulpExpWithShiftD` | 28 | $shiftD{=}1$ 时 $dSlice$ 的移位量（$B_d-5=33-5$） |
| `ulpExpNoShiftD` | 27 | $shiftD{=}0$ 时（$B_d-5=32-5$） |
| `sliceShiftWithShiftD` | 27 | $shiftD{=}1$ 时切片起始位（$PW-estBits=36-9$） |
| `sliceShiftNoShiftD` | 26 | $shiftD{=}0$ 时（$35-9$） |

`DIV.cpp` 四段与公式的对应：

| 函数 | 对应 | 要点 |
|---|---|---|
| `receive()` | §4.4.6 阶段 0 入口 | RISC-V 特判直接置 `resultValid`；一般路径取幅值并置 `prepareValid` |
| `prepare()` | 阶段 0 尾 | clz / 对阶 / `loopTimes=(align+1)>>1` / `shiftD=align&1` / `unsignedDivisor <<= shiftD` / `dSlice` / 三分支种子（$q_1=2/1/0$，含 `regA/regB`）。**已吃掉 $q_1$，故 `loop()` 只跑 $k-1$ 拍** |
| `loop()` | 阶段 1 迭代 | 9 bit 双切片相加 → `slice = (int32_t)(((sum9 & 0x1FFu) ^ 0x100u) - 0x100u) & ~1`（掩码 + 补码折叠 + `estPShift=1` 的 `>>1` **折进 `&~1`**）→ 五分支 $q$；`mask = shiftD ? (1ull<<36)-1 : (1ull<<35)-1` |
| `calculateResult()` | 阶段 2 收尾 | `Pk = (oldRegS+oldRegC) & mask` → bit35/34 符号修正 → `(Pk>>63)==0 ? (A, (Pk>>2>>shiftD)>>clzD) : (B, ((Pk+(unsignedDivisor<<2))>>2>>shiftD)>>clzD)` |

**RISC-V 特判表**（`receive()` 命中即绕过 SRT 前置/迭代/后处理；“0 次迭代”不等于“0 周期”）[[4]](#back-ref-4)：

| 条件 | `div` | `divu` | `rem` | `remu` |
|---|---|---|---|---|
| $d=0$ | $-1$ | $2^{32}-1$ | $x$ | $x$ |
| $x<d$ | $0$ | $0$ | $x$ | $x$ |
| $x=d$ | $1$ | $1$ | $0$ | $0$ |
| `INT_MIN / -1` | `INT_MIN`（不 trap） | — | $0$ | — |

⚠️ **只截 QDS 输入，$W$ 通路一位都不能丢**——整数要求商与余数逐位精确；
浮点除法通常按 IEEE 754 的正确舍入规则处理，不能把“允许 1 ulp”当作这里的
正确性口径 [[16]](#back-ref-16)。

#### 4.4.11 阶段 2：后处理

1. 全长加法 $P_k=S+C$（$=4W[k]$）——**迭代环路内 0 次**进位传播，这是第 1 次；
2. 判符号，**商与余数必须配套修正**：

| $P_k=4W[k]$ | 商 | 余数 |
|---|---|---|
| $\ge 0$ | $Q=A$ | $R=P_k$ |
| $<0$ | $Q=B=A-1$ | $R=P_k+4D_{\text{dp}}$ |

3. 对阶还原：$rem=((R\gg2)\gg shiftD)\gg clzD$（**先把 $P$ 域的 $/4$ 折回来**，再对阶）。

> 整个除法的全长进位传播 = 迭代环路 **0 次** + 后处理 **1～2 次**（$P_k\ge0$ 取 $A$：1 次；
> $P_k<0$ 再要一次 $P_k+4D_{\text{dp}}$：2 次）。**别简写成"全程仅 1 次"**。
>
> 右移 **$clzD$（除数的移位量）**，不是 $clzX$、不是 $align$：$X\cdot2^{align}=Q\cdot D+W[k]$，
> $D=d\cdot2^{clzD}\Rightarrow x=Qd+W[k]/2^{clzD}$。**商不缩放、余数要缩放**——预移位是
> "被除数除数同乘 $2^{clzD}$"，商在等比例缩放下不变，余数却被放大 $2^{clzD}$ 倍。

**手算验证（$1000\div7$）**：$clzX{=}22,clzD{=}29,align{=}7$ 奇 ⟹ $k{=}5,shiftD{=}1$。
单位 $u=2^{22}$，$X=P_1=1000u$、$D_{\text{dp}}=1792u$、$\rho D_{\text{dp}}\approx1194u$。
逐拍 $|W|\le\rho D_{\text{dp}}$ ✓、$A=B+1$ ✓。末拍 $W_5=-256u<0$ → 取 $B=\mathbf{142}$：

$$R=P_5+4D_{\text{dp}}=-1024u+7168u=6144u,\qquad rem=((6144u\gg2)\gg1)\gg29=\mathbf{6}\ \checkmark$$

（若错取 $A=143$ 却不补 $D_{\text{dp}}$，余数变负——**取 B 与 $+D_{\text{dp}}$ 必须配套**）

**重叠区任选演示（$1000\div3$）**：数字 $\{1,1,1,-1,\underline{q_5}\}$。路径 a：$q_5=1\to A=333$，
$W>0$ 取 A，$rem=1$ ✓；路径 b：$q_5=2$（重叠区）$\to W<0$ 取 $B=333$，$rem=(W+D)\gg8=1$ ✓。殊途同归。

### 4.5 除法参数速查（位宽两档）

| $shiftD$ | $B_d$ | $SW$ | $PW$ | 切片：**P 域**右移 $PW-estBits$ | `estPShift` | $estP$ 的 ulp | $dSlice$ 值域 |
|---|---|---|---|---|---|---|---|
| 0 | 32 | 33 | 35 | **26** | **+1** | $2^{B_d-5}$ | $[16,31]$ |
| 1 | 33 | 34 | 36 | **27** | **+1** | $2^{B_d-5}$ | $[16,31]$ |

随 $shiftD$ 变的**只有切片起始位**（差 1 位）——即 §4.4.4 说的那根 1 bit 选择线；
`estPShift` 与 $dSlice$ 值域两档完全相同 ⟹ **硬件是一次性 MUX，不存在每拍动态移位**。

| 配置 | 估计器 | 比较器 | 合计 | 备注 |
|---|---|---|---|---|
| $bitsD{=}5,\ estBits{=}8$ | 8 bit | 9 bit | 17 bit | 可证 0 违例，但零冗余 |
| **$bitsD{=}5,\ estBits{=}9$** | 9 bit | 9 bit | 18 bit | **定稿** |
| $bitsD{=}5,\ estBits{=}10$ | 10 bit | 9 bit | 19 bit | 备选，无额外收益 |
| $bitsD{=}6,\ estBits{=}8$ | 8 bit | 10 bit | 18 bit | 加法器最短（实测可行） |

> "比较器"按 $estP=slice\cdot2^{\,bitsD+3-estBits}$ **折算后**的 $|2\,estP|$ 定宽。
> 故 $estBits$ 越小**不代表** $estP$ 越窄——**折算后的位宽才是 $estBits$ 与 $bitsD$ 的共同结果**。

---

### 4.6 三个实现陷阱（写 RTL / C++ 前必读）

> 三者均为端到端调试实证，且**都不是理论问题，是位操作问题**——理论推导再正确，写错一行照样崩。

#### 陷阱一：切片位宽用错——寄存器装的是 $P=4W$，却按 $W$ 域（$SW$ 位）切

| 项 | 内容 |
|---|---|
| **定义** | 估计器取高位时按 $SW-estBits$ 右移 $S,C$，而不是 $PW-estBits$。**寄存器 $(S,C)$ 装的是 $P=4W$**（$PW=SW+2$），切片位**恒为 $PW-estBits$**。 |
| **机理** | 多留 2 个低位 ⟹ 等价于**把读数整体 $\times4$**。$slice$、$estP$ 全部 $\times4$，五个阈值相对量程被压小 4 倍，$q$ 系统偏大（动辄选 $+2$）。 |
| **症状** | $q$ 偏大 → $W$ 越界后**逐拍发散**，商/余数全错。**实测反例**（$1000/7$ 第 1 拍，$P_1=X$）：正解 $slice=X\gg(PW-estBits)=X\gg27=31\Rightarrow estP=15$，$2\,estP=30\ge dSlice=28\Rightarrow q_1=+1$ ✓；错法 $slice=X\gg25=125\Rightarrow estP=62$，$2\,estP=124\ge3dSlice=84\Rightarrow q_1=+2$ ✗——**第 1 拍就越界发散**。 |
| **特征** | **无数据依赖性、首拍即现**。 |
| **影响面** | 致命且系统：实测端到端 **18,465/20,279（91.1%）错**。最小复现 `7/2`：正解 $Q{=}3,rem{=}1$；错法 $Q{=}6,rem{=}3$。 |
| **正解** | **切片位 = $PW-estBits$，每一拍都一样**（含阶段 0 尾的 $q_1$）。寄存器从头到尾都是 $P$ 域，**不存在"域切换"**。 |

```c
// ✗ 错：寄存器装的是 P=4W，却按 W 域（SW 位）切 —— 读数整体 ×4
slice = ((S >> (SW-estBits)) + (C >> (SW-estBits))) & ((1<<estBits)-1);
// ✓ 对：按 P 域（PW 位）切；阶段 0 尾的 q₁ 用的是同一个位
slice = ((S >> (PW-estBits)) + (C >> (PW-estBits))) & ((1<<estBits)-1);
```

#### 陷阱二：$estBits$ bit 估计加法器的进位未丢弃

| 项 | 内容 |
|---|---|
| **定义** | $slice$ 的真和落在 $[0,2^{estBits+1}-2]$（**$estBits+1$ bit**）。必须先 `& ((1<<estBits)-1)` 丢弃进位，**再**做补码折叠。只折叠不掩码会偏差整整 $2^{estBits}$。 |
| **机理** | `& (2^{estBits}-1)` 就是 $\bmod 2^{estBits}$——把真和折回补码环上，**这一步做完折叠才有意义**。 |
| **症状** | $slice$ 整跳 $\pm2^{estBits}$（被丢弃那一位的权重）→ 折算到 $estP$ 为 $\pm2^{\,bitsD+3}$ ulp（$\pm256$，而 $estP$ 只有 $\pm83$ 量程）→ QDS 必然饱和到 $\pm2$ → 发散。与陷阱一不同，**两个方向都可能**。 |
| **特征** | **偶发跳变型**：只在 $S,C$ 高位之和 $\ge2^{estBits}$ 时触发（实测约 **42% 的迭代**真的溢出）。定向用例 `2/1` 是确定性触发的最小复现。 |
| **隐蔽性** | **三者中最高**——样本量不足或高位分布偏小时"跑了几百万组没出问题"，上线后特定数据才炸。 |
| **正解** | 先取 $estBits$ 位模、再折叠。折叠无歧义的前提 $\lvert P\rvert\le\frac83D_{\text{dp}}<2^{PW-1}$ 由收敛不变式保证，余量 $\frac13\cdot2^{PW-1}$。 |

```c
// ✗ 错：少了 estBits 位掩码，进位没丢
slice = (S >> (PW-estBits)) + (C >> (PW-estBits));
if (slice >= (1<<(estBits-1))) slice -= (1<<estBits);
// ✓ 对：先取 estBits 位模，再折叠
slice = ((S >> (PW-estBits)) + (C >> (PW-estBits))) & ((1<<estBits)-1);   // 进位自然丢弃
if (slice >= (1<<(estBits-1))) slice -= (1<<estBits);                    // 补码折叠
```

> 🔍 **两个 MASK 别混**：
> ① **`(1<<estBits)-1`（9 bit）**：**估计器**的进位丢弃，作用在 $estBits$ bit 加法器输出上（陷阱二）；
> ② **`(1<<PW)-1`（35/36 bit）**：**CSA 递推**的回卷，作用是"把 `u32/u64` 伪装成 $PW$ 位寄存器"。
> ②的位宽**一位都不能少**（硬下界 $PW$），①只要 $\ge estBits+1$ 即可。
> **`MASK`（回卷）实测**（116,513 组 / 951,900 次迭代）：$\text{maskw}\ge PW$ → 0 错；
> $35$ → 错 59,958（99.4%，**全在 $shiftD{=}1$ 半**）；$34$ → 113,323（97.3%）；$32$ → 115,060（98.8%）。
> 临界值精确等于 $PW$ 本身，与 $SW$ 无关。回卷对 QDS **完全透明**（回卷位的位权 $2^{\text{maskw}}\ge2^{PW}$
> 恒高于估计窗口最高位 $2^{PW-1}$）。

> ⚠️ **类型陷阱：$S,C$ 装不进 `u32`**。$SW=33/34$、$PW=35/36$ **全部 $>32$**，
> 承载 $S,C$ 及 CSA 三操作数 $S_4,C_4,T$ 的类型必须 $\ge PW$ 位（`u64`）。
> 用 `u32` 等价于把回卷位宽压到 32——实测 116,513 组错 115,060（98.8%），且**第 1 拍就错**。
> 反面：用 `u64` 而**完全不写 `& MASK`** 结果也对（$64\ge36$），但那不可移植到 RTL、
> 高 $k$ 用例逼近 64 bit 边界——**照 $PW$ 回卷才是与 RTL 一一对应的写法**。
> 另：$|S|,|C|$ 各自峰值 $=2^{PW-1}$（**两位都会吃满寄存器**），靠模 $2^{PW}$ 抵消才表达出
> $|P|\le\frac83D_{\text{dp}}$ ⟹ **位宽不可缩，估计器必须按补码取高位**。

> ⚙️ **C++ 语义三条（动 DIV 单元前先读）** [[15]](#back-ref-15)：
> ① `fold(...) << (-estPShift)` 一支在定稿 $estBits{=}9$ 下是**死分支**，但 `estPShift` 是 constexpr，
> 编译器仍会实例化该表达式——**有符号左移在 C++20 前是 UB**。本项目按 C++20 编译；移植回 C++17
> 工具链时这一支必须先转 `u32` 再移位。
> ② **有符号算术右移在 C++20 前是实现定义**，C++20 起才强制算术右移——"与 RTL 一一对应"的纪律
> 依赖这条。
> ③ `u32 dSlice` / `(i32)dSlice` 的 cast 不是洁癖：$dSlice\in[16,31]$ 无符号，而判据另一端
> `estP << 1` 有符号，**混用会让整型提升把判据拖进无符号域**（`(estP << 1) >= -T3` 直接恒真）。
> **规则：判据两侧必须同号**。
> ④ **`S >> (PW-estBits)` 是"碰巧 robust"**：无论 $S$ 存成符号扩展的 `i64` 还是原始 pattern 的 `u64`
> 结果都一样——因为紧随其后的 $estBits$ 位取模把差异全掩掉。**这依赖那个 mask 存在**。

#### 陷阱三：on-the-fly 拼接用 `|` 而非 `+`

| 项 | 内容 |
|---|---|
| **定义** | **只有"把 $q=0$ 并进 $q>0$ 分支"那种写法才会中招**：$B'$ 一律走 `(A<<2) \| (q-1)`，于是 $q=0$ 时低位字段是 **$-1$**（`0xFFFFFFFF`），`\|` 把**整个寄存器**写成全 1。**正确的 3 分支表里没有任何负字段**。 |
| **机理** | $(A,q-1)=4A-1$ 在 $q=0$ 时确实等于 $(B,3)=4B+3$，但这是**算术恒等式，不是位拼接恒等式**：`-1` 的 2 补码全 1，`\|` 沿位或污染 $A$ 的全部高位。 |
| **症状** | 商突然变成 $-1$ 或一个巨大负数；此后每拍继续被污染，**不会自愈**。最小复现 **$11\div3$**。 |
| **特征** | **只在 $q=0$ 分支触发**，而 $q=0$ 恰是**最常见的商数字**（占全部数字的 **23.4%**）⟹ **命中率极高，几乎必现**，三者中最易暴露。 |
| **正解** | 低位字段先规约到 $0..3$ 再拼，或整式用 `+`；**分支自由写法**见 §4.4.7。 |

```c
// ✗ 错：q=0 时 (q-1) = -1 = 0xFFFFFFFF，| 污染整个 A
A = (A << 2) | (q - 1);
// ✓ 对：低位先规约到 0..3 再拼；oldA/oldB 保证两条并行读旧值
u32 oldA = A, oldB = B;
A = (q >= 0) ? ((oldA << 2) + q        ) : ((oldB << 2) + (4 + q));
B = (q >  0) ? ((oldA << 2) + (q - 1)  ) : ((oldB << 2) + (3 + q));
```

> ⚠️ **早期文档把规则简记为"$q\ge0\Rightarrow B=(A,q-1)$"——数值等价，但不能当位操作实现**。

#### 4.6.1 三者对照速查

| | 陷阱一（切片位宽错） | 陷阱二（进位未丢弃） | 陷阱三（`\|` 拼接） |
|---|---|---|---|
| **性质** | 切片位宽用错（$SW-estBits$ 而非 $PW-estBits$） | 溢出位未掩码 | 有符号字段当无符号位拼 |
| **触发条件** | **每一拍**（读数整体 $\times4$） | $S,C$ 高位之和 $\ge2^{estBits}$ | 任何 $q=0$ 的拍 |
| **触发频率** | **极高（首拍即错，实测 91.1%）** | **低（数据依赖，约 42% 迭代）** | **极高（$q=0$ 最常见）** |
| **隐蔽性** | 中 | **最高** | 最低 |
| **最小复现** | `7/2`（第 1 拍 $q_1$ 由 $+1$ 变 $+2$） | `2/1` | **`11/3`**（商 $-1$） |
| **错误形态** | 单向越界发散（$q$ 偏大） | $slice$ 双向整跳 $\pm2^{estBits}$ | 商变 $-1$，持续污染 |
| **检查点** | 切片是 $PW-estBits$ 还是 $SW-estBits$ | 有无 `& ((1<<estBits)-1)` | 拼接符是 `+` 还是 `\|`、字段是否 $\ge0$ |

#### 4.6.2 通用防御

① 先跑定向小用例（`7/2`、`2/1`、`1/1`、`INT_MIN/-1`、**`5/3`**）再上随机；
② 每拍断言 $|W|\le\rho D_{\text{dp}}$，越界即停——三个陷阱全部会在**第 1~2 拍**触发它；
   $A=B+1$ **不能**每拍断言（$q_1=0$ 时首拍即为 $A-B=-3$，约 1/4 的除法会误报）；
③ 随机对拍必须覆盖 $x<d$、$align$ 奇偶、负余数高频三类分布——`5/3` 是 $q_1=0$ 的最小复现
   （$align=1$ 奇、$X<D$）。

---

## 5. 写回：四路结果总线

结果广播/标签唤醒的基本思想来自 Tomasulo 算法 [[1]](#back-ref-1)。本实现的结果总线
载荷定义在 `CDB.hpp`：`aluCDB` / `lqCDB` / `mulCDB` / `divCDB`——ALU、Load(LQ)、
MUL、DIV 各驱动一根，**源之间无跨单元仲裁**，每周期至多四个结果并行广播。
重命名保证同时在飞的目的寄存器使用不同物理标签，消除同地址写冲突；PRF/ROB
仍按四个逻辑写回端口消费这些并行结果：

- 消费端：PRF 完成写口（置 ready + 写值）、ROB 完成置位（`isCommitReady`）、
  LQ 完成口（`lqCDB` 带 `memIndex`，见 [访存](memory.md)）、BPU 训练
  （CDB 侧只改表）；
- 条件分支/间接跳转的结果载荷挂在 ALU 总线上（`isControl`）——FlushArbiter 在
  该总线上检测 JALR 目标误预测；
- **mulCDB 恒每拍可发**（MUL 有 4 槽输出缓冲，取最老 robTag）；
  **divCDB 受 `resultValid` 门控**（DIV 无输出缓冲，单发），被消费后
  周期更新清 `resultValid`；完成计算则在另一状态分支置位结果，单发不丢；
- `VERBOSE=cdb` 输出争用统计（both / 仅单侧 / 若单总线谁胜出），用于量化
  多总线 vs 单总线的收益边界。

> 设计注记：同一指令只可能由一个执行源完成，"同周期两总线写同一物理寄存器"
> 在源头上即被排除；保留站查 PRF 就绪的模型使总线数量不构成依赖链瓶颈。
> **四路 vs 三路**：DIV 单独占一路是为了让它那条长延迟（最多 20 周期）不阻塞
> MUL/LQ 的写回——若把 div 结果挤进 mulCDB，除法在飞时每次 MUL 完成都要与
> 除法的单发结果抢同一根线。

---

## 6. 提交与 squash 恢复

### 6.1 按序提交

ROB 头按序退休用于把乱序完成重新收敛为精确架构状态 [[2]](#back-ref-2)[[3]](#back-ref-3)。
ROB 头就绪即提交（每周期至多 1 条）：`REGISTER` 类型释放 `oldPhy` 回 PRF
自由表；`STORE` 类型在提交点经访存路径写缓存（见 [访存](memory.md)）；
halt 条目（`0x0ff00513`）提交后 `haltCommitted`，停机条件 = halt 已提交 ∧
FQ/IQ/ROB/SQ 全空 ∧ DCache 非 busy ∧ DMEM 读写双口均空闲。后三级 drain 条件
保证 halt 前已提交的 store 真正进入缓存，且相关回填/脏写回不会被进程退出截断。
进程输出停机时 `x10` 低 8 位。

### 6.2 FlushArbiter（squash 排队）

有状态仲裁器，拥有 4 项 squash 请求队列，一个周期内按固定顺序检测：

1. **BRU 分支误测**（BRU 最老有效结果 vs 预测）；
2. **CDB JALR 误测**（ALU 总线上 `isControl` 载荷 vs 预测）；
3. **记忆违例 MDP**（更老 store 地址解析发现更年轻 load 已越过，见
   [访存](memory.md) §4）——同一周期多源请求时**最老优先**。

`arbitResult()` 产出全局 `squashDetect{SquashTag, SquashPC, CkptId}`，随后每个
模块在自己的周期更新内按该窗口恢复：

| 模块 | 恢复动作 |
|------|----------|
| `RAT` | 从被 squash 的最老 ROB 条目的 checkpoint 快照整表回滚 |
| `PRF` | 按 ROB 条目 checkpoint 的 `headSeq` `restoreHead`，回卷自由表（未提交分配全部作废） |
| `BPU` | 按 `ckptId` 恢复 `BPUSnapshot`（GHR/AlignQueue/RAS_top），折叠视图重算（见 [frontend.md](frontend.md) §4.3） |
| `FQ/IQ/RS/LQ/SQ` | 各按 ROB 条目记录的尾快照回卷（RS 释放槽位、LQ/SQ 按 `getTailSnapshot` 截断） |
| `MUL` | `flush(tag)`：清 `partialRes/scRes` 的 valid + 清 `outputBuffer` 中不早于 tag 的槽位 |
| `DIV` | `flush(tag)`：**整机清零**（`resultValid`/`regS`/`regC`/`regA`/`regB`/`dSlice`/`loopTimes`/`prepareValid`/`loopValid`/`fullAdderValid` 全归零）——单实例无缓冲，被 squash 即在算的那条已经作废 |
| `FetchUnit` | 清 `haltFetched`（若被回卷）并从 `SquashPC` 重启取指 |

误预测惩罚 = squash 排队到前端重启取指之间的固定拍数 + 重执行时间；分支预测
统计（`VERBOSE=branch`）在解析点记录正确/总数，含方向与目标两个维度。

> `DIV` 的 flush 之所以比 `MUL` 更狠：`MUL` 是流水线，被 squash 的指令可能只在某一级，
> 逐级清 valid 即可；`DIV` 是**单实例闭环**，在算的那条与寄存器状态一一对应，
> 无法"部分保留"，只能整体作废——随后由重执行重新 `receive()`。

---

## 7. 关键规格

| 项 | 规格 |
|----|------|
| ROB / PRF / RAT | 64 / 128 / 32 |
| 保留站 | Integer 8 · Multiply 4 · **Divide 4** · Load 4 · StoreAddr 4 · StoreValue 4 · Branch 4 |
| 发射 | 每周期至多 1 条（IQ 头，单口 rename） |
| 执行/写回 | ALU·AGU·BRU·MUL 各 4 槽；DIV 单实例（三阶段 valid 归约背压）；派发与多槽输出均按 ROB 年龄选最老就绪/有效项 |
| 结果总线 | 4 根（aluCDB / lqCDB / mulCDB / **divCDB**），无跨单元仲裁 |
| MUL | Booth radix-4 + 3:2 CSA 压缩树（19 行 → 17 cell → S+C），3 级流水；`mul/mulh/mulhu/mulhsu`；派发门控 = `isFull()`（4 槽 > 在飞 3，无需背压） |
| DIV | SRT radix-4，$bitsD{=}5,\ estBits{=}9$，共享 ulp 常数法 QDS，on-the-fly 商转换；**单实例非流水** |
| DIV 位宽 | $B_d=32/33$，$SW=33/34$，$PW=35/36$（寄存器持 $P=4W$）；`MASK` 硬下界 $=PW$ |
| DIV 延迟 | $3+k$（满宽 17 拍 → **20 周期**）；迭代环路全长进位传播 0 次 |
| DIV 特判 | $d{=}0$ / `INT_MIN÷-1` / $x<d$ / $x{=}d$ 均为 0 次 SRT 迭代；结果仍经寄存器与 divCDB 广播 |
| FlushArbiter | 4 项请求队列；检测序 = BRU 误测 → CDB JALR 误测 → MDP；最老优先 |
| 停机 | halt 提交后继续 drain，直至 FQ/IQ/ROB/SQ 空、DCache 空闲、DMEM 双口空闲；出口 = `x10 & 0xFF` |

---

## 附 A：SRT 符号映射

下表用于把本文记号映射到 Parhami 的计算机算术教材 [[8]](#back-ref-8)；标为“—”的项
是本项目数据通路派生量，不应理解为教材中的原始符号。

| 本文件 | Parhami | 含义 |
|---|---|---|
| $r$ / $a$ | $r$ / $\alpha$ | 基数（固定 4）/ 数字集上界（=2） |
| $\rho$ | $h$ | 冗余因子 $=a/(r-1)=2/3$ |
| $D$ | $d$ | 除数（归一化后 $[2^{31},2^{32})$） |
| $D_{\text{dp}}$ | — | 数据通路实际除数 $=D\ll shiftD$，位宽 $B_d$ |
| $shiftD$ / $B_d$ | — | $align\&1$：首拍奇偶决定的除数额外左移量；**数据通路除数位宽** $=32+shiftD$。⚠️ 勿与 on-the-fly 的 $B$ 寄存器混淆 |
| $SW$ / $PW$ | — | $W$ 域补码位宽 $=B_d+1$；$P=4W$ 域位宽 $=SW+2=B_d+3$。**寄存器 $(S,C)$ 装 $P$ 域** |
| $W$ / $P$ | $align$ / $p$ | 部分余数（**只作说明**）/ **寄存器实际持有的量** $=4W$（carry-save 对 $S,C$） |
| $dSlice$ / $estP$ | — | $D_{\text{dp}}$ 的 5 bit 截断 / $P$ 的估计值（共享 ulp；定稿下 $estP=slice\gg1$，8 bit 有符号，域内 $\lvert estP\rvert\le86$） |
| $estBits$ / $bitsD$ | — | 估计器保留位数（**=9**）/ $D$ 参与 QDS 的位宽（=5）。**代码里一律小写 `estBits`** |
| `estPShift` | — | $estP$ 的右移量 $=estBits-bitsD-3$（定稿 $=1$）。⚠️ 与 $clzD=\mathrm{clz}(d)$ **无关**，勿混 |
| $A,B$ | — | on-the-fly 双寄存器；$A=B+1$ **仅在首个非零商数字之后**成立（$q_1{=}0$ 时首拍 $A-B=-3$） |
| $clzX$ / $clzD$ / $align$ | — | $\mathrm{clz}(\lvert x\rvert)$ / $\mathrm{clz}(\lvert d\rvert)$ / $clzD-clzX$ |
| $wLow$ / $r$ | — | **仅 §4.4.9 穷举局部**：$V$ 的窗内低位 / $D$ 的截断低位。$\lambda,\rho$ 是全文级符号，**禁止复用** |
| $V$ / $q0$ | — | **仅 §4.4.9 穷举局部**：寄存器读数 $=P=4W$（$V=A\,q0+wLow$）/ 窗口步长 $=2^{PW-estBits}$ |
| $W[j]$ | — | 第 $j$ 拍**出口**的部分余数，$W[j{+}1]=4W[j]-q_{j+1}D_{\text{dp}}$，故 $P_j=4W[j-1]$ |

> ⚠️ **撞名消歧**：① $B_d$（位宽）vs $B$（商寄存器）——靠上下文；
> ② $align$（对阶移位量）vs $S$（carry-save 和）——**靠大小写**，全文严格遵守；
> ③ $clzD$ vs `estPShift` —— 后者已改名带 `estP` 前缀。
> **读法：带下标 = 局部/派生量；单个大写字母 = 寄存器/向量；单个小写字母 = 标量参数。**
> 若再发现同形异义，**直接改名而不是加注**。

---

## 附 B：调试断言清单

**原则**：能**每拍**断言的只有"域"类不变量（它们是设计的数学前提）；
"关系"类不变量只在特定窗口成立，每拍断言会误报。

### B.1 每拍可断言（入口 / 出口各一次）

| # | 断言 | 违反时的含义 |
|---|---|---|
| A1 | $\lvert P\rvert\cdot3\le8D_{\text{dp}}$（第 $j$ 拍**入口**） | QDS 域越界：切片位置或 $B_d$ 算错 |
| A2 | $\lvert W\rvert\cdot3\le2D_{\text{dp}}$（第 $j$ 拍**出口**） | 商数字选错（最常见：`estPShift` 符号、MASK 位宽不足） |
| A3 | $dSlice\in[16,31]$（整次除法一次，非每拍） | $B_d$ 或归一化错（写死 32 的典型症状） |
| A4 | 回卷位宽 $\text{maskw}\ge PW$ | MASK 写窄；**临界值恰为 $PW$**，与 $SW$ 无关 |
| A5 | 不得假设 $\lvert S\rvert,\lvert C\rvert$ 与 $\lvert W\rvert$ 同量级（峰值 $8/16\,D_{\text{dp}}$） | 把 $slice$ 当"$\lvert W\rvert$ 的高位切片"直接相加，未折叠/未丢进位 |

### B.2 只在特定窗口成立（每拍断言会误报）

| # | 断言 | 成立窗口 |
|---|---|---|
| B1 | $A=B+1$ | **首个非零商数字之后**；首拍 $q_1=0$ 时 $A=0,B=3$（差 $-3$），该 $B$ 无人读 |
| B2 | $estP\le t$（误差单侧偏小） | 恒成立，但**前提是 $estBits$ 位切片且 mask 与 fold 成对** |
| B3 | $q<0$ 不出现在首拍 | 本方案归一化保证（$q_1\in\{0,1,2\}$，且 $shiftD{=}0\Rightarrow\{1,2\}$） |
| B4 | 首个非零数字停在 $[-3\,dSlice,3\,dSlice]$ 内 | QDS 常数法的定义域 |

### B.3 收尾断言（必查）

| # | 断言 |
|---|---|
| C1 | $Q=x/d$ 且 $R=x\bmod d$（与**真值**对拍，不与中间量比） |
| C2 | $0\le R<d$ |
| C3 | 末拍 $W[k]<0$ ⟺ 商取 $A$ / 余数 $+D_{\text{dp}}$ **成对**（只改一个必错） |
| C4 | 端到端与**独立实现**逐用例 diff |

### B.4 位级调试的黄金轨迹（dp 域逐拍 dump）

调试的痛点不是"推不出来"，而是"**第 2 拍就分叉**"。下表是 **dp 域**的逐拍记录，
可直接与实现在 $S,C,A,B,estP,q$ 上**逐列**对齐（$S,C$ 列给出该拍**入口**的两个寄存器
全宽读数，$P$ 列 $=\texttt{fold}(S+C,PW)$）。下列两个用例**都落在 $shiftD{=}1$**。

**(1) $1000\div7$**（$shiftD{=}1,\ B_d{=}33,\ k{=}5,\ dSlice{=}28$；单位 $u=2^{22}$，
dp 域原值 $=\times u$；$X=P_1=1000u$，$D_{\text{dp}}=7\cdot2^{30}$）

| 拍 $j$ | $q$ | $estP$ | $dSlice$ | $P$ | $W$ | $S$（该拍入口） | $C$（该拍入口） | 商寄存器 |
|---|---|---|---|---|---|---|---|---|
| 1 | $+1$ | $15$ | 28 | $1000u$ | $-792u$ | `0x0FA000000` | `0x000000000` | $A{=}1,\ B{=}0$ |
| 2 | $-2$ | $-50$ | 28 | $-3168u$ | $416u$ | `0xB17FFFFF8` | `0x1D0000008` | — |
| 3 | $+1$ | $25$ | 28 | $1664u$ | $-128u$ | `0x51FFFFFC0` | `0xC80000040` | — |
| 4 | $0$ | $-9$ | 28 | $-512u$ | $-512u$ | `0xE800001FF` | `0x0FFFFFE01` | — |
| 5 | $-1$ | $-33$ | 28 | $-2048u$ | $-256u$ | `0x9FFFFFFF8` | `0x400000008` | $A{=}143,\ B{=}142$ |

末拍 $W_5=-256u<0$ ⟹ $P_5=4W_5=-1024u$，取 $B{=}142$ 且 $R=P_5+4D_{\text{dp}}=6144u$
⟹ $rem=((6144u\gg2)\gg1)\gg29=\mathbf{6}$ ✓

> **$estP$ 列是最易分叉处**：若切片位写错（陷阱一），$j{=}1$ 的 $estP$ 就由 $15$ 变 $62$、
> $q_1$ 由 $+1$ 跳 $+2$，端到端 91.1% 错。

**(2) $5\div2$**（$shiftD{=}1,\ B_d{=}33,\ k{=}2,\ dSlice{=}16,\ D_{\text{dp}}=2D=2^{32}=4294967296$；
原值，不缩放）

| 拍 $j$ | $q$ | $estP$ | $P$（= 寄存器入口读数） | $W$ | $A$ | $B$ |
|---|---|---|---|---|---|---|
| 1 | $+1$ | $10$ | $2684354560$ | $-1610612736$ | 1 | 0 |
| 2 | $-2$ | $-25$ | $-6442450944$ | $2147483648$ | 2 | 1 |

末拍 $P_2=4W_2=8589934592\ge0$ ⟹ 取 $A{=}2$、$R=P_2$，$rem=((R\gg2)\gg1)\gg30=\mathbf{1}$ ✓（商 2 余 1）。

> 本表是 **V0（定稿口径，除数走 $D_{\text{dp}}=2D$）**的读数；同例在 **V3"都不放"**变体下读出的
> "商 5、余 0"是 §4.4.3 的最小反例，**两者不是同一张表**。

**(3) 其余建议用例**：$1000\div3$（$shiftD{=}0$ 一档对照）、$7\div2$、$11\div3$、
$0\text{xFF000000}\div0\text{x01000000}$（$k{=}5$）、$0\text{xFFFFFFFF}\div1$（**满宽 $align{=}31\Rightarrow k{=}17$**）。

---

## 相关文档

- [`../README.md`](../README.md) — 数据通路图 / 周期模型
- [`frontend.md`](frontend.md) — 预测 checkpoint 的语义与恢复（GHR/RAS）
- [`memory.md`](memory.md) — LQ/SQ 尾快照的推进、MDP 违例上报（squash 来源之一）
- [`cache.md`](cache.md) — store 提交落缓存 / load 回填的存储侧行为
- 仓库根目录 `docs/benchmarks.md` — 各用例 x10/clock 参考值（golden 唯一来源）

## 参考文献

1. <a id="back-ref-1"></a>R. M. Tomasulo, “An Efficient Algorithm for
   Exploiting Multiple Arithmetic Units,” *IBM Journal of Research and
   Development*, vol. 11, no. 1, pp. 25–33, 1967.
   https://doi.org/10.1147/rd.111.0025
2. <a id="back-ref-2"></a>J. E. Smith and A. R. Pleszkun, “Implementation
   of Precise Interrupts in Pipelined Processors,” in *Proceedings of ISCA ’85*,
   pp. 36–44, 1985. https://doi.org/10.1145/327070.327125
3. <a id="back-ref-3"></a>K. C. Yeager, “The MIPS R10000 Superscalar
   Microprocessor,” *IEEE Micro*, vol. 16, no. 2, pp. 28–41, 1996.
   https://doi.org/10.1109/40.491460
4. <a id="back-ref-4"></a>RISC-V International, *The RISC-V Instruction Set
   Manual, Volume I: Unprivileged Architecture*, “M” Extension Version 2.0,
   2026. https://docs.riscv.org/reference/isa/v20260120/unpriv/m-st-ext.html
5. <a id="back-ref-5"></a>A. D. Booth, “A Signed Binary Multiplication
   Technique,” *The Quarterly Journal of Mechanics and Applied Mathematics*,
   vol. 4, no. 2, pp. 236–240, 1951.
   https://doi.org/10.1093/qjmam/4.2.236
6. <a id="back-ref-6"></a>O. L. MacSorley, “High-Speed Arithmetic in Binary
   Computers,” *Proceedings of the IRE*, vol. 49, no. 1, pp. 67–91, 1961.
   https://doi.org/10.1109/JRPROC.1961.287779
7. <a id="back-ref-7"></a>C. S. Wallace, “A Suggestion for a Fast
   Multiplier,” *IEEE Transactions on Electronic Computers*, vol. EC-13,
   no. 1, pp. 14–17, 1964. https://doi.org/10.1109/PGEC.1964.263830
8. <a id="back-ref-8"></a>Behrooz Parhami, *Computer Arithmetic: Algorithms
   and Hardware Designs*, 2nd ed., Oxford University Press, 2010.
   https://www.ece.ucsb.edu/~parhami/text_comp_arit.htm
9. <a id="back-ref-9"></a>J. E. Robertson, “A New Class of Digital Division
   Methods,” *IRE Transactions on Electronic Computers*, vol. EC-7, no. 3,
   pp. 218–222, 1958. https://doi.org/10.1109/TEC.1958.5222579
10. <a id="back-ref-10"></a>K. D. Tocher, “Techniques of Multiplication and
    Division for Automatic Binary Computers,” *The Quarterly Journal of
    Mechanics and Applied Mathematics*, vol. 11, no. 3, pp. 364–384, 1958.
    https://doi.org/10.1093/qjmam/11.3.364
11. <a id="back-ref-11"></a>D. E. Atkins, “Higher-Radix Division Using
    Estimates of the Divisor and Partial Remainders,” *IEEE Transactions on
    Computers*, vol. C-17, no. 10, pp. 925–934, 1968.
    https://doi.org/10.1109/TC.1968.226439
12. <a id="back-ref-12"></a>M. D. Ercegovac and T. Lang, *Division and Square
    Root: Digit-Recurrence Algorithms and Implementations*, Kluwer Academic
    Publishers, 1994. https://link.springer.com/book/9780792394389
13. <a id="back-ref-13"></a>M. D. Ercegovac and T. Lang, “On-the-Fly
    Conversion of Redundant into Conventional Representations,” *IEEE
    Transactions on Computers*, vol. C-36, no. 7, pp. 895–897, 1987.
    https://doi.org/10.1109/TC.1987.1676986
14. <a id="back-ref-14"></a>T. Coe, T. Mathisen, C. Moler, and V. Pratt,
    “Computational Aspects of the Pentium Affair,” *IEEE Computational Science
    & Engineering*, vol. 2, no. 1, pp. 18–30, 1995.
    https://doi.org/10.1109/99.372929
15. <a id="back-ref-15"></a>ISO/IEC 14882:2020, *Programming Languages — C++*,
    §7.6.7 `[expr.shift]`, 2020. https://eel.is/c++draft/expr.shift
16. <a id="back-ref-16"></a>IEEE, *IEEE Standard for Floating-Point
    Arithmetic*, IEEE Std 754-2019, 2019.
    https://doi.org/10.1109/IEEESTD.2019.8766229
17. <a id="back-ref-17"></a>纸上谈芯, “基4 SRT除法器,” 知乎专栏, 2021.
    https://zhuanlan.zhihu.com/p/397563781
