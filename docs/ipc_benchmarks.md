# RV32IM IPC Benchmarks

## 当前双提交＋四拍恢复＋课程AXI（2026-10-20）

两软件Release/断言四构建六perf同值、同退休、同周期，严格门禁读取`benchmarks.md`首表。
总cycles **706,569**、core **706,485**，几何IPC **0.546998574899**。
与单提交相比，六perf总周期增加733、几何IPC小幅下降；单取指/单发射保持，
退休提前改变了资源分配/调度和预测训练时刻，不将宽度增加等同于性能收益，也不推断Fmax。

## 历史：单提交＋四拍恢复＋课程AXI（2026-10-17）

两软件Release/断言四构建和各自与真实RTL的25例逐拍/独立ISA验证闭合后更新共同golden。
Cache/AXI容量及10拍外部服务保持；本次周期变化来自四拍恢复协议，不用软件模拟速度推断Fmax。
六perf总cycles **705,836**、core **705,752**；按`retired/core`计算的几何IPC **0.547643861067**。
严格逐例门禁读取`benchmarks.md`首表，旧单拍恢复表保留为历史。

> **2026-10-16 两路CDB**：ALU独占、MUL/DIV/LQ按ROB年龄共享后，四软件构建六perf
> 结果/退休/周期全部保持；总657,435拍、几何IPC0.589912318160，不推断综合频率收益。

## 历史：单拍恢复课程AXI基线（2026-10-16）

活动perf语料为`data/course-testcases/perf_*/`的6个原始课程镜像，非下方旧HALT镜像。
两软件D$4 KiB/2-way/4 MSHR/8请求槽、AXI服务10拍；MMIO退出SW退休计入指令数，
IPC使用其退休时冻结的core区间，B握手后的total另列。严格golden统一在`benchmarks.md`首表。
六例总cycles **657,435**，几何IPC **0.589912318160**；`test_IPC.sh`默认验证这6例，不覆写历史表。

## 历史：旧HALT镜像（下方数字不是当前golden）

> **当前主树活动基线（2026-10-16）**：4 KiB/32 B/2路、4 MSHR＋同行Load合并；DMEM
> 读写共享一个总计16项的混合队列，所有已接受项独立推进20拍。I$/IMEM保持现状，不实现I$ MSHR。
> 主树Release/`_DEBUG`六例完整x10/退休数保持，周期和共有统计一致；总clock **650,407**，
> 精确几何IPC **0.597481783828**。模板/RTL代码未同步，不再要求匹配此主树新周期表。

> **历史共同基线（2026-10-14，4 KiB/32 B/1路）**：Load首个合格回填拍从完整
> 256bit行响应一次，Cache第4拍才完成。四软件构建全部24镜像结果/退休/周期/共有统计
> 一致，实际三CPU24/24共14,287,667拍的70项pre/post严格全等、独立退休/实际内存通过。
> 六例总clock **932,876**，未舍入几何IPC **0.507281711689**；结果/退休数不变。
> 当前报告、byte-exact源码manifest与旧golden持久保存在共同父目录
> `build/dcache-4k-sync-20261014/`，下表由本次真实默认实测刷新，不冒用旧候选报告。

> **历史early restart＋8拍回填（2026-10-13）**：Load首个合格回填拍响应一次，Cache
> 队首/DMEM行仍末拍释放，dirty写完成资格及立即HALT保持。四软件构建＋实际三CPU
> 六例结果/退休/周期一致，70项pre/post逐word门禁通过，精确几何IPC
> **0.547674360731（＋0.7448759%）**，总周期839,512（旧844,639）。三端报告
> `/tmp/opencode/sync-sram-cpu-0qy9juzc/report.json`；全24镜像验证后刷新本活动表。
> 最终mux版完整24镜像默认strict再次通过（原运行`sync-sram-cpu-6efx_20o`），六项
> 周期与本表一致。2026-10-14服务重启后/tmp原工件已清空，完成通知／文档摘要保留。

> **early restart前8拍回填（2026-10-13，历史）**：16个32bit D$ bank每拍写两个，DMEM整行协议和
> 20拍延迟不变。Release/`_DEBUG`各六例结果、退休、周期及共有统计全等；精确几何IPC
> **0.543625028725**。整行单拍回填上一基线为759,991拍，几何IPC0.576544287649。

> Corpus: `data/testcases_ipc/`; ISA: RV32IM. `clock` is the end-to-end cycle count, while IPC uses the simulator core interval frozen when HALT commits.
> Geomean IPC = geometric mean of per-case IPC (`retired / ipc-cycles`) over the table below.
> `RESULT_FULL=1` prints full 32-bit x10. `VERBOSE=clock` prints the active cycle, retired and IPC metrics on stderr.

| case | x10 | clock | retired | ipc-cycles | IPC |
| --- | ---: | ---: | ---: | ---: | ---: |
| median | 00000000 | 14459 | 7056 | 14459 | 0.488001 |
| multiply | 00000000 | 26909 | 21715 | 26909 | 0.806979 |
| qsort | 00000000 | 267977 | 139893 | 267977 | 0.522034 |
| rsort | 00000000 | 327222 | 187520 | 327222 | 0.573067 |
| towers | 00000000 | 7161 | 4087 | 7161 | 0.570730 |
| vvadd | 00000000 | 6679 | 4519 | 6679 | 0.676598 |

> Geomean IPC = geometric mean of per-case IPC = **0.597482** over 6 cases; totals retired=364790 ipc-cycles=650407 clock=650407.

流水DMEM前共同基线（2026-10-14，历史，模板/RTL仍未改）：median=15583、multiply=27338、
qsort=356680、rsort=518391、towers=7161、vvadd=7723；总932,876拍，几何IPC0.507281711689。

切换4 KiB前8 KiB early-restart周期（2026-10-13，历史）：median=14305、multiply=26970、
qsort=359194、rsort=425366、towers=7151、vvadd=6526；总839,512，几何IPC0.547674360731。

历史8拍回填周期：median=14545、multiply=27037、qsort=359319、rsort=429967、
towers=7151、vvadd=6620。退休数与完整x10均保持；IPC提升并不等于整机Fmax提升。
