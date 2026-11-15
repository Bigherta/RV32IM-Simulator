# 当前生产：双队列反向信用（357MHz）

用户2026-11-12明确采用精确三文件测量版本。

| 指标 | 当前生产 | 对前生产完整CDB包 |
|---|---:|---:|
| 面积 | 32256.416459 μm² | +0.195557% |
| 估计Fmax | 357.339149 MHz | +3.184297% |
| 原六几何IPC | 1.122239838632 | 不变 |

面积<36000、IPC>1.1通过；400MHz未达，全组非退化仍失败。
527稳定端点退化、missing0、匿名FF跨版本对应未建立。
DCache数据退43.134ps，真实输入其他SRAM控制/数据退61.422/2.661ps；
不能以全局改善抵消。用户明确采用不修改原拒绝证书。

仅FIFO/queue/frontend三文件：四宽队列按旧FQ/IQ空位反推包需求，晚ready比阈值；
FQ公开ready和真实push共用精确信用。其他宽度默认路径、同拍pop信用、两空队列旁路、
非法占用模运算、全部状态writer/级数保持。保留前生产完整CDB包及LSU chunks/Branch credit/PRF tag。
不叠加ACK/FQ-old-credit/decode-credit或未实现AGU size旁带，不修改mapped。

四取指/译码/分配/退休，双ALU/三CDB、ROB16/PRF48/checkpoint64，FQ8/IQ8、I$1KiB/D$8KiB保持。
两软件126输入逐字节不变，无需移植HDL拓扑；旧快照/Input和模板Register单写/统一sync保持。
新Release/断言四构建及两个外部软件副本原六答案/退休/init+core+drain和每副本374104退休PC/周期通过：
8341/13119/3101/5531/162354/162410。

复用精确converted资格：9437184全表示cut、真实queue+planner FW2/FW4独立oracle及gold全部公开含invalid差分、
四变异六有效运行时边界、原六完整raw，以及唯一同生产2.5ns/INVx1-5fF整核PPA/232宏/完整全组/后测。
采用不重建RTL模型、不跑native动态、不重复sv2v/综合/ABC/STA，不称全状态形式证明。
28160端点/11624违例、WNS−0.298442ns/TNS−1578.845510ns。频率为理想时钟无寄生估计，非后布局。

正式指针：`sync/evidence/current-production.json`；证书：`sync/evidence/frequency400-queue-backward-production-adoption-20261112-r01/adoption.json`。
原测量：`.cpu2026-synthesis/frequency400-queue-backward-ppa-20261111-r01/`；功能：screen r04。
真实旧指针及三RTL有完整备份；历史manifest按备份路径映射认证，不回写。
持续优化授权保留，不自动组合/清理/提交推送。
