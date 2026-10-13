# 前端：取指、译码与预测

## 取指与译码

`FetchUnit → ICache → FQ → Decoder/IQ`。默认每拍最多取指 1 条、译码 1 条，FQ / IQ 分别为 2 / 2 项。

## 分支预测

方向使用 Tournament；目标侧有 64 项 BTB 和 16 项 RAS，预测检查点有 32 项。恢复时从已提交栈重放存活的 ROB 指令。

[后端](backend.md) · [缓存](cache.md)
