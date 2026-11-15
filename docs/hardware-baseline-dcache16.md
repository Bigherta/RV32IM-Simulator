# 当前生产：16 KiB DCache

2026-11-08 用户明确“采纳并应用到生产”，选择已审计`dcache16-ppa-20261108-r01`，虽然目标值较上一8 KiB生产下降2.271463%，不称评分或目标最优。

| 指标 | 当前16 KiB生产 | 对上一8 KiB生产 |
|---|---:|---:|
| 面积 | 35107.695801 μm² | +10.423771% |
| Fmax估计 | 331.498867 MHz | +1.133053% |
| 原六几何IPC | 1.184086472112 | +5.511000% |
| Fmax²×IPC/Area | 3.706339426313 | −2.271463% |

面积严格<36000、IPC严格>1.1通过。ABC BUFx2/1fF、STA5fF/2ns；理想时钟／无寄生估计，非后布局频率。2ns是测量参考目标，不表示500MHz收敛。

生产实际backend实例显式`.SETS(256)`，两路×32字节行＝16384字节；公共package的DCache缺省参数仍128，不误称所有独立实例默认均16 KiB。保持早字／typed Load下一行预取、8请求／4MSHR、全部LSU／owner取消／仲裁／AXI／Slave、I$1 KiB、4/4/4/4、双ALU／三CDB、ROB16／PRF48／checkpoint64、已采用双发RS。只替换`verilog/rtl/memory/rv32_dmem_backend.sv`；其他43RTL／SRAM实现与全部writer文本保持，但参数化行数／tag位宽变化。没有组合未采用Branch切链或已否决tag方案。

两软件只修改`src/include/common.hpp`和`RISC-V-Simulator-Template/include/common.h`的DCache组数及对应注释，其他124构建输入保持。原算法／状态writer不改，主树旧快照/Input写目标、模板Register单写／统一sync保持。新Release／断言四构建原六答案／退休／课程周期和两外部副本各374104条退休PC／周期全部匹配。仅原六有限资格，不是全输入／全状态形式等价。

课程周期median8341、multiply13119、vvadd3101、towers5531、qsort159112、rsort120112。相对8 KiB前四项持平，qsort−3242拍／IPC+2.037558%，rsort−42298拍／IPC+35.215466%。不修改历史黄金表／镜像或Slave，旧脚本黄金周期不能套新生产；当前专用检查器从正式采用指针读取参考。

复用认证候选的局部native/sv2v字节owner／全组两路／dirty victim／预取双环境、三个有效负向、原六答案及九字段架构退休、严格前端转换资格和新整核opt／ABC／STA。232宏中仅64数据宏128×8→256×8、2元数据宏128×21→256×20，其余166宏逐实例保持。采用时无新综合或RTL模型构建；仅重链接既有外部观察器补12次时间戳运行并验证剔除P后原输出完全相同。

正式指针：`sync/evidence/current-production.json`。
采用证书：`sync/evidence/dcache16-production-adoption-20261108-r02/adoption.json`。
PPA证书：`sync/evidence/dcache16-ppa-20261108-r01/finalization.json`。
发布前源码／指针／文档已备份，历史资格／失败保持；未提交／推送，全部综合暂停，不自动继续其他实验或清理。
