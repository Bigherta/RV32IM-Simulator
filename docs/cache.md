# Cache与AXI存储层次

```text
core → ICache → AXIFetch ─┐
     → DCache → AXIData  ─┴→ AXIBus → AXI4-Lite → 外部Slave
```

## 组织

| 项目 | ICache | DCache |
|---|---|---|
| 容量 / 行长 | 1 KiB / 64 B | 4 KiB / 32 B |
| 组 / 路 | 16 / 1 | 64 / 2，tree-PLRU |
| 请求槽 | 8 | 8，包含MSHR owner |
| 缺失 | AXIFetch整行运输 | 4 MSHR，同行Load合并，独立组hit-under-miss |
| 数据 | 同步SRAM，tag寄存器 | 每路8个64×32-bit字节写SRAM，metadata每路64×22 bit，valid独立 |
| 写策略 | 无CPU写口 | 写回、写分配 |

AXIFetch8行；AXIData读写共享8项描述符/payload，顺序环仍16项。外部Slave每通道16项、默认10拍，
不是旧IMEM/DMEM的固定20拍模型。完整参数见[规范](../../CPU-SYNC-SPEC.md)。

## DCache事件

双提交只拓宽ROB到LSQ的退休事件，不增加Cache请求、AGU、CDB或AXI带宽；SQ继续按序单路排空。

- 请求入队与同步查询分开；查询读取拍初状态，不把同拍SRAM写结果当作新读数据。
- lookup候选提前计算，最终授权按**Fill > Store > lookup**；各way/bank和metadata独立选本地地址。
- 命中数据先选相邻word，再四路字节旋转并截取；一次read grant只提取一次raw，响应与缓存保存共享。
- 最低编号invalid way优先，否则按PLRU选victim；缺失冻结victim身份，保存dirty数据，锁住该组。
- 同行Load在首拍回填前可合并；Store保持接受顺序，Load不能绕过有冲突的旧Store。
- 每拍回填2个word，共4拍。Load首个合格回填拍从完整旧回复早返回一次；末拍才安装metadata/valid、
  消费行回复并释放owner。脏victim必须满足实际写完成资格。

请求控制、raw结果和victim行缓冲分离。payload在捕获后才可见，不因分配清无效宽数据；SRAM复位不清内容，
以独立valid屏蔽旧值。无效端口地址和无效行数据不裸比。

## 恢复与退出

错路Load持久标取消；已接受运输继续完成但不得形成有效架构写回。核心四拍重放不阻断Cache/AXI。
MMIO退出绕过Cache，不污染行；正常课程结束不扫描全部dirty行，详见[访存](memory.md)。

## 验证

当前Cache/AXIData与RTL及独立字节参考通过60,000拍，并比较实际使能SRAM端口；
完整CPU逐拍、早返回、合并、脏逐出、满请求和恢复检查见[验收记录](../../sync/RTL-TO-SW-SYNC-20261018.md)。
这不是全输入或门级等价证明；PPA按[硬件评估快照](../../RISC-V-CPU-2026/docs/assessment-results.md)另报。

[历史原文](_archive/20261018/docs/cache.md)
