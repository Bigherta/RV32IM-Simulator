# 第21轮无冲突Load接受沿查询生产基线

用户要求“生产采纳最优结果”，正式采用`round21-accepted-disjoint-load-20261102-r02`。
身份以[生产指针](../../sync/evidence/current-production.json)、
[硬件采用证书](../../sync/evidence/disjoint-load-production-adoption-20261102-r01/adoption.json)与
[软件证书](../../sync/evidence/software-disjoint-load-sync-20261102-r01.json)为准。

## 指标与权衡

| 指标 | 当前生产 | 相对第20轮 |
|---|---:|---:|
| 面积 | 34587.314647 μm² | −0.008809% |
| Fmax | 308.805790 MHz | −2.412545% |
| 六项几何IPC | 0.930507527921 | +0.788070% |
| IPC⁴ / Area² × Fmax | 1.935224301924e-7 | +0.717984% |

面积严格<36000、Fmax>300通过；长期几何IPC>1.0985未达。
按各自Fmax估算IPC×Fmax下降1.643488%，非墙钟吞吐实测。
median退化0.846006%、其余五项改善，用户在获知权衡后明确采用评分/IPC最优候选。
44项生产RTL与公共SRAM身份认证引用第21轮已审计整核综合/ABC/STA，不拼接异源指标，采用阶段不重跑PPA。

## 接受沿同步读合同

新Load不再必须等待队列变空：所有旧Store均不在同一个32字节缓存行，且无退出MMIO、
目标set未锁定、无旧lookup/fill/Store写时，可使用空闲同步读沿。
读流水必须空闲，或旧Load命中已实际交付。普通请求槽分配与读身份、offset、size、valid-way快照保持，
最早下一拍响应；不读同拍SRAM新写值，不是跨bank同拍Store写与Load读。
同一行内不重叠字节仍保守阻止；LSU未知地址消歧、Store转发和按序排空保持。
PRF共享写仲裁及两项Store位置缓冲继续适用，缓存容量/端口、MSHR、AXI和Slave均未修改。

## 原六项课程基线

| 测试 | 课程周期 | 退休数 | IPC | IPC变化 |
|---|---:|---:|---:|---:|
| median | 9338 | 6961 | 0.745448704 | −0.846006% |
| multiply | 19591 | 21722 | 1.108774437 | +0.020418% |
| vvadd | 4278 | 4524 | 1.057503506 | +1.986910% |
| towers | 6317 | 5278 | 0.835523191 | +1.488048% |
| qsort | 177506 | 139900 | 0.788142373 | +0.802226% |
| rsort | 173548 | 195719 | 1.127751400 | +1.303962% |

六项答案均为0。课程周期比较`init+core+drain`；软件旧total不含独立init=1。
生产RTL全新重建完整日志与候选逐字节一致。
两软件126项构建输入中3项DCache文件变化、123项保持，Release/断言四构建全部通过。
两个外部观察副本各374104条退休PC/周期逐条匹配RTL；本次未重跑完整PRF观察。
主树24 ticks＋58 helpers＋CPU clock及负向纪律检查通过，模板Register单写/统一sync保持。
不将原六项验收扩大为全部输入或完整状态等价。

## 证据与复验

- 硬件源码备份、模型、完整日志：`.cpu2026-synthesis/disjoint-load-production-adoption-20261102-r01/`。
- 两软件源码、四构建、外部退休观察：`.cpu2026-synthesis/software-disjoint-load-sync-20261102-r01/`。
- [第21轮实测](../../sync/ROUND21-MEMORY-20261102.md)及[生产采用汇总](../../sync/DISJOINT-LOAD-PRODUCTION-20261102.md)。
- [r32生产绑定最优归档](../../sync/evidence/best-results-retained-20261102-r32/retention.json)：本版本为已审计评分/IPC最优，无全部长期门槛达标者。

工作区根目录使用`sync/experiment_storage.py`设置F盘输出与进程临时目录。
CMake使用全新外部目录、`-DCMAKE_BUILD_TYPE=Release`、`-DCPU_RUNTIME_OUTPUT_DIRECTORY=<外部绝对目录>/bin`；
断言版追加`-DCMAKE_CXX_FLAGS=-D_DEBUG -DCMAKE_CXX_FLAGS_RELEASE='-O2 -UNDEBUG'`。

```bash
python3 -B sync/check_current_production_software.py \
  --main .cpu2026-synthesis/<新目录>/main/bin/code \
  --template .cpu2026-synthesis/<新目录>/template/bin/code \
  --out .cpu2026-synthesis/<新目录>/report
```

原镜像、答案、退休参考、旧黄金、Slave、仓库已有code与Git索引保留；未提交、未推送。
