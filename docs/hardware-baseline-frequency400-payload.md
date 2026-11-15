# 当前生产：完整CDB包条件展开（已包含②）

用户于2026-11-11明确选择采用；不含AXI bank先转发或六文件组合。

| 指标 | 当前生产 | 相对前生产三项组合 |
|---|---:|---:|
| 面积 | 32193.460019 μm² | +0.255764% |
| 估计Fmax | 346.311562 MHz | +3.889241% |
| 原六几何IPC | 1.122239838632 | 不变 |

面积<36000、IPC>1.1通过；400MHz未达，全组非退化审核仍失败。
28160 timed、11979违例，WNS−0.387570ns/TNS−3066.303041ns。
其他SRAM地址0→16违例（I$ lane0..15 addr[3]），另有PRF等组退化；
用户明确采用不等于原技术门禁通过，不以整核收益抵消其他组退化。

仅LSU/DCache/backend/core/top五个已测HDL文件：②响应预览/提前完整鉴权已内含，
完整valid/tag/destination/value包在晚到route选择前形成。原writer、实际grant、
无效载荷及流水级保持；此前生产的LSU chunks、Branch credit、PRF tag保留。
AXI和其他39RTL/公共SRAM不变，禁止postmap优化。

四取指/译码/分配/退休，双ALU/三CDB，ROB16/PRF48/checkpoint64，
FQ8/IQ8、I$1KiB/D$8KiB保持。两软件126输入不变，不照搬HDL门拓扑；
主树旧快照/Input写目标、模板Register单写/统一sync保持。
新Release/断言四构建与两外部退休观察副本均通过原六资格：
8341/13119/3101/5531/162354/162410，每副本374104退休PC/周期匹配。

精确复用候选converted局部98304/两有效反例/真实LSU及整核完整raw，
采用不新建RTL模型、不运行native、不重跑sv2v/综合/ABC/STA。
原PPA为SRAM先读+defer/opt/2.5ns/INVx1-5fF/232同形状宏，理想时钟无寄生，
非后布局或全状态形式等价证明。

正式指针：`sync/evidence/current-production.json`。
采用证书：`sync/evidence/frequency400-payload-production-adoption-20261111-r01/adoption.json`。
原测量：`.cpu2026-synthesis/frequency400-response-payload-cofactor-20261111-r01/`。
真实旧指针和五RTL已备份；原测量及全组拒绝不回写。
本次任务完成，不自动追加实验、组合、综合、清理、提交或推送。
