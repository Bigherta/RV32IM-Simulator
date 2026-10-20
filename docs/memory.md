# 访存：LQ/SQ、转发与退出

活动路径：`AGU/StoreValue → LQ/SQ → MemArbiter → DCache → AXIData → AXIBus`。
LQ/SQ各8项，采用包含epoch的队列身份；配置见[共同规范](../../CPU-SYNC-SPEC.md)。

## Store生命周期

1. Issue预留SQ槽；地址、数据由独立路径就绪，StoreValue不占AGU。
2. 旧SQ地址/数据齐备时，组合输出ROB slot/epoch完成位图；ROB校验身份/type/恢复年龄后置ready。
3. ROB按序退休Store，在原SQ槽标记committed；不等待Cache接受。
4. 按SQ顺序发送已提交或本拍实际退休的队首Store；Cache实际接受才释放SQ槽。

已提交Store可跨ROB回绕存活，不用ROB年龄代替SQ发送顺序，也不被分支恢复取消。
双提交可同拍标记两个SQ项，实际Cache发送仍每拍一个。发送资格按同一组实际退休tag计算，
即便Store位于第二lane也可在成为SQ队首时同拍发送；不会把退休提议当作实际提交。

## Load生命周期

1. Issue预留LQ槽，AGU结果经存活槽和完整tag检查后更新地址。
2. 尝试Store转发；不能转发时，检查是否允许进入Cache。
3. Cache/转发返回保存raw，CDB读出时按该行宽度/符号扩展。
4. 与MUL/DIV竞争共享CDB；授权只标已广播，实际ROB退休才回收LQ。

迟到Cache响应必须检查原运输身份及取消状态；新分配初始化不能被同拍旧响应覆盖。
Cache完成、CDB广播和ROB退休是三个不同事件。
同拍两个Load退休时按提交顺序释放两个LQ项；新分配仍不借用本拍释放的满队列额度。

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

四拍恢复暂停AGU/StoreValue新事件、Load广播和退休，但LSQ保存真实返回、已提交Store/Cache/AXI继续。
不重复交付冻结前的执行事件，也不冻结整个内存系统。

## 课程退出

SW全字写`0x80000000`，`WSTRB=0xf`。该Store按序退休后冻结核心，继续已提交SQ/Cache/AXI；
MMIO等待AXIData读写均空后独立发送，不查询/分配Cache，外部对应B握手后捕获完整结果并结束。
不全dirty写回，不用退休或AW/W握手冒充结束。
退出Store可以位于任一退休lane；它及其之前的老指令计入退休，更年轻lane必须截断。

软件total不含独立boot1拍；`total=core+drain`，IPC分母为core，退休数包含退出Store。
外部Slave仍每通道16项、默认10拍、256 MiB，与主机8条行信用分开。

[验证记录](../../sync/RTL-TO-SW-SYNC-20261018.md) · [Cache细节](cache.md) · [历史原文](_archive/20261018/docs/memory.md)
