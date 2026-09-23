# RV32IM IPC Benchmarks

> Corpus: `data/testcases_ipc/`; ISA: RV32IM. `clock` is the end-to-end cycle count, while IPC uses the simulator core interval frozen when HALT commits.
> Geomean IPC = geometric mean of per-case IPC (`retired / ipc-cycles`) over the table below.
> `RESULT_FULL=1` prints full 32-bit x10. `VERBOSE=cftrace,profile` adds host-only control-flow fetch/execute/commit records and a commit-filtered dynamic mix on stderr.

| case | x10 | clock | retired | ipc-cycles | IPC |
| --- | ---: | ---: | ---: | ---: | ---: |
| median | 00000000 | 16574 | 7056 | 16573 | 0.425753 |
| multiply | 00000000 | 27644 | 21715 | 27643 | 0.785551 |
| qsort | 00000000 | 246710 | 139893 | 246709 | 0.567036 |
| rsort | 00000000 | 213188 | 187520 | 213187 | 0.879603 |
| towers | 00000000 | 6798 | 4087 | 6797 | 0.601295 |
| vvadd | 00000000 | 9351 | 4519 | 9350 | 0.483316 |

> Geomean IPC = geometric mean of per-case IPC = **0.603844** over 6 cases; totals retired=364790 ipc-cycles=520259 clock=520265.
