# RV32IM IPC Benchmarks

> **当前双树2026-10-13基线**：D$ data/metadata同步SRAM，队首串行服务、DMEM双口
> 可并行；HALT提交立即结束、不排空，clock=ipc-cycles。Release与`_DEBUG`六IPC
> 两软件结果/退休数保持，周期/保留统计一致；精确几何IPC为0.576544287649。RTL未同步。

> Corpus: `data/testcases_ipc/`; ISA: RV32IM. `clock` is the end-to-end cycle count, while IPC uses the simulator core interval frozen when HALT commits.
> Geomean IPC = geometric mean of per-case IPC (`retired / ipc-cycles`) over the table below.
> `RESULT_FULL=1` prints full 32-bit x10. `VERBOSE=clock` prints the active cycle, retired and IPC metrics on stderr.

| case | x10 | clock | retired | ipc-cycles | IPC |
| --- | ---: | ---: | ---: | ---: | ---: |
| median | 00000000 | 14006 | 7056 | 14006 | 0.503784 |
| multiply | 00000000 | 26848 | 21715 | 26848 | 0.808813 |
| qsort | 00000000 | 330325 | 139893 | 330325 | 0.423501 |
| rsort | 00000000 | 375608 | 187520 | 375608 | 0.499244 |
| towers | 00000000 | 7116 | 4087 | 7116 | 0.574340 |
| vvadd | 00000000 | 6088 | 4519 | 6088 | 0.742280 |

> Geomean IPC = geometric mean of per-case IPC = **0.576545** over 6 cases; totals retired=364790 ipc-cycles=759991 clock=759991.
