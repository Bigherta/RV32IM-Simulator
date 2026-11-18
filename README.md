# RV32IM 周期级乱序处理器模拟器

C++20 实现的 RV32IM 模拟器，用 `comb()/tick()` 分离拍初求值与拍末更新。另有 [Wire/Register 模板版](https://github.com/CPU-2026/CPU-Simulator/tree/Tournament) 和 [配套 RTL](https://github.com/CPU-2026/RISC-V-CPU-2026)。

## 构建与运行

需要 CMake 和支持 C++20 的编译器。在仓库根目录运行，产物不会覆盖已有的 `code`：

```bash
cmake -S . -B build/quickstart -DCMAKE_BUILD_TYPE=Release \
  -DCPU_RUNTIME_OUTPUT_DIRECTORY="$PWD/build/quickstart/bin"
cmake --build build/quickstart -j4
RESULT_FULL=1 VERBOSE=clock ./build/quickstart/bin/code \
  < data/course-testcases/perf_median/program.data
```

从标准输入读取 Verilog-hex 字节镜像。`RESULT_FULL=1` 输出完整 32 位结果（默认低 8 位），`VERBOSE=clock` 将周期和退休统计输出到标准错误。

## 文档

- [前端](docs/frontend.md) · [后端](docs/backend.md) · [访存](docs/memory.md) · [缓存与 AXI](docs/cache.md)
- [课程用例与黄金表](docs/benchmarks.md) · [性能数据与复验](docs/ipc_benchmarks.md)
