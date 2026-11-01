# 2026-11-01 第17轮生产采用与双树软件同步

用户明确要求“替换生产代码，并同步软件”，正式采用
`round17-prf-microbank-20261101-r02`。生产指针为
[`sync/evidence/current-production.json`](../../sync/evidence/current-production.json)，
硬件[采用证书](../../sync/evidence/prf-microbank-production-adoption-20261101-r02/adoption.json)
与软件[完成证书](../../sync/evidence/software-prf-microbank-sync-20261101-r01.json)分别保存源码和测量身份。

## 实测结果

默认fetch/decode/issue/commit=2/4/4/4、双ALU/三CDB、单AGU、ROB32/PRF64/checkpoint64。
主树与模板树生产路径全新Release及`_DEBUG -O2 -UNDEBUG`四构建均通过。
原六项答案均为0，实际退休数与课程总周期全部匹配第17轮RTL。
`init=1`；课程周期为`init+core+drain`，原软件`timing.total=core+drain`。

| perf | 实际退休数 | core | drain | 三端课程周期 | 软件−RTL | 课程IPC |
|---|---:|---:|---:|---:|---:|---:|
| median | 6961 | 9244 | 14 | 9259 | 0 | 0.751809051 |
| multiply | 21722 | 19580 | 14 | 19595 | 0 | 1.108548099 |
| vvadd | 4524 | 4340 | 22 | 4363 | 0 | 1.036901215 |
| towers | 5278 | 6621 | 14 | 6636 | 0 | 0.795358650 |
| qsort | 139900 | 186096 | 14 | 186111 | 0 | 0.751701941 |
| rsort | 195719 | 185400 | 14 | 185415 | 0 | 1.055572634 |

几何IPC **0.9038909668726594**。同源第17轮全新整核综合/ABC/STA已独立审计：
面积 **34584.06330697913 μm²**，Fmax **315.27093596059115 MHz**，
评分`IPC^4 / Area^2 × Fmax`为 **1.759527609953837e-7**。
相对第16轮，面积−2.271465%、Fmax−0.738916%、六项IPC均不变、评分+3.928877%。
面积`<36000`和频率`>300`通过，长期几何IPC`>1.0985`尚未达到。
本次采用认证生产44个RTL输入与已审计候选逐字节一致，并重建核对完整退休日志；PPA引用该同源实测，采用阶段没有另做一轮综合。

[最优结果r20](../../sync/evidence/best-results-retained-20261101-r20/selection.json)继承r19全部候选并绑定新生产指针：第17轮为已审计评分最优，第16轮与其IPC并列最高；目前无满足全部长期门槛的候选。源码、模型、trace及摘要保留，综合大产物遵循最近3次规则。

## 本次软件实现

- **PRF**：P1–P63各一个`SRAM<1,32>`，P0常零且无bank。四分配＋三写回共7条上一拍写入记录覆盖写沿无有效SRAM读结果的情况；无写bank每拍读取，包括退出冻结阶段。`everWritten`负责复位后的零值；ready与数据独立。
- 每个目的分配都写PC+4，即使结果尚未ready；年轻分配优先，高序号WB覆盖分配及低序号WB。上一拍记录提供写后读，不增加当前CDB到PRF读口的组合旁路。
- **D$**：Load可探测有在途缺失的同组其他way；deferred miss仍按旧锁/信用准入。其他旧Load匹配返回word、范围及Store风险合格时捕获raw/ready。请求行返回不再等待脏victim写回完成，owner/MSHR及整行安装仍按完整事务收尾。
- **BPU**：仅PC chooser=3，且global计数器强（0/3）或local计数器弱（3/4）时选global；训练沿用预测时上下文。
- 主树保持旧快照/Input读取、逐字段写目标；模板嵌套SRAM每拍work一次，所有模块统一sync并清Wire缓存。双ALU、三CDB、单AGU与原容量保持；MUL RS未采用同拍复用。

## 验证范围

- 隔离候选与最终生产两树全部126个构建输入逐字节一致，本次软件源码变更共12个文件。
- 外部诊断副本三端逐拍比较全部64个PRF的ready/value，包括未ready值：六项共**411373个post-boot周期**，完整二进制序列相同。
- 三端**374104条**退休PC及post-boot周期逐条一致。观察器只在外部副本，验证范围为原六项及上述字段，不是全部CPU内部状态或全输入等价证明。
- `sync/audit_tick.py`通过24 ticks＋58 const writer helpers＋CPU clock；PRF SRAM只准同bank显式输出，并有目标读取/错bank负向自检。
- 原镜像、答案、退休参考、Slave、黄金表及已有`code`按保护SHA256核对；git索引和用户暂存内容保留。尚未提交或推送。

## 证据与复验

工作区根目录下`.cpu2026-synthesis/software-prf-microbank-sync-20261101-r01/`保存：

- `release-{main,template}/bin/code`、`assert-{main,template}/bin/code`：四个新生产构建。
- `release-report/report.json`、`assert-report/report.json`、`verification.json`：正式生产指针对照与构建认证。
- `observation/verification.json`、完整`.prf.bin`和退休日志：三端逐拍/逐条证据。
- `before/`、`candidate-{main,template}/`、`source-{main,template}/`：改前、观察候选及生产源码副本。
- `completion.json`、输入/保护清单、构建命令及审计日志：最终身份链。

从工作区根目录重新构建时每次选择新的F盘目录：

```bash
python3 -B - <<'PY'
import subprocess, sys
sys.path.insert(0, 'sync')
import experiment_storage as storage
out = storage.require_output(storage.WORK_ROOT / 'software-production-recheck-new')
out.mkdir(exist_ok=False)
env = storage.environment()
for label, source in (('main', 'RV32IM_Simulator'),
                      ('template', 'RV32IM_Simulator/RISC-V-Simulator-Template')):
    build = out / label
    subprocess.run(['cmake', '-S', source, '-B', str(build),
        '-DCMAKE_BUILD_TYPE=Release',
        '-DCPU_RUNTIME_OUTPUT_DIRECTORY=' + str(build / 'bin')], env=env, check=True)
    subprocess.run(['cmake', '--build', str(build), '-j4'], env=env, check=True)
subprocess.run([sys.executable, '-B', 'sync/check_current_production_software.py',
    '--main', str(out / 'main/bin/code'), '--template', str(out / 'template/bin/code'),
    '--out', str(out / 'report')], env=env, check=True)
PY
```

断言版另用全新目录，CMake追加`-DCMAKE_CXX_FLAGS=-D_DEBUG`及`-DCMAKE_CXX_FLAGS_RELEASE=-O2 -UNDEBUG`。
旧黄金表、`test.sh`/`test_IPC.sh`和[第10轮记录](hardware-baseline-word-production.md)保留对应历史口径，当前验收使用上述六例入口。
