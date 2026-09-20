# RV32IM 周期级乱序处理器模拟器

RV32IM 周期级乱序处理器模拟器。用 C++20 `comb()/tick()` 分离拍初求值与拍末更新。

## 构建与运行

需要 CMake 和支持 C++20 的编译器。在独立检出中运行；此版本会在仓库根目录生成 `code`：

```bash
cmake -S . -B build/quickstart -DCMAKE_BUILD_TYPE=Release
cmake --build build/quickstart -j4
VERBOSE=clock ./code < data/testcases/gcd.data
```

从标准输入读取字节镜像；`VERBOSE=clock` 将周期统计写到标准错误。

## 文档

[前端](docs/frontend.md) · [后端](docs/backend.md) · [访存](docs/memory.md) · [缓存](docs/cache.md) · [用例基准](docs/benchmarks.md)
