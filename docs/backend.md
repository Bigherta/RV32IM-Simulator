# 后端：重命名、执行、写回与恢复

## 重命名与执行

每拍最多按序分配 1 条、提交 2 条。ROB 有 16 项、PRF 有 48 项；ROB 身份由 epoch 和槽位组成。

有 1 个 ALU。Load 和 Store 地址共用 AGU，条件分支由 BRU 解析，乘除法送专用执行单元。

## 写回与恢复

写回有 2 路：ALU 独占一路，Load/MUL/DIV 按年龄共享一路。 指令仍按 ROB 队首顺序退休。

每个重放拍处理 4 个 ROB 槽，固定 4 个重放拍。

## 乘除法

MUL 用 radix-4 Booth 编码和 CSA；DIV 用 radix-4 SRT 迭代。

[完整算法推导和调试轨迹](_archive/20261018/docs/backend.md)保留了算术细节。

[访存](memory.md)
