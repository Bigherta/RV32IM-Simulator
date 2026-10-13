# RV32IM IPC Benchmarks

> **当前双树8拍回填（2026-10-13）**：16个32bit D$ bank每拍写两个，DMEM整行协议和
> 20拍延迟不变。Release/`_DEBUG`各六例结果、退休、周期及共有统计全等；精确几何IPC
> **0.543625028725**。整行单拍回填上一基线为759,991拍，几何IPC0.576544287649。

> Corpus: `data/testcases_ipc/`; ISA: RV32IM. `clock` is the end-to-end cycle count, while IPC uses the simulator core interval frozen when HALT commits.
> Geomean IPC = geometric mean of per-case IPC (`retired / ipc-cycles`) over the table below.
> `RESULT_FULL=1` prints full 32-bit x10. `VERBOSE=clock` prints the active cycle, retired and IPC metrics on stderr.

| case | x10 | clock | retired | ipc-cycles | IPC |
| --- | ---: | ---: | ---: | ---: | ---: |
| median | 00000000 | 14545 | 7056 | 14545 | 0.485115 |
| multiply | 00000000 | 27037 | 21715 | 27037 | 0.803159 |
| qsort | 00000000 | 359319 | 139893 | 359319 | 0.389328 |
| rsort | 00000000 | 429967 | 187520 | 429967 | 0.436126 |
| towers | 00000000 | 7151 | 4087 | 7151 | 0.571528 |
| vvadd | 00000000 | 6620 | 4519 | 6620 | 0.682628 |

> Geomean IPC = geometric mean of per-case IPC = **0.543625** over 6 cases; totals retired=364790 ipc-cycles=844639 clock=844639.
