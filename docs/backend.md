# 后端：重命名、执行、写回与恢复

**2026-10-29当前合同**：主树已同步模板生产版多路发射、BPU、8 KiB D$及Load RS授权WB/实际离站复用。
两树均支持独立1/2/4宽度，默认1/1/1/2；本轮验证状态见[同步记录](tree-sync-20261029.md)。
以下2026-10-27及更早阶段的“主树不变/仍单发射”仅为历史，不描述当前源码。

## 历史阶段记录

2026-10-27双向同步r02已将RTL的Load RS授权WB当拍派发/值旁路及issue1/2离站复用写入模板；
issue4既有Load复用保留。模板/RTL的Load与Integer/Branch都可用真实授权WB，StoreAddr/StoreValue/MUL/DIV
仍按旧就绪；参考主树不变。旧空槽优先，只有共享AGU实际选中接收的Load才腾位，多lane不得重复占槽，
RS原push优先/单写保持。没有新增执行/CDB/AGU口；采用后复验见[双向记录](../../sync/MUTUAL-OPT-SYNC-20261027.md)。

2026-10-27双向同步r01只补模板D$接受沿同步读；不改RS、执行、WB、提交或恢复。
Load RS的WB当拍派发/issue1及2离站复用仍列后续项；模板issue4已有真实Load离站复用。
参考主树及RTL源码保持，当前证据与范围见[双向记录](../../sync/MUTUAL-OPT-SYNC-20261027.md)。

2026-10-27：本轮只在模板与 RTL 采用 BPU 预测时上下文和 D$8 KiB，参考主树源码不变。
没有新增执行、CDB、提交或恢复级；模板继续 Register 单写/统一 sync，既有宽度选项保留。
下面历史周期对齐结论不能直接用于新配置；当前验收见[采用记录](../../sync/ADOPT-BPU-DCACHE-20261027.md)。

2026-10-26模板同步RTL四宽分配：`CPU_ISSUE_WIDTH=1/2/4`（旧rename宏为同义别名），
四宽配套ROB32/PRF64/checkpoint64、6-bit完整tag和8槽恢复窗；最近实际写者RAW/LINK、WAW链、
稀疏PRF/LSQ排名和七池RS与ROB共用真实连续前缀。单/双宽原路径及主树不改，不新增执行或CDB口。
见[同步记录](../../sync/TEMPLATE-WIDE4-20261026.md)；不将历史三端或PPA结论扩大到此配置。

2026-10-25模板与RTL可选扩展：`CPU_RENAME_WIDTH=2`接通两路顺序重命名/资源分配，支持双Load和双Store入队；默认仍为1，主树仍单宽。组内RAW转发新物理tag、WAW年轻映射胜出，PRF/ROB/RS/LSQ使用同一组实际发射事件。RTL值缓存RS对组内LINK源捕获PC+4/ready，对应模板下拍PRF查询；并非新项本拍执行。详见[模板接线](../../sync/DUAL-RENAME-WIRING-20261025.md)和[RTL验收](../../sync/RTL-DUAL-RENAME-20261025.md)。本文其余单宽描述适用于主树和模板默认配置。

独立译码宽度1/2只改变前端前缀传递及可选FQ/IQ容量，rename宽度另行配置。Integer/Branch已同步RTL的RS4、授权WB旁路与离站复用；历史RS阶段验证范围见[RS记录](../../sync/RS-RTL-TO-SW-20261024.md)，当前结果以各配置最新验收为准，不扩大为全域三端等价。

本文是当前后端契约入口；公共容量见[共同规范](../../CPU-SYNC-SPEC.md)。
MUL/SRT的完整推导、伪代码、位宽表和调试轨迹保留在[详细原文](_archive/20261018/docs/backend.md)的§4.3～4.6、附A/B；
**这些算术定义继续有效，原文的四路CDB、旧容量和结束协议等集成说明已由本文覆盖。**

## 1. 当前重命名与身份

