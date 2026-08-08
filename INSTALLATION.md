# SpDiag 安装与项目集成指南

本文介绍 SpDiag 的依赖、源码安装、RPM 构建与安装，以及第三方 CMake 项目接入 SDK 的完整流程。RPM 章节说明当前打包脚本和 spec 实际生成的产物与使用方式。

## 1. 平台与依赖

当前发布和 RPM 打包面向 **Linux aarch64**。PerfPoint 在其他 Linux 架构存在兼容计时路径，但 eBPF 对象当前固定按 ARM64 构建，不应把 x86\_64 或 Windows 视为完整支持平台。

openEuler 24.03 LTS 可安装以下依赖：

```bash
sudo dnf install -y \
  gcc gcc-c++ make cmake git pkgconfig \
  clang libbpf-devel bpftool elfutils-libelf-devel zlib-devel \
  systemtap-sdt-devel gtest-devel kernel-headers rpm-build
```

依赖用途：

| 软件包                                                           | 用途                              |
| ------------------------------------------------------------- | ------------------------------- |
| `gcc gcc-c++ make cmake`                                      | C++17 编译工具链                     |
| `git`                                                         | RPM 从当前提交创建可复现源码包               |
| `clang libbpf-devel bpftool elfutils-libelf-devel zlib-devel` | eBPF 程序编译、加载及符号解析               |
| `systemtap-sdt-devel`                                         | MemPoint 的 `sys/sdt.h` USDT 头文件 |
| `gtest-devel`                                                 | 单元测试及 RPM `%check`              |
| `kernel-headers`                                              | RPM 构建和内核接口头文件                  |
| `rpm-build`                                                   | 提供 `rpmbuild`                   |

运行 OB Memstat 或 MemPoint 时，内核需提供 `/sys/kernel/btf/vmlinux`，并需要 root 或相应 BPF/perf capabilities。OB Cachestat 使用 PMU/perf\_event，需要 root、`CAP_PERFMON`，或合适的 `kernel.perf_event_paranoid` 配置。

## 2. 从源码构建和安装

### 2.1 使用 build.sh

```bash
# 默认：clean、Debug、静态 SDK、examples，并安装到 /usr/local
bash build.sh

# 常用组合
bash build.sh -r                         # Release
bash build.sh -r -l on                  # Release 共享库
bash build.sh -r -t on                  # 构建测试
bash build.sh -r -i off                 # 只构建，不安装
bash build.sh -r -p on -s on -m on      # 开启分位数、PerfLog、MemPoint
```

| 选项           | 含义                      | 默认值   |
| ------------ | ----------------------- | ----- |
| `-r`         | Release；不指定时为 Debug     | Debug |
| `-c on\|off` | 构建前清理 `build/`          | on    |
| `-t on\|off` | 构建测试                    | off   |
| `-e on\|off` | 构建 examples             | on    |
| `-i on\|off` | 安装到系统                   | on    |
| `-l on\|off` | 共享库 `.so`；off 为静态库 `.a` | off   |
| `-p on\|off` | P99/P999/P9999          | off   |
| `-s on\|off` | PerfLog                 | off   |
| `-m on\|off` | MemPoint                | off   |
| `-o on\|off` | OB Memstat              | on    |
| `-k on\|off` | OB Cachestat            | on    |
| `-g <path>`  | 本地 googletest 源码目录      | —     |
| `-d LEVEL`   | `DEBUG/INFO/WARN/ERROR` | ERROR |
| `-w on\|off` | 控制台日志                   | on    |
| `-W on\|off` | 文件日志                    | off   |

`build.sh` 的默认值与直接运行 CMake 不完全相同：脚本默认关闭测试和 MemPoint；根 `CMakeLists.txt` 默认开启二者。自动化构建应显式传递所需开关。

构建前若存在 `/dev/shm/spdiag_shm*`，脚本会要求先停止正在运行的 SpDiag，避免新旧共享内存布局不一致。

### 2.2 直接使用 CMake

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF \
  -DBUILD_EXAMPLES=ON \
  -DENABLE_MEMPOINT=OFF \
  -DSPDIAG_BUILD_SHARED=ON
