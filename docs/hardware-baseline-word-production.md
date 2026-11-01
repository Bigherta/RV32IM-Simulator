# 2026-10-31 生产 RTL 六项软件同步

同步目标是正式采用的 `round10-linefill-registered-word-20261031-r01`，
身份由 `sync/evidence/current-production.json` 和采用记录 SHA256 核对。

## 实测结果

主树与模板树全新 Release 和 `_DEBUG -O2 -UNDEBUG` 四构建的原六项答案、实际退休数和课程总周期全部匹配生产 RTL。
答案均为 0；软件 `init=1`，课程周期为 `init+core+drain`，历史 `total=core+drain`。

| perf | 实际退休数 | core | drain | 主树课程周期 | 模板课程周期 | RTL课程周期 | 差值 |
|---|---:|---:|---:|---:|---:|---:|---:|
| median | 6961 | 9451 | 14 | 9466 | 9466 | 9466 | 0 |
| multiply | 21722 | 19725 | 14 | 19740 | 19740 | 19740 | 0 |
| vvadd | 4524 | 4429 | 22 | 4452 | 4452 | 4452 | 0 |
| towers | 5278 | 6638 | 14 | 6653 | 6653 | 6653 | 0 |
| qsort | 139900 | 188662 | 14 | 188677 | 188677 | 188677 | 0 |
| rsort | 195719 | 192994 | 14 | 193009 | 193009 | 193009 | 0 |

## 本次补齐的硬件行为

- AXIData 从 owner 的关键字开始轮转读；R 接受沿寄存返回字，按原 word 位置收集整行。
- D$ 合格字提前返回；整行一次写入全部 word bank，owner/MSHR 保留至整行完成。
- StoreValue 旧槽可使用实际授权的**非控制 ALU**结果当拍派发；共享 Load/MUL/DIV CDB 不参与此旁路。
- SQ 数据捕获、LQ 数据通知、RS 释放与 Issue 同拍复用使用同一真实派发事件。
  多个合格 StoreValue 槽可同时离站；同槽重分配时新分配优先。
- 主树保持读旧快照/Input、逐字段写目标；模板保持 Register 单写和统一 sync。

首轮只补齐 Cache/AXI 后尚有 `0/0/-34/+221/-36/+1233` 的周期差，
补齐上述 StoreValue 行为后六项差值全部归零。首轮报告及失败证据保留在 r01 目录。

## 证据与复验

本轮独立产物：`../../.cpu2026-synthesis/software-word-production-sync-20261031-r02/`。
`sv-report/report.json` 保存首次六项匹配结果；`release-report/report.json` 与
`assert-report/report.json` 均为 `matched`，两份逐项记录完全一致。
`verification.json`、`builds.json`、`source-inputs.json` 保存源码/二进制/构建命令及保护文件身份，
`source-{main,template}/` 保留源码副本。`tick-audit-final.log` 通过主树24 ticks＋60 helpers＋CPU clock纪律检查，
两树 `git diff --check` 通过。首次审计调用误用软件仓库内路径的失败日志也保留，随后以工作区真实入口通过。
外部只读主树诊断副本还核对了原六项全部374104条退休事件的PC及post-boot采样周期，
与已认证native RTL trace逐条一致（`diagnostic-main-build/retirement-timing.json`）。
观察代码仅存在于外部副本；此检查不包含全部退休载荷或内部状态。
只验证默认配置的原六 perf，不扩大为全输入/全部内部状态等价；软件测量不是新 PPA。
原镜像、答案、指令数、Slave、黄金表和已有 `code` 保留。

从工作区根目录执行以下示例（每次选择新的输出目录）：

```bash
python3 - <<'PY'
from pathlib import Path
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
subprocess.run([sys.executable, 'sync/check_current_production_software.py',
    '--main', str(out / 'main/bin/code'), '--template', str(out / 'template/bin/code'),
    '--out', str(out / 'report')], env=env, check=True)
PY
```

断言版使用另一全新目录，并在 CMake 配置中追加
`-DCMAKE_CXX_FLAGS=-D_DEBUG`、`-DCMAKE_CXX_FLAGS_RELEASE=-O2 -UNDEBUG`。
旧 `check_fetch2_software.py`、`test.sh`、`test_IPC.sh` 仅用于其对应历史基线。