- Issue独立1/2/4，`CPU_RENAME_WIDTH`为`CPU_ISSUE_WIDTH`同义别名。默认ROB16/PRF48，issue4为ROB32/PRF64；RAT32。
- ROB tag为epoch＋slot（默认5 bit、issue4为6 bit），P0不分配；所有消费者共用实际连续前缀。
- 组内最近实际写者决定RAW及WAW oldPhy链，PRF/LSQ按本池排名分配；只借旧信用，不借当拍退休/回收。
- Issue预留ROB、目的物理寄存器、目标RS及所需LQ/SQ槽；LINK目的值为PC+4。
- PRF使用FIFO自由队列；恢复按年龄回收年轻`newPhy`，正常退休回收`oldPhy`，不改分配顺序。
- 迟到完成必须匹配完整tag和ROB存活窗口，不能因槽内残留tag接受已退休/取消指令的写回。

## 2. RS与派发

七池默认容量：Integer/MUL/DIV/Branch/Load/StoreAddr/StoreValue＝4/1/1/4/2/2/2。Integer/Branch容量可用同名CMake/编译宏`CPU_INT_RS_ENTRIES`、`CPU_BRANCH_RS_ENTRIES`覆盖；当前索引上限分别16/4，不要求非环形RS容量为二次幂。
各池按最老就绪指令选择；Load/StoreAddr共用一个AGU口，StoreValue独立交付，每拍最多2项。

新入RS项仍只能从下一拍参与派发。Integer/Branch/Load旧活动项可由本拍已授权且完整身份认证的两条CDB写口补足就绪和值；旧ready值优先，P0常量不匹配，重复命中取低编号口。MUL/DIV/StoreAddr/StoreValue仍只读旧就绪。
Integer/Branch/Load只有被执行端真实接受的旧项才能同拍复用；Load必须赢得共享AGU仲裁。issue1/2优先旧空槽，issue4按可用槽位排名；每槽只能分配一次。分配覆盖释放，主树tick只读快照/Input写目标，模板逐Register单写。
恢复广播拍允许严格更老、已就绪且被执行端真实接收的工作，边界保留不派发、年轻项取消。
后续原存活窗口重放期间RS/FU冻结。

软件从PRF派生RS操作数，RTL缓存ready/value；比较有效语义，不裸比释放槽残留数据。

## 3. 结果与退休

- 两条物理CDB：ALU独占CDB0；MUL/DIV/Load按完整ROB年龄竞争CDB1。
- 未获授权者保持结果和背压；消费者资格、生产者释放和授权是同一事件。
- PRF/ROB拍末接收WB，退休读取旧ready；提交宽度独立1/2/4，默认最多2条连续队首指令。
- `CommitPlan`（模板为ROB输出的`CommitPort`数组）统一驱动ROB、RAT、PRF、LQ、SQ、RAS和计数。
  后一lane不能绕过前一lane；HALT、异常、退出Store截断后续提交。恢复广播及窗口重放仍禁提交。
- 同rd的已提交映射以年轻lane为准；每条oldPhy按年龄追加自由FIFO；本拍回收下拍才可分配。
  已提交RAT/RAS仍用单端口宽字SRAM，先合并写掩码/同槽年轻优先，再写一次。
- Load广播只置已广播标记，实际退休才回收LQ；Store退休后可在SQ等待Cache接受。
- LQ内部保存raw，CDB读出时按存活行的宽度/符号扩展，不把raw缓存直接当作架构值。

## 4. 执行单元

### 4.1 ALU

n拍选择操作数并运算，拍末保存单槽结果；n+1拍CDB，依赖的Integer/Branch/Load可在实际授权的n+1拍旁路派发；其他池最早n+2拍。
不加执行输入寄存级；结果被消费或取消时可同拍重填。

### 4.2 AGU与BRU

AGU共享Load/Store地址口，32-bit模加法；n拍保存地址/tag/目标槽，n+1拍交付LSQ，n+2拍地址ready可见。Load B允许n+1拍的真实旧AGU结果在登记前参与Cache请求；只对已预留活动LQ行、完整tag匹配且无完整SQ转发的Load生效。
LSQ更新须校验目标存活槽与生产者完整tag。
Load C允许Cache返回在捕获raw的同拍竞争CDB，必须检查活动FETCHING行/完整tag/未广播/flush并服从完整Store转发优先。返回与旧buffer结果按年龄选择后再与MUL/DIV竞争，未授权仍捕获raw，授权才标已广播；不是绕过CDB直接唤醒。

