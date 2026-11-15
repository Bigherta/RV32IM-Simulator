# 当前生产：LQ新地址行插入预选择（364.899777MHz）

用户2026-11-12在获知I$新增违例及其他组退化后明确采用此精确版本。

| 指标 | 当前生产 | 对357.339149MHz前生产 |
|---|---:|---:|
| 面积 | 32143.129859μm² | −0.351206% |
| 估计Fmax | 364.899777MHz | +2.115813% |
| 原六几何IPC | 1.122239838632 | 不变 |

面积<36000、IPC>1.1通过；**未400MHz，全组非退化仍失败**。
DC地址/控制WNS改善72.453/159.988ps，但I$地址新增16违例及其他组退化，3222稳定端点退化/missing0。
匿名FF跨版对应未建立。用户明确采用不修改原拒绝结论，不以全局提频抵消其他组退化。

仅`verilog/rtl/memory/rv32_lsu.sv`：对旧LQ候选及各新地址行插入情形提前计算完整原年龄树，晚AGU完整鉴权/index/forward选择结果。
保留任意非传递年龄、tie、invalid载荷、原地址捕获/转发/Store屏障、仲裁/真实grant/信用、全部writer和流水级数。
保前生产双队列反向信用/完整CDB包/LSU chunks/Branch credit/PRF tag。
不带未采用的DCache地址预选择、地址rank、Load-index唤醒、年龄预比较、条件化请求边界，不改mapped。

四取指/译码/分配/退休、双ALU/三CDB、ROB16/PRF48/checkpoint64，FQ8/IQ8、I$1KiB/D$8KiB保持。
两软件126项构建输入逐字节不变，无需移植HDL门拓扑；主树旧快照/Input和模板Register单写/统一sync保持。
新Release/断言四构建原六答案/退休/init+core+drain及两个外部软件副本各374104退休PC/周期通过：
8341/13119/3101/5531/162354/162410。

采用复用精确converted局部524288项/真实LSU两响应配置/两变异四有效边界/原六完整raw和唯一PPA后测。
PPA沿原2.5ns/INVx1-5fF/五库/SRAM先读-defer/232同形状宏；理想时钟无寄生STA估计，非后布局。
原workflow只追加精确两条成功尾段的关闭收据沿用，不改原manifest或post。采用无RTL模型/native动态/sv2v/综合ABCSTA重跑。

正式指针：sync/evidence/current-production.json；证书：sync/evidence/frequency400-lq-insertion-production-adoption-20261112-r01/adoption.json。
原测量：.cpu2026-synthesis/frequency400-lq-address-insertion-ppa-20261112-r01；原功能screen r02。
旧生产单LSU/真实指针/文档备份保留，采用链不伪造；不自动组合、清理或提交推送。
