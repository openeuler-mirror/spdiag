# 用户侧 PerfPoint 集成测试 — 使用说明

## 1. 测试目的

SpDiag 提供了 `auto_perf.h` 便利头文件，用户只需：

1. 创建一个 `perf_points.def` 文件定义性能点
2. `#include "spdiag/auto_perf.h"` — 一行搞定

枚举自动生成、PerfManager 自动初始化，**无需修改或重新编译 SpDiag 库**。

```
┌─────────────────────────┐       ┌──────────────────────────────────┐
│   SPdiag 库（编译一次）    │       │  用户项目（可自由变更 .def）        │
│                         │       │                                  │
│  libspdiag.a            │◄──────│  perf_points.def    ← 用户唯一定义 │
│  auto_perf.h            │ 链接  │  #include auto_perf.h ← 一行搞定  │
│  PerfPoint 模板构造函数   │       │  PerfKey::USER_LOGIN ← 自动枚举   │
└─────────────────────────┘       └──────────────────────────────────┘
```

## 2. 用户接入方式

### 最简方式（推荐）

用户项目目录下创建 `perf_points.def`：

```
PERF_KEY_DEF(USER_LOGIN,  "User", "Login")
PERF_KEY_DEF(USER_QUERY,  "User", "Query")
PERF_KEY_DEF(DB_WRITE,    "DB",   "Write")
```

然后在代码中：

```cpp
#define SPDIAG_PROGRAM_NAME "my_app"    // 可选：自定义程序名（spdiag show 中显示）
#include "spdiag/auto_perf.h"           // ← 1 行搞定

void handleRequest() {
    PerfPoint pt(PerfKey::USER_LOGIN);   // PerfKey 是自动生成的枚举
    pt.Start();
    doStuff();
    pt.End(0);
}
```

> **level 参数默认值**：`PerfPoint` 构造函数的 `level` 参数具有默认值 `PerfLevel::SUB_SYSTEM`，因此关键路径级别的点位可以省略该参数。如需指定其他级别，传入第二个参数即可：`PerfPoint pt(PerfKey::USER_LOGIN, PerfLevel::KEY_MODULE);`。

**不需要**：
- 手写枚举定义
- 手写 PerfConfig 初始化
- 手动调用 `PerfManager::Init()`

`auto_perf.h` 自动完成：
- 从 `perf_points.def` 生成 `enum class PerfKey`
- 在 main() 前注册 slot 数量
- PerfPoint 构造时自动调用 `EnsureInitialized()`

> **推荐接入方式**：`auto_perf.h` 是定义用户 PerfPoint 的推荐头文件，详细设计思路和高级用法请参阅项目文档 [QuickStart.md](../../QuickStart.md)。

### 自定义 def 文件名

如果用户的 def 文件不叫 `perf_points.def`：

```cpp
#define SPDIAG_PERF_DEF_FILE "my_custom_points.def"
#include "spdiag/auto_perf.h"
```

## 3. 文件清单

```
tests/user_def_test/
├── perf_points.def       # 用户的性能点定义（X-Macro 格式）
├── main.cpp              # 测试主程序（演示 auto_perf.h 用法）
├── disabled_main.cpp     # SPDIAG_DISABLE 编译期禁用验证
├── CMakeLists.txt        # 测试项目构建文件
├── build_and_run.sh      # 一键构建运行脚本
└── README.md             # 本文档
```

### 各文件职责

#### `perf_points.def`

用户自定义的性能点定义文件，使用 X-Macro 格式：

```
PERF_KEY_DEF(枚举名, "模块名", "点名称")
```

本测试定义了 5 个性能点：

| 枚举名 | 模块 | 点名称 |
|--------|------|--------|
| `USER_LOGIN` | User | Login |
| `USER_QUERY` | User | Query |
| `USER_LOGOUT` | User | Logout |
| `ADMIN_REPORT` | Admin | Report |
| `ADMIN_CONFIG` | Admin | Config |

#### `main.cpp`