BRU保存真实条件结果和next-PC；ALU处理JAL/JALR的链接值/目标。错误预测经FlushArbiter排队后按年龄广播，
不能用next-PC等于PC+4推断分支未跳转。软件BRU四槽、RTL单槽是既有表示差异。

### 4.3 MUL：radix-4 Booth＋CSA

三级弹性流水：19行部分积 → CSA压缩为S/C → 最终64-bit加法结果。
压缩树为19→13→9→6→4→3→2；负Booth行用反码和稀疏+1修正，保留高乘的符号/无符号修正。
`MUL`取低32位，其余高乘取高32位。

无争用时n拍partial、n+1 CSA、n+2 result、n+3 CDB、n+4依赖最早派发。
准入从实际CDB授权向前递推；受阻级valid/data/tag/op整体保持，不能假设共享CDB恒授权。

### 4.4 DIV 实现：SRT radix-4

递推`W[j+1] = 4W[j] - qD`，`q∈{-2,-1,0,1,2}`，保持`|W|≤2D/3`。
归一化、奇偶shift、QDS、on-the-fly商转换及收尾修正均由移位/加减/CSA实现，数据通路不使用宿主乘除取模。

- `B_d=32+shiftD`，`SW=B_d+1`，`PW=SW+2`；S/C寄存器保存的是**P=4W域**，宽35/36 bit。
- QDS使用`bitsD=5, estBits=9`；估计切片按`PW-estBits`取高位，先掩掉估计加法进位再补码折叠。
- CSA递推按完整PW回卷，不能将S/C截成32 bit；on-the-fly拼接避免负字段按位或污染高位。
- 除零、有符号溢出等快速路径独立处理；一般路径`L=ceil(align/2)`次loop，无背压时n+L+3拍CDB，快速路径n+1拍。
- 结果消费同拍可接收下一项；flush取消严格年轻，边界结果无授权时保持。

精确QDS判据、商转换表、收尾和位级黄金轨迹请查[详细原文§4.4～4.6、附A/B](_archive/20261018/docs/backend.md)，
修改DIV前必须核对这些定义及两树`src/DIV/`、`src/include/DIV.hpp`。

### 4.5 除法参数速查

| shiftD | B_d / SW / PW | QDS切片起点PW−9 |
|---|---|---|
| 0 | 32 / 33 / 35 | 26 |
| 1 | 33 / 34 / 36 | 27 |

两档均为`bitsD=5, estBits=9, estPShift=1`，dSlice范围16～31。完整阈值和折算单位见详细原文§4.4/4.5。

### 4.6 实现陷阱

QDS必须按P域切片、先丢估计加法进位再折叠；递推S/C不能截成32 bit，比较两侧保持同号。
on-the-fly的q=0分支不能把负数字段直接按位或入商寄存器。反例、断言条件和黄金轨迹见详细原文§4.6、附B。

## 5. 原存活窗口恢复

广播沿同步读已提交RAT/RAS基线，广播拍禁止退休/分配。随后每拍4个ROB槽（issue4为8槽）：
RAT/RAS重放存活前缀至边界，PRF回收严格年轻后缀；按广播前原liveCount在`offset + RECOVERY_WIDTH >= liveCount`时结束，省略空beat。同步基线读及至少一个重放beat保持，不能只按保留前缀长度结束。

ROB输出共享窄窗，RAT/PRF读tag/rd/newPhy，RAS读tag/PC/已解码hint。head与边界只处理一次，回收保持FIFO顺序。
重放期间冻结RS/FU、ROB、FlushArbiter及CDB消费；默认单取冻结前端，双取仅对当前PC的非控制流hit开放Fetch/FQ/IQ，资格见[前端](frontend.md)。LSQ返回、Cache/AXI和已提交Store继续。

已提交RAT为1×192 bit、6-bit掩码写SRAM；默认RAS为1×256 bit、32-bit掩码写SRAM，valid屏蔽复位未初始化内容。
投机映射/栈、PRF数据和自由队列仍是寄存状态。

## 6. 验证与限制

主树保持只读拍初快照/逐字段写目标；模板保持Register单写/全部work后统一sync。
当前[25例逐拍与专项验收](../../sync/RTL-TO-SW-SYNC-20261018.md)不等于所有异常域或所有参数组合已证明。
课程退出按[访存协议](memory.md)执行，不采用旧的全dirty扫描结束。
