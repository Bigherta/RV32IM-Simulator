# 当前硬件基线：四取指ROB16／PRF48

默认fetch/decode/allocation/commit=4/4/4/4，双ALU／三CDB；ROB16、总PRF48（不是额外48），checkpoint64、恢复4槽／拍，FQ8／IQ8、LQ8／SQ8、I$1 KiB／D$8 KiB不变。

| 程序 | 课程周期 | IPC |
|---|---:|---:|
| median | 8341 | 0.834552212 |
| multiply | 13119 | 1.655766446 |
| vvadd | 3101 | 1.458884231 |
| towers | 5531 | 0.954257820 |
| qsort | 162354 | 0.861697279 |
| rsort | 162410 | 1.205092051 |

几何IPC **1.122239838632**，相对旧ROB32／PRF64 **−1.315692%**；用户选择采用，仅vvadd改善20.606256%。课程周期含init+core+drain；软件旧total仍不含init，不修改历史计数器或黄金表。

两软件只更改各自common头的ROB_CAP默认值，PRF_CAP和恢复宽度派生；checkpoint保持64。其余124项构建输入保持，包括用户并发的主树PRF显式转换修复。全新Release／断言四构建原六答案／退休／周期匹配，两外部副本各374104条退休PC／周期匹配RTL；主树旧快照/Input与模板Register单写／统一sync保持，无生产观察探针。

硬件新默认与已认证显式配置预处理token相同，复用原六native／sv2v资格及checkpoint初始断言恒真桥接；补充退休周期时，仅重链接已有模型外部观察器并新增12次原六时间戳观察，剔除P行后原日志保持，未重建RTL模型。只保证原六限定资格，不是全输入／全状态证明。本次无综合／ABC／STA，面积／Fmax／评分未测，不沿用旧PPA。

正式指针在工作区sync/evidence/current-production.json；采用证书为sync/evidence/rob16-prf48-production-adoption-20261107-r02/adoption.json。旧黄金、镜像、Slave和仓库已有code未覆盖；需新构建时使用CPU_RUNTIME_OUTPUT_DIRECTORY指定外部目录，验收使用工作区sync/check_current_production_software.py，不套旧golden周期。
