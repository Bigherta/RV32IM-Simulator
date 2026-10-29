# 访存：LQ/SQ、转发与退出

**2026-10-29当前合同**：主树已同步模板生产版Load RS授权WB/实际离站复用、1/2/4路共同分配前缀及8 KiB D$。
下面旧阶段“主树不变/仍单宽/未移植”仅为历史；当前验证见[同步记录](tree-sync-20261029.md)。

## 历史阶段记录

2026-10-27双向同步r02：模板Load→AGU就绪与值现在共同使用现有真实授权WB，补齐RTL；
只对旧Load项生效，新分配项不当拍执行。issue1/2支持实际Load离站复用，issue4已有复用保留。
被StoreAddr抢占、消费者未接受或冻结的Load不腾位；StoreAddr/StoreValue就绪、LSQ/Cache/AXI/Slave不改。
参考主树源码保持，生产复验状态见[双向记录](../../sync/MUTUAL-OPT-SYNC-20261027.md)。

2026-10-27双向同步r01：模板补齐RTL已有的空队列Load接受沿同步SRAM读，
同沿仍占原请求槽，暖命中最早下一拍响应；无组合请求→响应旁路。
读offset/size随原身份保存；非空队列、Store/MMIO、MSHR及AXI/Slave合同保持。
参考主树未移植，RTL源码未改。验证/采用后复验状态见[双向记录](../../sync/MUTUAL-OPT-SYNC-20261027.md)。

2026-10-27：模板与 RTL 的 D$ 已改为 8 KiB，参考主树仍为 4 KiB；LQ/SQ、单 AGU、
请求/响应端口、AXI 信用和外部 Slave 服务规则不变。尚未采用按部分 R beat 提前返回的
critical-word 旁路，仍须完整行回复就绪才可按原首回填拍响应。生产复验状态见[采用记录](../../sync/ADOPT-BPU-DCACHE-20261027.md)。

2026-10-26模板四宽：所有消费者共享0..4条有序发射事件，Load/Store分别使用本池实际分配排名；
尾快照含自身，LSQ只借旧信用。原内存RS各2项和单AGU限制保持，未扩Cache/AXI/Slave，主树仍单宽。
验收边界见[四宽同步记录](../../sync/TEMPLATE-WIDE4-20261026.md)。

2026-10-25模板可选双重命名：LQ/SQ分别支持同拍0/1/2项顺序分配，满判断只用旧态，不提前借用本拍释放。每条ROB保存包含自身及更老lane的LSQ尾边界，身份含epoch；AGU/Cache请求及SQ发送带宽不变。随后RTL也已接双Load/双Store分配，按既有完整tag/活动位与committed状态恢复，不复制软件尾快照字段；主树仍单宽。见[模板接线](../../sync/DUAL-RENAME-WIRING-20261025.md)和[RTL验收](../../sync/RTL-DUAL-RENAME-20261025.md)。

前端独立译码宽度与FQ/IQ容量不改变本文LQ/SQ或访存带宽。Load A/B/C已同步D$连续lookup、旧AGU提前请求和返回当拍CDB；双取安全hit恢复期提前取指只开放前端，不开放AGU/StoreValue/Load广播或退休。验证配置边界见[实施记录](../../sync/WIDTH-DECODE-SYNC-20261024.md)。

## 当前访存合同

Integer/Branch/Load使用当拍已授权WB派发；Load的AGU操作数和值使用同一授权旁路。StoreAddr/StoreValue仍只读旧PRF，Load B提前的是已寄存AGU结果的请求。

LSQ按同一实际Issue前缀的本池排名分配，ROB尾边界包含自身和更老lane；只使用旧信用，不借当拍退休/发送释放。每条保留完整ROB tag和队列epoch，恢复不改变已提交Store资格。

Load A允许D$命中响应与下一同步lookup重叠。B只对完整身份匹配的旧活动LQ行提前请求，完整SQ转发时不发冗余读；C允许合格Cache返回与旧buffer结果按年龄选择后竞争共享CDB，未授权仍捕获raw，下拍继续竞争，实际授权只广播一次。完整转发优先，已ready/已广播行不被通知再次唤醒。

