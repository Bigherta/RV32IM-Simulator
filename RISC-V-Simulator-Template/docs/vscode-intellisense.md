# VS Code C/C++ IntelliSense 排障手册（compile_commands.json 方案）

> 适用：cmake && make 编译正常，但 VS Code 中 #include / std::xxx / 自定义类全部报红。
> 本文档按本仓库实测状态编写（2026-09-07）。核心结论：**IntelliSense 必须吃 CMake 的编译数据库，而不是手写 includePath。**

---

## 1. 根因：为什么"能编译却全红"

VS Code 里跑代码有两种互不相干的通道：

| 通道 | 谁在执行 | 用的配置 |
|---|---|---|
| 编译/运行 | GCC（riscv64-unknown-elf-g++） | CMake 算好的每个文件的 include、`-D` 宏、`-std` |
| 报红/补全 | C/C++ 扩展内置的 clang 引擎 | 它**自己探测**到的配置，找不到就 fallback 到空配置 |

CMake 默认不暴露每个文件的编译参数。C/C++ 扩展只有在能读到 **compile_commands.json**（编译数据库，每文件一条精确编译命令）时，才知道真实 include 路径、宏和 C++ 标准——三者齐全，红波浪线才会消失。**所以治本只有一条路：让扩展读到编译数据库。**

---

## 2. 本仓库实测状态（已代查，2026-09-07）

| 检查项 | 结果 | 含义 |
|---|---|---|
| 顶层 CMakeLists.txt | `仓库根/CMakeLists.txt`，C++20，include = `src/include`、`include` | 打开目录必须是仓库根 |
| `build/compile_commands.json` | **已存在**，22 个编译单元，`-std=gnu++20` | 数据源已就绪，缺的只是让扩展找到它 |
| 编译数据库路径形态 | `/mnt/f/...`（Unix 路径） | 由 **WSL 内** 生成 |
| 编译器 | `/usr/bin/riscv64-unknown-elf-g++` | **只存在于 WSL 内** |
| `.vscode/` | 原本不存在，本次已创建 | 见文末"已落地文件" |

结论一句话：**工具链与编译数据库都在 WSL 里，所以本仓库的 VS Code 必须用 WSL Remote 打开**（Windows 原生打开时，Windows 侧 clang 引擎既找不到 /mnt/f 路径、也访问不了 WSL 内的头文件，必然全红）。

---

## 3. 分场景操作（对应你列的 5 个场景）

### 场景 1：装扩展 + 在顶层目录 Configure

```bash
# 扩展（在 WSL 窗口内也要装，见场景 4）
ms-vscode.cpptools        # C/C++
ms-vscode.cmake-tools    # CMake Tools
```

1. `File > Open Folder` 打开 **仓库根**（含 CMakeLists.txt 的目录，不要打开 src）。
2. `Ctrl+Shift+P` → `CMake: Configure`。

