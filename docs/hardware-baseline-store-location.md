# 第19轮 Store 位置缓冲生产基线

用户要求“保存，作为生产”，选择`round19-store-location-20261101-r02`。
硬件生产重建与两软件同步已完成，身份链见[正式指针](../../sync/evidence/current-production.json)、
[硬件采用证书](../../sync/evidence/store-location-production-adoption-20261101-r01/adoption.json)及
[软件证书](../../sync/evidence/software-store-location-sync-20261101-r01.json)为准。

## 指标

| 指标 | 第19轮r02 | 相对第18轮 |
|---|---:|---:|
| 面积 | 34734.558067 μm² | +0.338710% |
| Fmax | 307.599880 MHz | −3.214178% |
| 六项几何IPC | 0.923231817701 | +2.139733% |
| IPC⁴ / Area² × Fmax | 1.852275829013e-7 | +4.629363% |

按各自Fmax估算IPC×Fmax下降1.143221%，用户已知该代价后明确采用。
面积严格`<36000`及Fmax`>300`通过；长期IPC`>1.0985`尚未达到。
采用按44项RTL及SRAM身份复用已独立审计的第19轮整核综合/ABC/STA。

## Store位置缓冲合同

两项记录各保存有效位、行地址和way；只由真实同步SRAM Load命中训练。
选槽读取旧状态：默认victim，优先无效槽（槽0优先），已有同行记录优先覆盖，
训练后victim指向另一槽。任意miss分配保守失效同set记录。

队首非MMIO Store未ready、无miss、未deferred，命中有效位置且无回填/set锁时，
读流水空闲或本拍交付正常Load命中即可直接使用原1RW端口写入。
写数据和字节掩码仍来自Store请求；位置表不保存数据。PLRU按每set的
fill→位置命中Store→普通read hit优先级更新。主树读旧快照/Input逐字段写目标；
模板状态进入`DCacheInner`，Register单写并统一sync。

## 原六项课程基线

| 测试 | 课程周期 | 退休数 | IPC | 相对第18轮IPC |
|---|---:|---:|---:|---:|
| median | 9259 | 6961 | 0.751809050653 | 持平 |
| multiply | 19595 | 21722 | 1.108548099005 | 持平 |
| vvadd | 4363 | 4524 | 1.036901214760 | 持平 |
| towers | 6411 | 5278 | 0.823272500390 | +3.509593% |
| qsort | 178930 | 139900 | 0.781870005030 | +4.013301% |
| rsort | 175811 | 195719 | 1.113235235565 | +5.462684% |

六项答案均为0。课程周期为`init+core+drain`，历史`clock`/`timing.total`
不含独立init=1。测试镜像、答案、退休参考及旧黄金表保留原身份。

## 证据与复验

- 硬件生产全新构建与完整退休日志：工作区
  `.cpu2026-synthesis/store-location-production-adoption-20261101-r01/`。
- 两软件源码备份、四构建和外部退休观察副本：工作区
  `.cpu2026-synthesis/software-store-location-sync-20261101-r01/`。
- 四个DCache构建输入变更，其余122项构建输入与第18轮证书相同。
- 全新Release及`_DEBUG -O2 -UNDEBUG`四个生产构建，六项答案、退休和课程周期全部匹配；
  两个外部断言观察副本各374104条退休PC/周期逐条匹配生产RTL。
- 主树24 ticks＋58 const writer helpers＋CPU clock及负向纪律检查通过。
- [r26最优归档](../../sync/evidence/best-results-retained-20261101-r26/retention.json)绑定本生产，
  本版为已审计评分最优与IPC最高；全部长期门槛达标最优仍为空。
- [生产采用汇总](../../sync/STORE-LOCATION-PRODUCTION-20261101.md)与
  [第19轮候选测量](../../sync/ROUND19-STORE-LOCATION-20261101.md)。

在工作区根目录使用`sync/experiment_storage.py`的`environment()`作为构建进程环境，
输出放`.cpu2026-synthesis/<新目录>/`；CMake指定`-DCMAKE_BUILD_TYPE=Release`
和`-DCPU_RUNTIME_OUTPUT_DIRECTORY=<外部绝对目录>/bin`。断言版另建目录并追加
`-DCMAKE_CXX_FLAGS=-D_DEBUG -DCMAKE_CXX_FLAGS_RELEASE='-O2 -UNDEBUG'`。
现有仓库`code`保留，使用新构建的绝对路径。

```bash
python3 -B sync/check_current_production_software.py \
  --main .cpu2026-synthesis/<新目录>/main/bin/code \
  --template .cpu2026-synthesis/<新目录>/template/bin/code \
  --out .cpu2026-synthesis/<新目录>/report
```

范围限定原六perf及所列观察字段；历史第17轮完整PRF观察仍属于当时版本。
