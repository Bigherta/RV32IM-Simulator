# 前端：取指、译码与预测

当前共同配置见[规范](../../CPU-SYNC-SPEC.md)，源码入口为`src/FetchUnit/`、`ICache/`、`Decoder/`和`BPU/`。

## 数据流

```text
BPU → FetchUnit → ICache → FQ(2) → Decode/IQ(2) → Issue
                     ↕
               AXIFetch(8行) → AXIBus → 外部Slave
```

- 每拍最多接受一个取指请求；I$为1 KiB/64 B直映同步SRAM，8请求槽。
- 双提交不扩取指/译码/发射宽度。已提交RAS每拍顺序处理最多两个退休事件，各事件先pop后push；
  同一栈槽多次写时保留年轻事件，恢复仍读完整已提交基线并固定四拍重放。
- 命中经过同步读边界，队首且下游可接收时交付FQ，否则保存；空队列不组合直通。
- FQ/IQ满时可用本拍真实消费释放的额度补入；取指预留按真实事件更新。
- 同行实际回填可以供给本拍新接受的请求，不重复发主存请求或读取SRAM新写值。
- AXIFetch逐32-bit AXI读收齐整行后返回。恢复清逻辑请求，已接受旧R继续排空；
  已断言但未握手的AR必须保持VALID/地址，不能撤回。

## 分支预测

| 资源 | 配置 |
|---|---|
| 方向 | Tournament：local/global/selector各256×2 bit，初值1 |
| 历史 | GHR8；32个取指checkpoint，实际接受一次取指只分配一次 |
| 目标 | 寄存器BTB64，完整PC匹配；实验SRAM BTB不接入 |
| 类型 / 栈 | condSeen512，specRAS/archRAS各16项 |

取指预译码提供call/return早事件；训练读取拍初状态。条件分支训练使用真实`actualTaken`，不从next-PC反推。
BTB同槽竞争的tag/类型与target独立按fetch > jump CDB > taken BRU仲裁，无target写者不覆盖已有target。

JAL按链接目的寄存器决定push；JALR的pop/push hint独立，双hint（含同寄存器）按项目规则先pop再push。
空pop、满push保持；预测读取旧栈顶。

## 恢复与退出

GHR/checkpoint在广播拍回卷；已提交RAS使用同步SRAM，四拍按ROB共享窗口恢复完整spec栈内容和深度，
head/边界各重放一次。期间暂停取指、FQ/IQ和早事件，I$/AXIFetch继续处理在途回填。

默认`0x0ff00513`为普通ADDI。课程MMIO退出Store退休后停取，外部完成规则见[访存](memory.md)。

验证范围见[验收](../../sync/RTL-TO-SW-SYNC-20261018.md)；历史设计和参考文献见[归档原文](_archive/20261018/docs/frontend.md)。