`.vscode/settings.json` 已配置 `cmake.configureArgs = ["-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]`，
今后每次 Configure 都会自动刷新编译数据库（若手动跑命令行 cmake，请自行带
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`）。

### 场景 2：出现 "No active kit"

No active kit = 没选编译器，CMake 不知道用哪个工具链。

1. `Ctrl+Shift+P` → `CMake: Select a Kit`。
2. 选 `riscv64-unknown-elf-g++`（即 `/usr/bin/riscv64-unknown-elf-g++`，与命令行构建保持一致；
   若选了别的编译器会触发整树重新 configure，覆盖现有 build 缓存）。
3. 再次 `CMake: Configure` → `CMake: Build`。

### 场景 3：必须在顶层 CMakeLists.txt 目录打开

`compileCommands` 路径与 CMake Tools 都以 workspaceFolder（打开的那个文件夹）为基准解析。
在 `src/` 子目录打开时：找不到顶层 CMakeLists → 探测不到 build 与编译数据库 → fallback 空配置 → 全红。

正确姿势：`File > Open Folder` 选 **仓库根**。确认方法：打开后资源管理器顶部应直接能看到
`CMakeLists.txt` 这一层。

### 场景 4：WSL2 + Ubuntu（本仓库推荐路径）

```bash
# 编译器在 WSL 的 /usr/bin，仓库在 /mnt/f/...
# 因此必须让 VS Code 跑在 WSL 里，才能解析 /mnt/f/.../build/compile_commands.json
```

1. 左下角绿色 `><` → `Connect to WSL`（Remote Explorer 里右键发行版 Connect，均可）。
2. 在 WSL 窗口内 `File > Open Folder`，地址填
   `\\wsl$\<发行版名>\mnt\f\程序设计与数据结构\PPCA\RV32IM_Simulator\RISC-V-Simulator-Template`
   （或在 WSL 终端里敲 `code /mnt/f/程序设计与数据结构/PPCA/RV32IM_Simulator/RISC-V-Simulator-Template`）。
3. 扩展面板会显示 "Install in WSL"，把 C/C++ 与 CMake Tools 装进 WSL 侧。
4. 按场景 1 → 2 走：Select a Kit → Configure。

此时 `.vscode/c_cpp_properties.json` 里的
`"compileCommands": "${workspaceFolder}/build/compile_commands.json"` 会解析到
`/mnt/f/.../build/compile_commands.json`——Unix 路径与 WSL 内工具链完全匹配，报红即消。

### 场景 5：让 IntelliSense 读 compile_commands，而不是手改 includePath

**原则：compile_commands.json 是唯一权威**，它由 CMake 逐文件生成，包含真实 include 路径、
宏（如 `_DEBUG`）、`-std=gnu++20`。手写 `c_cpp_properties.json` 的 includePath/defines 属于
"复制粘贴编译器状态"，任何源文件/宏/双树差异都会让手写内容过期，只用于无 CMake 的裸项目兜底。

本仓库已就绪的文件（相对路径，WSL/Windows 打开均能定位到同一份编译数据库）：

```json
{
  "version": 4,
  "configurations": [
    {
      "name": "WSL-riscv64",
      "compileCommands": "${workspaceFolder}/build/compile_commands.json",
      "intelliSenseMode": "linux-gcc-x64"
    }
  ]
}
```

改完配置文件后执行一次 `C/C++: Reset IntelliSense Database`（Ctrl+Shift+P）或
`Developer: Reload Window`。

---

## 4. 验证是否生效

| 手段 | 预期结果 |
|---|---|
| 状态栏点语言模式（"C++"） | 配置名 `WSL-riscv64`，并显示 "Using compile commands from: ..." |
| `C/C++: Log Diagnostics` | 无 "cannot open source file" 类报错 |
| 打开任一经 include 的 .hpp | 不再有红色波浪线，跳转/补全正常 |

---

## 5. 常见坑速查

| 现象 | 原因 | 处理 |
|---|---|---|
| WSL 里 Configure 后仍红 | 扩展没装进 WSL 侧 | 扩展面板点 "Install in WSL" |
| 新增 .cpp 后 IntelliSense 不认 | 编译数据库没刷新 | 重新 `CMake: Configure` |
| 双树/多 build 目录混乱 | compileCommands 指错 build | 确认指向实际构建用的那棵树的 build |
| 在 Windows 原生窗口打开仍红 | /mnt/f 路径与 WSL 工具链在 Windows 侧不可见 | 改用场景 4（WSL Remote） |

---

## 6. 已落地文件（本次）

| 文件 | 作用 |
|---|---|
| `.vscode/c_cpp_properties.json` | 让 C/C++ 扩展读取 `build/compile_commands.json`（compileCommands 优先于手写 includePath） |
| `.vscode/settings.json` | CMake Tools 打开即 Configure + 每次配置自动导出编译数据库 |
| `docs/vscode-intellisense.md` | 本文档 |