测试主程序。通过 `auto_perf.h` 自动生成 `PerfKey` 枚举并注册 slot 数量，然后运行 7 个测试场景。

关键代码结构：

```cpp
#include "spdiag/auto_perf.h"    // ← 枚举 + 注册全自动

void foo() {
    PerfPoint pt(PerfKey::USER_LOGIN, PerfLevel::KEY_MODULE);
    pt.Start();
    // ... do work ...
    pt.End(0);
}
```

#### `CMakeLists.txt`

使用 `find_package(SpDiag)` 从系统路径查找已安装的 SPdiag 库，和真实用户项目的接入方式完全一致：

```cmake
find_package(SpDiag REQUIRED)
add_executable(user_def_test main.cpp)
target_link_libraries(user_def_test PRIVATE SpDiag::spdiag_lib)
```

无需手动指定库路径或 include 路径，`SpDiag::spdiag_lib` target 自动包含所有必要信息。

## 4. 环境要求

| 依赖 | 最低版本 | 说明 |
|------|---------|------|
| 操作系统 | Linux | 依赖 POSIX 共享内存（`/dev/shm`） |
| GCC | 9+ | 或 Clang 10+，需要 C++17 支持 |
| CMake | 3.16+ | 构建系统 |
| pthread | 系统库 | 多线程支持 |
| rt | 系统库 | POSIX 实时扩展 |

> **注意**：本测试仅在 Linux 上运行。

## 5. 一键运行

默认运行正常打点功能测试：

```bash
bash tests/user_def_test/build_and_run.sh
```

验证 `SPDIAG_DISABLE` 编译期禁用模式：

```bash
bash tests/user_def_test/build_and_run.sh disabled
```

禁用模式会使用 Release 优化构建 `user_def_test_disabled`，运行二进制，然后通过 `nm -C` 检查二进制中不包含 `SpDiag::PerfPoint` 或 `SpDiag::PerfManager` 符号。成功时输出：

```text
[PASS] SPDIAG_DISABLE keeps the PerfPoint call syntax
[PASS] no PerfPoint or PerfManager symbols found
```

## 6. 分步手动运行

### Step 1：编译并安装 SPdiag 库

```bash
cd /path/to/spdiag
mkdir -p build && cd build
cmake .. -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF
cmake --build . --target spdiag_lib -j$(nproc)
sudo cmake --install .    # 安装到 /usr/local
```

### Step 2：编译用户测试项目

```bash
cd /path/to/spdiag/tests/user_def_test
mkdir -p build && cd build
cmake ..                  # find_package 自动从 /usr/local 找到 SpDiag
cmake --build . -j$(nproc)
```

### Step 3：运行测试

```bash
./user_def_test
```

退出码 `0` = 全部通过，`1` = 存在失败项。

## 7. 测试用例详解

### 用例 1：正常 End（好返回码）

```cpp
PerfPoint pt(PerfKey::USER_LOGIN, PerfLevel::KEY_MODULE);
pt.Start();
std::this_thread::sleep_for(std::chrono::milliseconds(1));
pt.End(0);
```

验证：`backGoodCount > 0`，`tickCount > 0`，`totalTimeNs > 0`

### 用例 2：坏返回码

```cpp
PerfPoint pt(PerfKey::USER_QUERY, PerfLevel::KEY_MODULE);
pt.Start();
pt.End(1);
```

验证：`backBadCount > 0`，`backGoodCount == 0`

### 用例 3：析构自动 Abandon

```cpp
{
    PerfPoint pt(PerfKey::USER_LOGOUT, PerfLevel::KEY_MODULE);
    pt.Start();
    // 故意不调用 End()
}  // ← 析构函数触发 Abandon()
```

验证：`backNotCount > 0`

### 用例 4：显式 Abandon

```cpp
PerfPoint pt(PerfKey::ADMIN_REPORT, PerfLevel::KEY_MODULE);
pt.Start();
pt.Abandon();
```

验证：`backNotCount > 0`，`backGoodCount == 0`，`backBadCount == 0`

### 用例 5：级别过滤

