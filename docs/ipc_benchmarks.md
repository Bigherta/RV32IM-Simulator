# RV32IM IPC Benchmarks

> Corpus: `data/testcases_ipc/`; ISA: RV32IM. `clock` is the end-to-end cycle count, while IPC uses the simulator core interval frozen when HALT commits.
> Weighted IPC = `Σretired / Σipc-cycles` over the table below (not the arithmetic mean of per-case IPC).

| case | x10 | clock | IPC |
| --- | ---: | ---: | ---: |
| median | 0 | 16585 | 0.425470 |
| multiply | 0 | 27650 | 0.785381 |
| qsort | 0 | 245799 | 0.569138 |
| rsort | 0 | 213192 | 0.879587 |
| towers | 0 | 6812 | 0.600059 |
| vvadd | 0 | 9351 | 0.483316 |

> Weighted IPC = `Σretired / Σipc-cycles` = **0.702353** (364790/519383); total `clock` = 519389.
