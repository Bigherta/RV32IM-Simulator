# RV32IM IPC Benchmarks

> Corpus: `data/testcases_ipc/`; ISA: RV32IM. `clock` is the end-to-end cycle count, while IPC uses the simulator core interval frozen when HALT commits.

| case | x10 | clock | IPC |
| --- | ---: | ---: | ---: |
| median | 0 | 16177 | 0.436202 |
| multiply | 0 | 27682 | 0.784473 |
| qsort | 243 | 226674 | 0.545261 |
| rsort | 0 | 213442 | 0.878557 |
| towers | 0 | 7001 | 0.583857 |
| vvadd | 0 | 9419 | 0.479826 |
