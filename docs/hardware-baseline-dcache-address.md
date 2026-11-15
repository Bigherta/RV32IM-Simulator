# 当前生产：DCache读地址频率优化

2026-11-07 用户授权采用已审计候选 `dcache-address-frequency-20261107-r01`。

| 指标 | 当前生产 | 相对最新同约束基线 |
|---|---:|---:|
| 面积 | 31764.108179 μm² | −0.295597% |
| Fmax估计 | 317.617866 MHz | +4.466501% |
| 原六几何IPC | 1.122239838632 | 持平 |
| Fmax² × IPC / Area | 3.564173709178 | +9.456048% |

面积严格<36000、IPC严格>1.1均通过。采用MHz/μm²口径；ABC为BUFx2/1fF，STA输出5fF、目标2ns，理想时钟无寄生，非后布局频率。

硬件仅将DCache读地址mux选择条件改为排队候选是否存在，提前地址选择；实际读使能、fill/Store覆盖、仲裁与所有状态更新保持。保留当前warning清理及四取指/四译码/四分配/四退休、ROB16/PRF48、双ALU/三CDB、I$1KiB/D$8KiB配置。

两软件126项构建输入逐字节保持，无需算法同步。采用时全新Release/断言四构建原六答案、退休数、init+core+drain周期匹配：median8341、multiply13119、vvadd3101、towers5531、qsort162354、rsort162410。主树旧快照/Input写目标及模板Register单写检查保持。

复用精确同源候选native/sv2v原六答案/完整输出/周期及局部每模型9501次差分、字节owner oracle、两个有效负向和2048组局部布尔检查。复用已完成综合/ABC及STA接续的独立审计；采用阶段没有新PPA或新逐条退休轨迹，不扩大为全状态形式等价。

正式指针：工作区 `sync/evidence/current-production.json`。
采用证书：`sync/evidence/dcache-address-production-adoption-20261107-r01/adoption.json`。
原测量证书：`sync/evidence/dcache-address-frequency-20261107-r01/finalization.json`。
旧生产指针早于warning清理；采用证书明确记录当前实际源码与旧指针的差异并绑定当前候选，历史证书不回写。
