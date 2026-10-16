# RV32IM 周期级乱序处理器模拟器

RV32IM 周期级乱序处理器模拟器。用 C++20 `comb()/tick()` 分离拍初求值与拍末更新。

## 构建与运行

需要 CMake 和支持 C++20 的编译器。在仓库根目录运行：

```bash
cmake -S . -B build/quickstart -DCMAKE_BUILD_TYPE=Release \
  -DCPU_RUNTIME_OUTPUT_DIRECTORY="$PWD/build/quickstart/bin"
cmake --build build/quickstart -j4
RESULT_FULL=1 VERBOSE=clock ./build/quickstart/bin/code < data/course-testcases/perf_median/program.data
```

从标准输入读取字节镜像；`VERBOSE=clock` 将周期统计写到标准错误。

## 文档

[前端](docs/frontend.md) · [后端](docs/backend.md) · [访存](docs/memory.md) · [缓存](docs/cache.md) · [用例基准](docs/benchmarks.md)