活动路径：`AGU/StoreValue → LQ/SQ → MemArbiter → DCache → AXIData → AXIBus`。
LQ/SQ各8项，采用包含epoch的队列身份；配置见[共同规范](../../CPU-SYNC-SPEC.md)。

## Store生命周期

1. Issue预留SQ槽；地址、数据由独立路径就绪，StoreValue不占AGU。
2. 旧SQ地址/数据齐备时，组合输出ROB slot/epoch完成位图；ROB校验身份/type/恢复年龄后置ready。
3. ROB按序退休Store，在原SQ槽标记committed；不等待Cache接受。
4. 按SQ顺序发送已提交或本拍实际退休的队首Store；Cache实际接受才释放SQ槽。

已提交Store可跨ROB回绕存活，不用ROB年龄代替SQ发送顺序，也不被分支恢复取消。
多提交可同拍标记多个SQ项，实际Cache发送仍每拍一个。发送资格按同一组实际退休tag计算，
即便Store位于非首lane也可在成为SQ队首时同拍发送；不会把退休提议当作实际提交。

## Load生命周期

1. Issue预留LQ槽，AGU结果经存活槽和完整tag检查后更新地址。
2. 尝试Store转发；不能转发时，检查是否允许进入Cache。
3. Cache/转发返回保存raw，CDB读出时按该行宽度/符号扩展。
4. 与MUL/DIV竞争共享CDB；授权只标已广播，实际ROB退休才回收LQ。

迟到Cache响应必须检查原运输身份及取消状态；新分配初始化不能被同拍旧响应覆盖。
Cache完成、CDB广播和ROB退休是三个不同事件。
多个Load同拍退休时按提交顺序释放LQ项；新分配仍不借用本拍释放的满队列额度。

## 转发与保守准入

- 从更老或已提交Store中选最年轻的字节重叠来源，要求**单源完整覆盖**且数据就绪；不做多源拼接。
- 未知地址Store按既定年龄支配规则阻塞转发；不能仅因另一个已知同址Store就绕过它。
- 未转发Load遇到地址未知或任意字节重叠的更老未提交Store时，不准入Cache；不重叠字节可放行。
- Store地址/数据通知读取旧SQ，通知的地址、宽度和完整tag须与生产者一致。
- 字节重叠使用同word/相邻word分类，保留非对齐和32-bit地址回绕边界。

## AXI与恢复

AXIData共享8项描述符/word payload信用；读写顺序环各16项，五个epoch进度游标分别跟踪AR/R/AW/W/B。
AW/W独立，普通D$行各需8次32-bit传输；收齐整行R/B后才向Cache呈现完成，并保持至Cache消费。
旧满停收、最后信用写优先；本拍消费释放的槽不被新请求同拍复用。

原存活窗口恢复暂停AGU/StoreValue新事件、Load广播和退休，但LSQ保存真实返回、已提交Store/Cache/AXI继续。恢复按原liveCount省略空beat，不改访存返回身份和已提交Store资格。
不重复交付冻结前的执行事件，也不冻结整个内存系统。

## 课程退出

SW全字写`0x80000000`，`WSTRB=0xf`。该Store按序退休后冻结核心，继续已提交SQ/Cache/AXI；
MMIO等待AXIData读写均空后独立发送，不查询/分配Cache，外部对应B握手后捕获完整结果并结束。
不全dirty写回，不用退休或AW/W握手冒充结束。
退出Store可以位于任一退休lane；它及其之前的老指令计入退休，更年轻lane必须截断。

软件total不含独立boot1拍；`total=core+drain`，IPC分母为core，退休数包含退出Store。
外部Slave仍每通道16项、默认10拍、256 MiB，与主机8条行信用分开。

[验证记录](../../sync/RTL-TO-SW-SYNC-20261018.md) · [Cache细节](cache.md) · [历史原文](_archive/20261018/docs/memory.md)
