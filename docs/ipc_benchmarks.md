# RV32IM IPC Benchmarks

> Corpus: `data/testcases_ipc/`; ISA: RV32IM. `clock` is the end-to-end cycle count, while IPC uses the simulator core interval frozen when HALT commits.

| case | x10 | clock | IPC |
| --- | ---: | ---: | ---: |
| median | 0 | 15233 | 0.463235 |
| multiply | 0 | 27528 | 0.788862 |
| qsort | 243 | 225372 | 0.548411 |
| rsort | 0 | 202073 | 0.927986 |
| towers | 0 | 6855 | 0.596294 |
| vvadd | 0 | 8425 | 0.536443 |