cmake --build build --parallel
sudo cmake --install build
```

默认安装前缀为 `/usr/local`：

- CLI：`/usr/local/bin/spdiag`
- 头文件：`/usr/local/include/spdiag/`
- SDK：`/usr/local/lib/libspdiag.a` 或 `libspdiag.so`
- CMake 包：`/usr/local/lib/cmake/SpDiag/`
- 配置文件：通常为 `/usr/local/etc/spdiag/spdiag.conf`

CLI 当前依次查找 `config/spdiag.conf`、当前目录 `spdiag.conf` 和 `/etc/spdiag/spdiag.conf`。若使用默认 `/usr/local` 前缀安装，又希望 CLI 使用系统配置，请执行：

```bash
sudo install -D -m 0644 /usr/local/etc/spdiag/spdiag.conf \
  /etc/spdiag/spdiag.conf
```

未安装时可直接运行 `./build/src/cli/spdiag`。

## 3. RPM 构建、安装与维护

这一部分分成两条路径：

- 如果你是 SpDiag 的发布者，需要自己生成 RPM，请从第 3.1 节开始。
- 如果你拿到的是已经构建好的 RPM，只需根据第 3.3 节选择并安装需要的包。

### 3.1 一次打包会生成什么

执行一次 RPM 打包会同时构建共享 SDK 和静态 SDK，并生成二进制 RPM 与源码 RPM。

| 包名                          | 包含内容                                                            | 什么时候安装                       |
| --------------------------- | --------------------------------------------------------------- | ---------------------------- |
| `spdiag`                    | `spdiag` CLI、版本化 `libspdiag.so.*`、`/etc/spdiag/spdiag.conf`     | 只运行 CLI，或运行动态链接 SpDiag 的业务程序 |
| `spdiag-devel`              | SDK 头文件、`libspdiag.so` 链接名、`SpDiagConfig.cmake`                 | 编译动态链接 SpDiag 的业务程序          |
| `spdiag-static`             | `libspdiag.a`、`libspdiag_logger.a`、`SpDiag::spdiag_static` 导出文件 | 编译静态链接 SpDiag 的业务程序          |
| `spdiag-<版本>-<发行号>.src.rpm` | 完整源码 RPM                                                        | 复现构建、审计或二次发布                 |

依赖关系是：

```text
spdiag-static -> spdiag-devel -> spdiag
```

因此，`dnf install spdiag-static` 会连带安装开发包和运行时包。发行版还可能自动生成 `debuginfo` 和 `debugsource` 包，它们不是 SDK 接入的必需包。

按使用场景选择：

| 需求          | 安装命令                             |
| ----------- | -------------------------------- |
| 只使用 CLI     | `sudo dnf install spdiag`        |
| 编译并运行动态链接应用 | `sudo dnf install spdiag-devel`  |
| 编译静态链接应用    | `sudo dnf install spdiag-static` |

### 3.2 从源码构建 RPM

#### 构建条件

RPM 只面向 aarch64。构建机需要 spec 中声明的依赖：

```bash
sudo dnf install -y \
  cmake make gcc-c++ clang pkgconfig \
  libbpf-devel bpftool elfutils-libelf-devel zlib-devel \
  systemtap-sdt-devel gtest-devel kernel-headers rpm-build git
```

打包脚本从当前 Git `HEAD` 创建源码归档。执行前必须满足：

1. 当前目录是 SpDiag Git 仓库根目录；
2. `spdiag.spec` 中的版本与 `CMakeLists.txt` 一致；
3. 已跟踪文件没有未提交或暂存修改；
4. `rpmbuild` 在 `PATH` 中。

未跟踪文件不会进入源码包。要把文档或代码改动打进 RPM，必须先提交。

#### 默认功能和可选开关

```bash
# 查看帮助
bash build.sh package --help