```cpp
PerfManager::Instance().IsLevelEnabled(PerfLevel::SUB_SYSTEM)   // true
PerfManager::Instance().IsLevelEnabled(PerfLevel::KEY_MODULE)   // true
PerfManager::Instance().IsLevelEnabled(PerfLevel::MODULE)       // false
PerfManager::Instance().IsLevelEnabled(PerfLevel::DEBUG)        // false
```

### 用例 6：复用 PerfPoint

```cpp
PerfPoint pt(PerfKey::ADMIN_CONFIG, PerfLevel::KEY_MODULE);
for (int i = 0; i < 5; ++i) {
    pt.Start();
    pt.End(0);
}
```

验证：`backGoodCount == 5`，`tickCount == 5`

### 用例 7：多 Key 同作用域

```cpp
{
    PerfPoint pt1(PerfKey::USER_LOGIN, PerfLevel::KEY_MODULE);
    pt1.Start(); pt1.End(0);
}
{
    PerfPoint pt2(PerfKey::USER_QUERY, PerfLevel::KEY_MODULE);
    pt2.Start(); pt2.End(0);
}
```

验证：两个 slot 的 `backGoodCount` 各为 1，互不干扰。

## 8. 预期输出

```
=== User-Defined Auto Perf Test ===
(Using auto_perf.h - no manual enum or Init needed)

1. Normal End (good return code)
  [PASS] backGoodCount incremented
  [PASS] backBadCount is 0
  [PASS] backNotCount is 0
  [PASS] tickCount incremented
  [PASS] totalTimeNs > 0
  [PASS] minTimeNs <= maxTimeNs

2. Bad return code
  [PASS] backBadCount incremented
  [PASS] backGoodCount is 0

3. Abandon on destructor (no End call)
  [PASS] backNotCount incremented by dtor
  [PASS] tickCount incremented

4. Explicit Abandon()
  [PASS] backNotCount incremented
  [PASS] backGoodCount is 0
  [PASS] backBadCount is 0

5. Level gating (IsLevelEnabled API)
  [PASS] SubSystem(1) enabled when gate=2
  [PASS] KeyModule(2) enabled when gate=2
  [PASS] Module(3) disabled when gate=2
  [PASS] Debug(4) disabled when gate=2

6. Reuse PerfPoint (multiple Start/End pairs)
  [PASS] good count matches iteration count
  [PASS] tick count matches iteration count

7. Multiple keys in single scope
  [PASS] slot 1 good count == 1
  [PASS] slot 2 good count == 1

========================================
Results: 21 passed, 0 failed
========================================
```

## 9. 自定义扩展

用户只需编辑 `perf_points.def` 即可增删性能点：

```
PERF_KEY_DEF(MY_NEW_POINT, "MyModule", "MyPoint")
```

然后在代码中使用 `PerfKey::MY_NEW_POINT`。**无需修改 SPdiag 库，只需重编用户项目**。

## 10. 常见问题

### Q: 编译报错 `undefined reference to 'SpDiag::PerfPoint::~PerfPoint()'`

**原因**：SPdiag 库未正确安装，`find_package` 找不到。

```bash
# 确认库已安装
ls /usr/local/lib/libspdiag.a
ls /usr/local/lib/cmake/SpDiag/SpDiagConfig.cmake

# 重新安装
cd /path/to/spdiag/build && sudo cmake --install .
```

### Q: 运行报错 `FATAL: PerfManager auto-init failed`

**原因**：POSIX 共享内存不可用。需要先运行 `spdiag start`。

```bash
spdiag start
./user_def_test
```

### Q: 编译报错 `perf_points.def: No such file or directory`

**原因**：def 文件不在 include 搜索路径中。确保 `perf_points.def` 在源码目录下，CMake 会自动将源码目录加入 include 路径。

### Q: 运行后 `spdiag show` 看不到数据

`PerfTimer::Stop()` 会在 Shutdown 时执行最后一次数据 dump。确保：
1. `spdiag start` 已运行
2. 程序正常退出（调用了 Shutdown）
3. 在程序退出后立即执行 `spdiag show`
