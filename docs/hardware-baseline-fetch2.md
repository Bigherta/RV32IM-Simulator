# 当前硬件同步基线：2/4/4/4，双ALU/三路写回

主树与模板源码同步生产双取指RTL，ROB32/PRF64/checkpoint64、FQ8/IQ8、I$2包、AXIFetch4行、D$8 KiB、单AGU不变。
双ALU结果各独占一个写回口，Load/MUL/DIV竞争第三口；单一跳转预测/重定向口只消费最老控制结果。
Integer/Branch/Load可读真实授权三口WB，双整数派发的实际离站槽可同拍复用；新项不当拍执行。

原六perf答案、实际退休计数、两树init/core/drain相同。两软件各438378拍的11项观察与原生RTL逐拍相同，
仅代表该语料/这些观察字段，不是完整内部状态或全输入等价证明。失败/旧草稿和原表均保留。
源码、独立构建、生产复验状态见[持久证据](../../sync/evidence/software-fetch2-sync-20261030-r01/README.md)。

| case | actual retired | core | drain | course total |
|---|---:|---:|---:|---:|
| perf_median | 6961 | 9907 | 14 | 9922 |
| perf_multiply | 21722 | 19829 | 14 | 19844 |
| perf_vvadd | 4524 | 4553 | 24 | 4578 |
| perf_towers | 5278 | 6872 | 14 | 6887 |
| perf_qsort | 139900 | 191207 | 14 | 191222 |
| perf_rsort | 195719 | 205916 | 14 | 205931 |

每例init=1。软件历史`clock`/`timing.total=core+drain`不含boot，RTL课程总周期包括boot；
专用入口核对`course total=init+core+drain`，保留两种区间，不随意加减周期或修改旧golden。
`ipc:`仍是退休冻结的core区间统计，不能当课程IPC或硬件PPA。课程几何IPC0.859943738223来自原生RTL证据。

当前只运行原六perf，不执行额外课程例/冒烟/全量回归。旧25例周期表属于历史1/1/1/2默认；
旧测试脚本读取该表，不用于新默认。新构建及六例入口见[README](../README.md#构建与验证)。
Slave、镜像、答案、指令数及原有code均未覆盖；软件同步不修改正在综合的硬件快照。
