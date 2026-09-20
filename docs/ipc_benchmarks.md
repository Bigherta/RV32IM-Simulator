# RV32IM IPC Benchmarks

> Corpus: `data/testcases_ipc/`; ISA: RV32IM. `clock` is the end-to-end cycle count, while IPC uses the simulator core interval frozen when HALT commits.

| case | x10 | clock | IPC |
| --- | ---: | ---: | ---: |
| median | 0 | 16169 | 0.436418 |
| multiply | 0 | 27682 | 0.784473 |
| qsort | 243 | 226911 | 0.544692 |
| rsort | 0 | 213434 | 0.878590 |
| towers | 0 | 6993 | 0.584525 |
| vvadd | 0 | 9417 | 0.479928 |
