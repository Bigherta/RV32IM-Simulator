# 后端：重命名、执行、写回与恢复

本文是当前后端契约入口；公共容量见[共同规范](../../CPU-SYNC-SPEC.md)。
MUL/SRT的完整推导、伪代码、位宽表和调试轨迹保留在[详细原文](_archive/20261018/docs/backend.md)的§4.3～4.6、附A/B；
**这些算术定义继续有效，原文的四路CDB、旧容量和结束协议等集成说明已由本文覆盖。**

## 1. 重命名与身份

- 单宽Issue；ROB16、PRF48、RAT32。ROB tag为epoch＋slot共5 bit，物理寄存器tag为6 bit，P0不分配。
- Issue预留ROB、目的物理寄存器、目标RS及所需LQ/SQ槽；LINK目的值为PC+4。
- PRF使用FIFO自由队列；恢复按年龄回收年轻`newPhy`，正常退休回收`oldPhy`，不改分配顺序。
- 迟到完成必须匹配完整tag和ROB存活窗口，不能因槽内残留tag接受已退休/取消指令的写回。

## 2. RS与派发

七池容量：Integer/MUL/DIV/Branch/Load/StoreAddr/StoreValue＝2/1/1/2/2/2/2。
各池按最老就绪指令选择；Load/StoreAddr共用一个AGU口，StoreValue独立交付，每拍最多2项。

新入RS、当前拍WB才唤醒的指令，都从下一拍参与派发；不增加同拍WB→依赖派发旁路。
恢复广播拍允许严格更老、已就绪且被执行端真实接收的工作，边界保留不派发、年轻项取消。
后续四拍重放期间RS/FU冻结。

软件从PRF派生RS操作数，RTL缓存ready/value；比较有效语义，不裸比释放槽残留数据。

## 3. 结果与退休

- 两条物理CDB：ALU独占CDB0；MUL/DIV/Load按完整ROB年龄竞争CDB1。
- 未获授权者保持结果和背压；消费者资格、生产者释放和授权是同一事件。
- PRF/ROB拍末接收WB，退休读取旧ready，每拍最多提交1条。
- Load广播只置已广播标记，实际退休才回收LQ；Store退休后可在SQ等待Cache接受。
- LQ内部保存raw，CDB读出时按存活行的宽度/符号扩展，不把raw缓存直接当作架构值。

## 4. 执行单元

### 4.1 ALU

n拍选择操作数并运算，拍末保存单槽结果；n+1拍CDB，n+2拍依赖最早派发。
不加执行输入寄存级；结果被消费或取消时可同拍重填。

### 4.2 AGU与BRU

AGU共享Load/Store地址口，32-bit模加法；n拍保存地址/tag/目标槽，n+1拍交付LSQ，n+2拍地址ready可见。
LSQ更新须校验目标存活槽与生产者完整tag。

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

## 5. 四拍恢复

广播沿同步读已提交RAT/RAS基线，广播拍禁止退休/分配。随后固定四拍，每拍4个ROB槽：
RAT/RAS重放存活前缀至边界，PRF回收严格年轻后缀；即使窗口不足16项也不提前结束。

ROB输出共享窄窗，RAT/PRF读tag/rd/newPhy，RAS读tag/PC/已解码hint。head与边界只处理一次，回收保持FIFO顺序。
重放期间冻结前端、RS/FU、ROB、FlushArbiter及CDB消费；LSQ返回、Cache/AXI和已提交Store继续。

已提交RAT为1×192 bit、6-bit掩码写SRAM；RAS为1×512 bit、32-bit掩码写SRAM，valid屏蔽复位未初始化内容。
投机映射/栈、PRF数据和自由队列仍是寄存状态。

## 6. 验证与限制

主树保持只读拍初快照/逐字段写目标；模板保持Register单写/全部work后统一sync。
当前[25例逐拍与专项验收](../../sync/RTL-TO-SW-SYNC-20261018.md)不等于所有异常域或所有参数组合已证明。
课程退出按[访存协议](memory.md)执行，不采用旧的全dirty扫描结束。
