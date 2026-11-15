# 当前生产：三项HDL提频组合（8KiB D$）

2026-11-10 用户明确“采纳，更换生产”，在知悉其他组退化后选择采用。

| 指标 | 当前生产 | 对本次8KiB基线 |
|---|---:|---:|
| 面积 | 32111.330879 μm² | −0.133130% |
| 估计Fmax | 333.346897 MHz | +5.452696% |
| 原六几何IPC | 1.122239838632 | 不变 |

面积<36000、IPC>1.1通过；400MHz未达、全组非退化审核失败仍有效。其他SRAM控制组0→8违例，WNS+0.157881→−0.168165ns；其他SRAM数据/输出组亦退化。用户明确采用不等于技术门禁通过。

六个HDL文件：rv32_lsu.sv、rv32_rs.sv、rv32_core.sv、rv32_rename_plan.sv、rv32_rename_four.sv、rv32_prf_lanes.sv，分别为相邻word分块比较、Branch credit fit、原目的tag提前旁带；不是PRF并行排名，不改free-list、writer或流水级。其他当前源码及用户综合脚本修改保持。

四取指/译码/分配/退休，双ALU/三CDB，ROB16/PRF48、checkpoint64，I$1KiB、D$128组×2路×32B=8KiB。两软件仅容量头256→128组及注释同步，不照搬HDL组合门拓扑；旧快照/Input与Register单写/统一sync保持。原六周期8341/13119/3101/5531/162354/162410，全新Release/断言四构建答案/退休/课程周期及两个外部观察副本各374104退休PC/周期验证。

采用复用已认证converted组合局部655360/真实RS1033/2有效反例、整核374104退休完整日志和一次整核PPA；不重新综合/ABC/STA或运行native。测量为SRAM先读＋defer、opt、2.5ns、INVx1/5fF、232同形状宏，理想时钟无寄生估计非后布局。不是全状态形式证明。

正式指针：`sync/evidence/current-production.json`。
采用证书：`sync/evidence/frequency400-triple-production-adoption-20261111-r03/adoption.json`。
原测量：`.cpu2026-synthesis/frequency400-triple-combination-20261110-r01/`，原全组拒绝证书不回写。
旧指针入口已缺失，只新建本次真实采用记录，不恢复旧历史。原生产文件已备份，未提交/推送/清理或启动新综合。
