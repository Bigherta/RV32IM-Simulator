# RV32IM IPC Benchmarks

> Corpus: `data/testcases_ipc/`; ISA: RV32IM. `clock` is the end-to-end cycle count, while IPC uses the simulator core interval frozen when HALT commits.
> Geomean IPC = geometric mean of per-case IPC (`retired / ipc-cycles`) over the table below.
> `RESULT_FULL=1` prints full 32-bit x10. `VERBOSE=cftrace,profile` adds host-only control-flow fetch/execute/commit records and a commit-filtered dynamic mix on stderr.
> Updated 2026-10-05 (capacity sync 3A): I$ 1 KiB / 64 B / synchronous SRAM; D$ **8 KiB / 64 B / direct-mapped**. FQ/IQ4 and LQ/SQ8 are fully usable with epoch pointers. Main O2+asserts and template Release+`_DEBUG` agree on full x10, retired, cycles and statistics for all six self-checks.

| case | x10 | clock | retired | ipc-cycles | IPC |
| --- | ---: | ---: | ---: | ---: | ---: |
| median | 00000000 | 12685 | 7056 | 12684 | 0.556291 |
| multiply | 00000000 | 26200 | 21715 | 26199 | 0.828848 |
| qsort | 00000000 | 309928 | 139893 | 309927 | 0.451374 |
| rsort | 00000000 | 330811 | 187520 | 330810 | 0.566851 |
| towers | 00000000 | 5500 | 4087 | 5499 | 0.743226 |
| vvadd | 00000000 | 5748 | 4519 | 5747 | 0.786323 |

> Geomean IPC computed from unrounded retired/ipc-cycles = **0.640350140801** over 6 cases; retired counts unchanged. The smaller D$ increases the memory-heavy qsort/rsort cycle counts.

## Previous active configuration: 16 KiB / 64 B / direct-mapped DCache

FQ/IQ each had three usable entries; LQ/SQ each seven. Archived 2026-10-01 values:

| historical case | clock | retired | ipc-cycles | IPC |
|---|---:|---:|---:|---:|
| median | 12720 | 7056 | 12719 | 0.554761 |
| multiply | 26200 | 21715 | 26199 | 0.828848 |
| qsort | 236471 | 139893 | 236470 | 0.591589 |
| rsort | 211257 | 187520 | 211256 | 0.887643 |
| towers | 5546 | 4087 | 5545 | 0.737060 |
| vvadd | 5748 | 4519 | 5747 | 0.786323 |

Historical exact geomean: 0.720538578156.

## Previous measured configuration: 64 KiB / 16 B / four-way DCache

This is the freshly rebuilt pre-change baseline (the earlier 0.603844 report used an older instruction-cache configuration).

| historical case | clock | retired | ipc-cycles | IPC |
| --- | ---: | ---: | ---: | ---: |
| `median` | 16362 | 7056 | 16361 | 0.431269 |
| `multiply` | 27500 | 21715 | 27499 | 0.789665 |
| `qsort` | 245894 | 139893 | 245893 | 0.568918 |
| `rsort` | 212643 | 187520 | 212642 | 0.881858 |
| `towers` | 5670 | 4087 | 5669 | 0.720938 |
| `vvadd` | 9306 | 4519 | 9305 | 0.485653 |

Exact geomean: **0.625380057590 -> 0.720538578156 (+15.2161%)**. Image/self-check semantics and retired counts are unchanged.
