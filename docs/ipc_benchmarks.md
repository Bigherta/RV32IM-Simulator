# RV32IM IPC Benchmarks

> 当前2026-10-10全dirty写回/boot隔离（SPEC §62）：两软件Release/`_DEBUG`各18＋6
> 实测全等，初始化1拍单列、clock=core+drain；全部脏行写完才结束。六IPC clock
> **693,042**（旧691,640＋1,402），退休/core/ipc-cycles和精确几何IPC
> **0.635112506623**保持。18 ISA新clock11,973,998。下面RAS策略更新为历史。

> 当前2026-10-10用户RAS规则：JALR双link（含相同寄存器）先pop再push。两软件Release/
> _DEBUG六IPC完整结果/退休/周期/统计一致，五例周期增加、vvadd保持；六项clock
> **691,640**（+826），精确几何IPC **0.635112506623**。18 ISA clock11,967,396保持。
> 本表按实测更新，旧值保留于历史；strict-cycles门禁现同时检查ISA及六IPC表，SPEC §59。

> 上一2026-10-08 LQ统一ROB提交释放后，两软件六IPC结果/退休/周期/统计一致，rsort −1拍、
> towers −11拍，其余周期保持；新六项总clock690,814，ISA总clock11,967,396，公共SPEC §44。

> 上一2026-10-08 DIV同拍消费重填/严格年轻flush定版后，两软件六IPC完整结果/退休/周期/统计
> 逐项一致，下面六项数值保持；18 ISA的新clock总和为11,967,406，见benchmarks.md及公共SPEC §40。

> Corpus: `data/testcases_ipc/`; ISA: RV32IM. `clock` is the end-to-end cycle count, while IPC uses the simulator core interval frozen when HALT commits.
> Geomean IPC = geometric mean of per-case IPC (`retired / ipc-cycles`) over the table below.
> `RESULT_FULL=1` prints full 32-bit x10. `VERBOSE=clock` prints the active cycle, retired and IPC metrics on stderr.

| case | x10 | clock | retired | ipc-cycles | IPC |
| --- | ---: | ---: | ---: | ---: | ---: |
| median | 00000000 | 13043 | 7056 | 12691 | 0.555985 |
| multiply | 00000000 | 27024 | 21715 | 26738 | 0.812140 |
| qsort | 00000000 | 310116 | 139893 | 309984 | 0.451291 |
| rsort | 00000000 | 330950 | 187520 | 330818 | 0.566837 |
| towers | 00000000 | 5921 | 4087 | 5657 | 0.722468 |
| vvadd | 00000000 | 5988 | 4519 | 5746 | 0.786460 |

> Geomean IPC = geometric mean of per-case IPC = **0.635113** over 6 cases; totals retired=364790 ipc-cycles=691634 clock=693042.

全dirty写回前的上一活动clock（SPEC §59，历史）：median12692、multiply26739、
qsort309985、rsort330819、towers5658、vvadd5747，共691640；各core周期与当前表相同。

用户pop-push改造前活动值（2026-10-08）：median12683/12682/0.556379，multiply26196/
26195/0.828975，qsort309928/309927/0.451374，rsort330794/330793/0.566880，towers5466/
5465/0.747850，vvadd5747/5746/0.786460（clock/ipc-cycles/IPC）；clock690814，
ipc-cycles690808，精确几何IPC0.641069650019。五例clock变化分别+9/+543/+57/+25/+192。

历史上一值：rsort clock330795/ipc-cycles330794/IPC0.566878，towers clock5477/ipc-cycles5476/
IPC0.746348；六项clock690826、几何IPC0.640854520836。
