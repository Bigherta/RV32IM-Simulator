# 当前生产：RS直接行选择／释放信用优化

2026-11-07 用户“采用”，采用已审计单候选 `rs-onehot-credit-ppa-20261107-r01`。

| 指标 | 当前生产 | 相对上一生产 |
|---|---:|---:|
| 面积 | 31793.603519 μm² | +0.092857% |
| Fmax估计 | 327.784891 MHz | +3.201024% |
| 原六几何IPC | 1.122239838632 | 持平 |
| Fmax²×IPC/Area | 3.792484290924 | +6.405709% |

面积增29.495340 μm²，面积严格<36000、IPC严格>1.1通过。ABC BUFx2／1fF、STA5fF／2ns；频率为理想时钟无寄生估计，非后布局结果。

只替换 `verilog/rtl/backend/rv32_rs.sv`：DEPTH4双发WB旁路独立ready分支按旧行tag预算flush资格，直接行选择mask得到issue有效性及真正issueAccept后的释放行信用，减少索引编码后再选择tag／解码。mask精确保持原OR编码索引解码，包括零／多winner；原索引、全部无效周期载荷、排序／tie、WB旁路、真实接受信用、flush／freeze和所有状态writer保持。其他参数路径仍沿原索引解码。不组合未采用的提前Load地址候选，不恢复已否决PRF排名／重定时。

四取指／四译码／四分配／四退休、双ALU／三CDB、ROB16／PRF48／checkpoint64、FQ8／IQ8、I$1KiB／D$8KiB及现有LSU／BPU／AXI／Slave保持。

两软件126构建输入逐字节不变，无需算法同步；保主树只读旧快照/Input写目标、模板Register单写／统一sync。采用时全新Release／断言四构建原六答案、退休数和init+core+drain匹配：median8341、multiply13119、vvadd3101、towers5531、qsort162354、rsort162410。

精确复用44RTL／公共SRAM同源候选native/sv2v原六答案／输出／周期、五配置每模型15380拍公开输出差分与独立列表操作数信用oracle、三个有效负向、256选择和131072信用布尔检查，以及全新整核opt／ABC／STA独立审计。采用阶段不重新综合或重建RTL模型，不新增逐条退休轨迹／全状态形式等价声明。

正式指针：`sync/evidence/current-production.json`。
采用证书：`sync/evidence/rs-onehot-credit-production-adoption-20261107-r01/adoption.json`。
测量证书：`sync/evidence/rs-onehot-credit-ppa-20261107-r01/finalization.json`。
旧版本／指针／文档已备份，历史证书不回写；未提交／推送，全部综合继续暂停，不自动继续实验或清理。
