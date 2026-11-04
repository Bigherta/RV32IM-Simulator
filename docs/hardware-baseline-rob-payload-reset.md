# ROB载荷复位分离生产基线

## 2026-11-04：正式采用ROB载荷复位分离（优先于以下历史）

用户要求“采纳ROB 复位分离”，仅采用已审计`rename-area-ppa-sequence-20261104-r01/rob-payload-reset`。面积34238.371507 μm²（-1.349543%），Fmax313.341493 MHz（+1.744186%），六项几何IPC1.010966177065（不变），评分2.792146575072e-07（+4.546957%）。IPC×Fmax估算+1.744186%，非墙钟实测。
仅替换`rv32_rob_lanes.sv`：载荷raw不复位，每行initialized复位并屏蔽未初始化读出；valid／ready／完整tag控制复位及写优先级保持。initialized不跟随valid清除，保留取消项replay和失效端口载荷。不带PRF或ROB SRAM候选，不改容量／宽度／BPU／访存协议。现有frontend格式及等宽截取修复原样保留并记录旧指针差异。
44项RTL／公共SRAM与已审计候选同源，复用该候选已完成整核综合／ABC／STA及native／converted局部与原六资格；本次不新综合、不声称另建生产路径RTL模型或全状态形式等价。两软件126项构建输入不变，无需改算法；全新Release／断言四构建原六答案／退休／init+core+drain及外部副本各374104条退休PC／周期匹配。周期8605/17550/3985/5864/155719/168501。旧黄金／镜像／Slave／既有code和三树HEAD／索引保持。
面积<36000及Fmax>300通过，长期IPC>1.0985未达。面积／频率合格候选中评分最优，不是无面积限制的最高IPC或最高评分者；历史排名／否决保持。采用证书`sync/evidence/rob-payload-reset-production-adoption-20261104-r01/adoption.json`；正式指针`sync/evidence/current-production.json`；软件说明`docs/hardware-baseline-rob-payload-reset.md`。
全部综合／ABC／STA继续暂停，未提交／推送；不自动继续新实验／组合／清理，不算完整新优化轮。

