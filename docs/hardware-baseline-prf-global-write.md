# 第20轮 PRF 共享写仲裁生产基线

用户要求“应用到生产”，已采用`round20-prf-global-write-20261101-r02`。
正式身份见[生产指针](../../sync/evidence/current-production.json)、
[硬件采用证书](../../sync/evidence/prf-global-write-production-adoption-20261101-r01/adoption.json)及
[软件复验证书](../../sync/evidence/software-prf-global-write-sync-20261101-r01.json)。

## 指标

| 指标 | 第20轮共享仲裁r02 | 相对第19轮生产 |
|---|---:|---:|
| 面积 | 34590.361867 μm² | −0.415138% |
| Fmax | 316.440049 MHz | +2.873918% |
| 六项几何IPC | 0.923231817701 | 持平 |
| IPC⁴ / Area² × Fmax | 1.921428756728e-7 | +3.733403% |

面积减少144.1962 μm²；面积严格`<36000`、Fmax`>300`通过，长期几何IPC`>1.0985`尚未达到。
通过44项RTL及公共SRAM源码身份认证引用第20轮已独立审计的整核综合/ABC/STA；采用阶段全新重建生产仿真。
按各自Fmax估算IPC×Fmax提高2.873918%，属于频率折算而非墙钟吞吐实测。

## PRF合同

RTL在行译码前统一处理四分配＋三WB的物理tag碰撞，后序同tag写者优先，
再按胜出的tag选择P1–P63各行。高序号WB覆盖低序号WB和分配；年轻分配覆盖同tag旧分配。
所有分配值（含未ready的PC+4）、P0恒零、ready/everWritten、上一拍写旁路和冻结持续读语义保持。
局部资格已验证native/converted全部64读口、公共同步1RW模型以及三项有效负向控制。

两套软件126项构建输入逐字节等同第19轮，既有PRF行为与该结构改写一致。
第19轮[两项Store位置缓冲合同](hardware-baseline-store-location.md#store位置缓冲合同)继续适用。

## 原六项课程基线

| 测试 | 课程周期 | 退休数 | IPC | 相对第19轮IPC |
|---|---:|---:|---:|---:|
| median | 9259 | 6961 | 0.751809050653 | 持平 |
| multiply | 19595 | 21722 | 1.108548099005 | 持平 |
| vvadd | 4363 | 4524 | 1.036901214760 | 持平 |
| towers | 6411 | 5278 | 0.823272500390 | 持平 |
| qsort | 178930 | 139900 | 0.781870005030 | 持平 |
| rsort | 175811 | 195719 | 1.113235235565 | 持平 |

六项答案均为0。课程周期比较`init+core+drain`，旧`clock`/`timing.total`不含独立init=1。
生产RTL全新构建六项完整日志与候选逐字节一致；两软件全新Release/断言四构建的答案、退休数、课程周期全匹配。
本次软件验证为上述原六项结果/计数；第17轮完整PRF及第19轮逐条退休观察属于各自历史证据。

## 证据与复验

- 硬件源码备份、全新模型、完整日志：工作区`.cpu2026-synthesis/prf-global-write-production-adoption-20261101-r01/`。
- 两软件126项源码、四构建与报告：工作区`.cpu2026-synthesis/software-prf-global-write-sync-20261101-r01/`。
- [r29最优归档](../../sync/evidence/best-results-retained-20261101-r29/retention.json)绑定新生产；共享仲裁为评分最优，最高IPC与第19轮持平，全部长期门槛达标最优为空。
- [采用汇总](../../sync/PRF-GLOBAL-WRITE-PRODUCTION-20261101.md)；[第20轮实测与面积取舍](../../sync/ROUND20-FREQUENCY-20261101.md)。

在工作区根目录用`sync/experiment_storage.py`的`environment()`设置构建进程环境，
输出放`.cpu2026-synthesis/<新目录>/`。CMake指定`-DCMAKE_BUILD_TYPE=Release`及
`-DCPU_RUNTIME_OUTPUT_DIRECTORY=<外部绝对目录>/bin`；断言版另建目录并追加
`-DCMAKE_CXX_FLAGS=-D_DEBUG -DCMAKE_CXX_FLAGS_RELEASE='-O2 -UNDEBUG'`。

```bash
python3 -B sync/check_current_production_software.py \
  --main .cpu2026-synthesis/<新目录>/main/bin/code \
  --template .cpu2026-synthesis/<新目录>/template/bin/code \
  --out .cpu2026-synthesis/<新目录>/report
```

原镜像、答案、退休参考、旧黄金表、仓库已有code和两Git索引均保留；使用外部新构建验收。
