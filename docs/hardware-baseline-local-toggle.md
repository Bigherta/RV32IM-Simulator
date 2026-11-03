# 2026-11-01 第18轮评分最优生产基线

按用户“这组提交不要，频率下降太多，转成使用我当前的最优版本”的要求，正式采用
`round18-local-toggle-20261101-r01`。正式指针为
[`current-production.json`](../../sync/evidence/current-production.json)，
[硬件采用证书](../../sync/evidence/local-toggle-production-adoption-20261101-r01/adoption.json)与
[两软件复验证书](../../sync/evidence/software-local-toggle-sync-20261101-r01.json)保存完整身份链。
远端`032fb13`评测已取消，该提交未合并。

## 指标与选择

| 指标 | 当前版本 | 相对第17轮 |
|---|---:|---:|
| 面积 | 34617.305707 μm² | +0.096121% |
| Fmax | 317.815022 MHz | +0.806952% |
| 六项几何IPC | 0.903890966873 | 持平 |
| IPC⁴ / Area² × Fmax | 1.770321235297e-7 | +0.613439% |

面积严格`<36000`、Fmax`>300`通过；长期IPC`>1.0985`尚未达到。
[r23最优归档](../../sync/evidence/best-results-retained-20261101-r23/retention.json)继承r22所有候选并绑定新生产：
本版为已审计评分最优，lane0分支旁路为已审计IPC最高；无全部长期门槛达标最优。

本次按44项RTL及SRAM源身份复用第18轮已审计的整核综合/ABC/STA测量，
采用过程没有新增综合轮次。生产仅改`rv32_bpu_direction.sv`的3位local计数器更新，
共享饱和判定和进位/借位翻转；19组任意状态SAT及4项负向已在候选资格审计中通过。

两软件使用相同的0至7饱和增减语义，126项构建输入与第17轮同步证书逐字节一致。
默认2/4/4/4、双ALU/三CDB、单AGU、PRF单行SRAM、PC chooser、BTB32和8槽恢复配置见
[第17轮实现记录](hardware-baseline-prf-microbank.md#本次软件实现)。

## 原六项新生产复验

| perf | 实际退休数 | 课程周期 | IPC | 相对第17轮IPC |
|---|---:|---:|---:|---:|
| median | 6961 | 9259 | 0.751809051 | 0% |
| multiply | 21722 | 19595 | 1.108548099 | 0% |
| vvadd | 4524 | 4363 | 1.036901215 | 0% |
| towers | 5278 | 6636 | 0.795358650 | 0% |
| qsort | 139900 | 186111 | 0.751701941 | 0% |
| rsort | 195719 | 185415 | 1.055572634 | 0% |

- 生产RTL重新构建，配置断言及lint通过；六项普通日志、374104条退休记录及完整诊断日志与已测候选逐字节一致。
- 主树/模板各全新Release和`_DEBUG -O2 -UNDEBUG`构建，共四构建24次原六项运行；答案均0，实际退休数、`init+core+drain`课程周期全匹配生产RTL。
- `init=1`，软件历史`timing.total=core+drain`；不更改旧计数器或黄金周期表。
- 第17轮411373拍PRF状态三端观察保留为历史证据，本次复验范围为上述日志和统计，未重跑PRF逐拍观察。

## 证据与复验

工作区`.cpu2026-synthesis/local-toggle-production-adoption-20261101-r01/`保存生产改前/改后源码、配置、构建及退休日志。
`.cpu2026-synthesis/software-local-toggle-sync-20261101-r01/`保存两软件源码副本、四构建、`release-report/report.json`、`assert-report/report.json`及完成证书。
最优源码/模型/trace/摘要保留；综合大产物遵循最近3次规则。

全新外部构建方式沿用[构建示例](hardware-baseline-prf-microbank.md#证据与复验)，每次选择新F盘目录。
已有本次构建可从工作区根目录运行（输出目录必须尚不存在）：

```bash
python3 -B sync/check_current_production_software.py \
  --main .cpu2026-synthesis/software-local-toggle-sync-20261101-r01/release-main/bin/code \
  --template .cpu2026-synthesis/software-local-toggle-sync-20261101-r01/release-template/bin/code \
  --out .cpu2026-synthesis/software-local-toggle-recheck-new
```

仅验默认配置原六perf；镜像、答案、退休参考、Slave、旧黄金表、已有`code`和git索引均保留。