# 使用默认功能组合
bash build.sh package
```

默认组合：

| 功能             | 参数           | 默认值 |
| -------------- | ------------ | --- |
| P99/P999/P9999 | `-p on\|off` | off |
| PerfLog        | `-s on\|off` | off |
| MemPoint       | `-m on\|off` | off |
| OB Memstat     | `-o on\|off` | on  |
| OB Cachestat   | `-k on\|off` | on  |

例如：

```bash
# 开启分位数和 PerfLog
bash build.sh package -p on -s on

# 开启 MemPoint，关闭两个 OB 观测命令
bash build.sh package -m on -o off -k off
```

只有显式传入的开关才会写入 RPM `Release` 后缀。例如 `-p on -s on` 会生成包含 `.p99_on.perflog_on` 的发行号；没有显式传入的默认项不追加后缀。

#### 打包过程和输出

脚本依次执行：

1. 从当前提交生成 `spdiag-<版本>.tar.gz`；
2. 以 RelWithDebInfo 构建共享 SDK、CLI 和测试；
3. 在 spec 的 `%check` 阶段运行 CTest；
4. 单独配置静态构建目录，生成两个静态库；
5. 使用 `rpmbuild -ba` 生成二进制 RPM 和源码 RPM；
6. 将所有 RPM 复制到仓库的 `output/`。

任何 CTest 失败都会中止打包。默认 rpmbuild 工作目录是 `$HOME/rpmbuild`，可覆盖：

```bash
RPM_TOPDIR=/data/rpmbuild bash build.sh package
```

构建完成后：

```bash
ls -1 output/*.rpm
```

典型结果包括：

```text
spdiag-<version>-<release>.<dist>.aarch64.rpm
spdiag-devel-<version>-<release>.<dist>.aarch64.rpm
spdiag-static-<version>-<release>.<dist>.aarch64.rpm
spdiag-<version>-<release>.<dist>.src.rpm
```

具体版本号、发行号和发行版后缀以实际输出为准。

### 3.3 安装已经生成的 RPM

建议使用 `dnf` 安装本地 RPM，以便自动处理依赖。下面的 `[0-9]*` 用来避免主包通配符误匹配 `-devel`、`-static` 和调试包。

#### 只安装 CLI 和运行库

```bash
sudo dnf install ./output/spdiag-[0-9]*.aarch64.rpm
```

#### 安装动态 SDK 开发环境

```bash
sudo dnf install \
  ./output/spdiag-[0-9]*.aarch64.rpm \
  ./output/spdiag-devel-[0-9]*.aarch64.rpm
```

安装后，第三方项目可直接使用 `find_package(SpDiag CONFIG REQUIRED)`。

#### 安装静态 SDK

```bash
sudo dnf install \
  ./output/spdiag-[0-9]*.aarch64.rpm \
  ./output/spdiag-devel-[0-9]*.aarch64.rpm \
  ./output/spdiag-static-[0-9]*.aarch64.rpm
```

如果这些包已经进入配置好的软件仓库，只写 `dnf install spdiag-static` 即可由依赖自动安装另外两个包。直接安装本地文件时应像上例一样同时传入三个 RPM。安装后会同时存在动态 target `SpDiag::spdiag_lib` 和静态 target `SpDiag::spdiag_static`。

### 3.4 安装后检查

```bash
# 查询版本和包依赖
rpm -q spdiag
rpm -q --requires spdiag-devel
rpm -q --requires spdiag-static

# 查看每个包安装了什么
rpm -ql spdiag
rpm -ql spdiag-devel
rpm -ql spdiag-static

# 验证 CLI
spdiag --version
spdiag --help
```

关键文件通常位于：

```text
/usr/bin/spdiag
/usr/lib64/libspdiag.so.*
/usr/include/spdiag/
/usr/lib64/cmake/SpDiag/
/etc/spdiag/spdiag.conf
```

实际 libdir 以 `rpm --eval '%{_libdir}'` 和 `rpm -ql` 输出为准。

### 3.5 升级、配置文件和卸载

升级前先停止采集，并停止仍加载旧共享库的业务程序：

```bash
spdiag stop
sudo dnf upgrade \
  ./output/spdiag-[0-9]*.aarch64.rpm \
  ./output/spdiag-devel-[0-9]*.aarch64.rpm
```

`/etc/spdiag/spdiag.conf` 以 `%config(noreplace)` 安装。升级不会静默覆盖用户修改；如果新版本带来不同配置，可能生成 `spdiag.conf.rpmnew`，需要人工比较和合并。

卸载：

```bash
sudo dnf remove spdiag-static spdiag-devel spdiag
```

### 3.6 从源码 RPM 重建

```bash
rpmbuild --rebuild output/spdiag-*.src.rpm
```

重建结果写入当前用户的 `%{_topdir}/RPMS/`。源码 RPM 对应打包时的 Git 提交，不包含当时工作区中的未提交内容。

## 4. 在其他项目中使用 SpDiag

PerfPoint 接入包含两个相互独立的部分：

1. **业务程序链接 SDK**：在被测代码中定义点位并调用 `Start()`/`End()`；
2. **运行 CLI**：`spdiag start/show/watch/stop` 通过共享内存控制和读取数据。

业务程序即使没有执行 `spdiag start` 也应能正常运行，只是不会产生可供 CLI 展示的 PerfPoint 数据。

### 4.1 选择动态链接或静态链接

同时安装动态库和静态库不会自动决定业务程序使用哪一个。最终链接方式由业务项目的 CMake 配置决定。

| 链接方式 | 安装包             | CMake 写法                   | 运行时要求                                  |
| ---- | --------------- | -------------------------- | -------------------------------------- |
| 动态链接 | `spdiag-devel`  | 链接 `SpDiag::spdiag_lib`    | 运行环境必须存在同版本 `libspdiag.so.*`           |
| 静态链接 | `spdiag-static` | 链接 `SpDiag::spdiag_static` | SpDiag SDK 已进入业务二进制，不依赖 `libspdiag.so` |

动态链接写法：

```cmake
find_package(SpDiag CONFIG REQUIRED)
target_link_libraries(my_service PRIVATE SpDiag::spdiag_lib)
```

静态链接写法：

```cmake
find_package(SpDiag CONFIG REQUIRED)
target_link_libraries(my_service PRIVATE SpDiag::spdiag_static)
```

两个 target 可以同时存在，业务程序最终使用哪一种完全由 `target_link_libraries` 中选择的 target 决定。

编译后可以使用 `ldd` 判断：

```bash
ldd ./build/my_service | grep spdiag
```

- 显示 `libspdiag.so`：业务程序使用动态库；
- 没有 SpDiag 输出：通常表示使用静态库，可结合构建日志或 `nm` 进一步确认。

一般建议优先使用动态链接，因为安装包提供的 CMake target 会自动传递头文件、系统依赖和与 SDK 一致的公开编译宏。

### 4.2 方式一：使用系统动态 SDK

#### 安装 SDK

使用 RPM：

```bash
sudo dnf install spdiag-devel
```

或从源码安装共享库：

```bash
bash build.sh -r -l on
```

#### 业务 CMake

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_service LANGUAGES CXX)

find_package(SpDiag CONFIG REQUIRED)

add_executable(my_service
  src/main.cpp
  src/request_handler.cpp)

target_include_directories(my_service PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(my_service PRIVATE
  SpDiag::spdiag_lib)
```

这段配置中与 SpDiag 接入有关的内容如下：

| CMake 配置                                                                             | 作用                                                                                                                 |
| ------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------ |
| `cmake_minimum_required(VERSION 3.16)`                                               | 指定业务项目使用的最低 CMake 版本；SpDiag 同样要求 CMake 3.16 或更高版本。                                                                 |
| `find_package(SpDiag CONFIG REQUIRED)`                                               | 查找 SpDiag 安装的 `SpDiagConfig.cmake`。`CONFIG` 表示只使用配置包模式；`REQUIRED` 表示找不到时立即终止 CMake 配置。成功后会导入 `SpDiag::spdiag_lib`。 |
| `target_include_directories(my_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)` | 把业务自己的 `include/` 加入搜索路径，用于查找 `my_perf_points.h` 和 `my_perf_points.def`。这里不需要手动添加 SpDiag 头文件目录。                    |
| `target_link_libraries(my_service PRIVATE SpDiag::spdiag_lib)`                       | 将业务程序链接到 SpDiag SDK。该导出 target 会传递 SpDiag 头文件路径、共享库、`pthread`/`rt` 等系统依赖，以及构建时启用的公开宏。                              |

这里两个 `PRIVATE` 的含义都是“只用于 `my_service` 自身，不继续向依赖 `my_service` 的其他 CMake target 传播”。

`SpDiag::spdiag_lib` 不是磁盘上的文件名，而是 SpDiag 安装时导出的 CMake target。在当前动态 SDK 安装方式下，它最终指向 `libspdiag.so`。因此不需要在业务 CMake 中手写 `/usr/include`、`/usr/lib64`、`-lspdiag`、`-lpthread` 或 `-lrt`。

如果 SDK 安装在非标准目录：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/spdiag
```

### 4.3 定义业务自己的 PerfPoint

建议把点位定义放在业务项目内，而不是修改 SpDiag 源码。例如：

```text
my_service/
├── CMakeLists.txt
├── include/
│   ├── my_perf_points.def
│   └── my_perf_points.h
└── src/
    ├── main.cpp
    └── request_handler.cpp
```

#### 第一步：定义点位

`include/my_perf_points.def`：

```cpp
PERF_KEY_DEF(REQUEST_PARSE,   "Request", "Parse")
PERF_KEY_DEF(REQUEST_EXECUTE, "Request", "Execute")
PERF_KEY_DEF(STORAGE_WRITE,   "Storage", "Write")
```

每行依次是：

| 字段                | 含义                |
| ----------------- | ----------------- |
| `REQUEST_EXECUTE` | 生成的 `PerfKey` 枚举名 |
| `"Request"`       | CLI 中显示的模块名       |
| `"Execute"`       | CLI 中显示的点位名       |

#### 第二步：创建统一包装头

`include/my_perf_points.h`：

```cpp
#pragma once

#define SPDIAG_PERF_DEF_FILE "my_perf_points.def"
#define SPDIAG_PROGRAM_NAME "my_service"
#include <spdiag/auto_perf.h>
```

`SPDIAG_PERF_DEF_FILE` 告诉 `auto_perf.h` 从哪个文件生成 `PerfKey` 和名称数组。`SPDIAG_PROGRAM_NAME` 用于在 CLI 中区分不同进程；不定义时会使用类似 `proc_<PID>` 的名字。

所有使用业务点位的源文件都应包含这个包装头，不要在不同源文件里为同一个程序定义不同顺序的点位表。

#### 第三步：在业务路径打点

```cpp
#include "my_perf_points.h"

int HandleRequest()
{
    SpDiag::PerfPoint point(
        PerfKey::REQUEST_EXECUTE,
        SpDiag::PerfLevel::KEY_MODULE);

    point.Start();
    int rc = ExecuteRequest();
    point.End(rc);
    return rc;
}
```

`End(0)` 记录成功，非零返回码进入失败类统计。必须显式调用 `End()`；未结束的对象析构时记录为 Abandon，不会自动记为成功。

存在提前返回时，应先结束点位：

```cpp
point.Start();
auto result = AllocateBuffer();
if (!result) {
    point.End(-1);
    return ERROR_ALLOC;
}
point.End(0);
```

修改业务 `.def` 文件后，只需重新编译业务程序，不需要重新编译 SpDiag。

#### 运行期与编译期跳过打点

SpDiag 支持在运行期或编译期跳过 `PerfPoint` 打点，两种方式的适用场景和开销不同。

**运行期跳过打点**

对已经编译的业务程序，可以执行：

```bash
spdiag stop
```

`spdiag stop` 关闭 SpDiag 共享内存采集。业务程序中的 `PerfPoint` 代码仍然存在，但打点处检查到未启用后会直接返回，典型开销一次Start()+End()约为 3 ns。这种方式无需重新编译或重启业务程序，便于动态开启和关闭采集。

**编译期跳过打点**

如果希望编译后的业务程序不包含打点开销，可以在编译业务程序时定义 `SPDIAG_DISABLE`：

```bash
export CXXFLAGS="${CXXFLAGS} -DSPDIAG_DISABLE"
cmake -S . -B build-disabled
cmake --build build-disabled --parallel
```

这种方式不需要修改业务项目的 `CMakeLists.txt`。`CXXFLAGS` 环境变量必须在 CMake 首次配置时设置；CMake 会缓存编译参数，因此建议使用新的构建目录 `build-disabled`。

定义 `SPDIAG_DISABLE` 后，`SpDiag::PerfPoint` 的构造函数、`Start()`、`End()` 和 `Abandon()` 均为头文件内联空函数，`auto_perf.h` 也不会执行自动注册。业务代码的使用方式无需改变：

```cpp
SpDiag::PerfPoint point(
    PerfKey::REQUEST_EXECUTE,
    SpDiag::PerfLevel::KEY_MODULE);
point.Start();
int rc = ExecuteRequest();
point.End(rc);
```

在 Release 优化编译中，编译器会删除这些空函数调用和空对象，打点运行时开销为零。如果业务项目通过 `add_subdirectory` 同时编译 SpDiag SDK 源码，不要将 `SPDIAG_DISABLE` 全局传递给 SDK 目标；该宏应只定义给用户程序。

### 4.4 方式二：使用系统静态 SDK

安装静态包：

```bash
sudo dnf install spdiag-static
```

<br />

```cmake
find_package(SpDiag CONFIG REQUIRED)
target_link_libraries(my_service PRIVATE SpDiag::spdiag_static)
```

`SpDiag::spdiag_static` 会自动对应 `libspdiag.a`，并传递 `libspdiag_logger.a`、`pthread`、`rt`、头文件路径以及该 RPM 构建时启用的公开宏。`SpDiag::spdiag_lib` 仍然指向 `libspdiag.so`，不会因为安装静态包而改变。

其他使用方式与动态库相同。

可确认静态 target 已安装：

```bash
rpm -ql spdiag-static | grep SpDiagStaticTargets
```

### 4.5 构建并确认接入结果

```bash
cmake -S . -B build
cmake --build build --parallel
```

动态链接方式可检查：

```bash
ldd ./build/my_service | grep spdiag
```

源码静态或 RPM 静态方式不会出现在 `ldd` 中，可以通过构建日志、`nm` 或运行验证确认。运行时的完整验证流程见下一节。

## 5. 运行验证

```bash
# 终端 1：创建共享内存
spdiag start

# 终端 2：运行已集成 SDK 的业务程序
./build/my_app

# 终端 3：查看数据
spdiag show
spdiag show --detail
spdiag watch --interval 1000

# 完成后清理
spdiag stop
```

默认共享内存支持 Linux inotify 重连，因此业务进程可先启动，再执行 `spdiag start`。自定义共享内存名不在默认 watcher 的自动重连路径中，建议先创建共享内存再启动业务程序。

排查无数据时依次确认：

1. `spdiag status` 显示共享内存存在；
2. 业务二进制确实链接到预期 SpDiag SDK；
3. `.def` 文件已进入业务编译，且 `PerfKey` 顺序一致；
4. 被测代码执行到了 `Start()` 和 `End()`；
5. `--filter`、`--level`、`--core` 没有过滤掉数据。

更多命令见 [QuickStart.md](QuickStart.md) 和 [CLI\_REFERENCE.md](docs/zh/CLI_REFERENCE.md)。

## 6. Docker 注意事项

本仓库不提供预构建镜像。PerfPoint 使用 POSIX shared memory，建议为容器设置足够的 `/dev/shm`：

```bash
docker run -it --shm-size=256m --name spdiag-dev <image> /bin/bash
```

eBPF/perf\_event 功能还需要相应 capabilities、设备和内核接口。开发环境可用 `--privileged` 快速验证，但生产环境应按最小权限原则配置。

容器默认拥有独立 IPC namespace。若业务在容器内、CLI 在宿主机，需要使用 `--ipc=host`；此时两侧不要同时对同一名称执行 `spdiag start`。容器退出前执行 `spdiag stop`，避免共享内存残留。
