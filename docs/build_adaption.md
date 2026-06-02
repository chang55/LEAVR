# LEAVR 构建系统适配记录

> 日期: 2026-06-02  
> 目标平台: HI3516CV610 / arm-v01c02-linux-musleabi (musl libc)  
> 工具链: GCC 10.3.0, gcc-20240318-arm-v01c02-linux-musleabi

---

## 一、背景

将 LEAVR 执法记录仪项目从通用 CMake 配置（支持本机 + 交叉编译双模式）改造为**纯交叉编译**模式，并适配实际的 HI3516CV610 工具链和 SDK 路径。

---

## 二、涉及文件

| 文件 | 操作 |
|------|------|
| `toolchain/arm-himix-linux.cmake` | 重写 |
| `CMakeLists.txt` | 大幅修改 |
| `scripts/build.sh` | 简化 |

---

## 三、`toolchain/arm-himix-linux.cmake` 修改明细

### 3.1 编译器前缀变更

| 配置项 | 旧值 | 新值 |
|--------|------|------|
| `TOOLCHAIN_PREFIX` | `/opt/hisi-linux/arm-himix-linux` | `/home/csq/HI3516/gcc-20240318-arm-v01c02-linux-musleabi/arm-v01c02-linux-musleabi-gcc` |
| 编译器前缀 | `arm-himix-linux-` | `arm-v01c02-linux-musleabi-` |
| `HISDK_DIR` | `/opt/hisi-sdk/hi3516` | `/home/csq/HI3516/Hi3516CV610_SDK_V1.0.2.0/smp/a7_linux/source/out` |

### 3.2 编译器二进制清单

```cmake
set(CMAKE_C_COMPILER    "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-gcc")
set(CMAKE_CXX_COMPILER  "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-g++")
set(CMAKE_AR             "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-ar")
set(CMAKE_LINKER         "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-ld")
set(CMAKE_STRIP          "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-strip")
set(CMAKE_OBJCOPY        "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-objcopy")
set(CMAKE_OBJDUMP        "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-objdump")
set(CMAKE_RANLIB         "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-ranlib")
```

---

## 四、`CMakeLists.txt` 修改明细

### 4.1 纯交叉编译（移除本机构建分支）

| 位置 | 旧逻辑 | 新逻辑 |
|------|--------|--------|
| 工具链加载 | `if(DEFINED CMAKE_TOOLCHAIN_FILE) include(...) else 本地gcc` | 强制要求，否则 `FATAL_ERROR` |
| `-fno-exceptions -fno-rtti` | `if(HISI_CROSS_COMPILE)` 条件启用 | 无条件启用 |
| `find_library` 依赖查找 | `if(HISI_CROSS_COMPILE)` / `else find_package(Threads)` | 无条件查找交叉编译库 |
| `include_directories` | `MPP_INCLUDE` 条件添加 | 无条件包含 |

### 4.2 API 版本适配 (CV610)

| 项 | 旧值 (HI3516 旧版) | 新值 (HI3516CV610) |
|----|---------------------|---------------------|
| MPI 库名 | `libmpi.so` | `libss_mpi.so` |
| 头文件前缀 | `hi_comm_vi.h` | `ot_common_vi.h` |
| API 前缀 | `hi_mpi_*` | `ss_mpi_*` / `ot_mpi_*` |

### 4.3 musl libc 适配

musl libc 将 `pthread`、`librt`、`libdl` 的功能直接集成在 `libc.so` 中，无需独立链接：

| 移除项 | 替代方案 |
|--------|----------|
| `find_library(PTHREAD_LIB libpthread.so)` | `CMAKE_C_FLAGS` / `CMAKE_CXX_FLAGS` 添加 `-pthread` |
| `find_library(RT_LIB librt.so)` | 移除（musl libc 内置） |
| `find_library(DL_LIB libdl.so)` | 移除（musl libc 内置） |

### 4.4 新增 SDK 依赖库

`libss_mpi.so` 有传递依赖，需显式链接：

```cmake
find_library(MPI_LIB       libss_mpi.so         PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(SECUREC_LIB   libsecurec.so        PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(OSAL_LIB      libot_osal.so        PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(SYSBIND_LIB   libss_mpi_sysbind.so PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(SYSMEM_LIB    libss_mpi_sysmem.so  PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
```

> 使用 `NO_CMAKE_FIND_ROOT_PATH` 绕过 `CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY` 的 sysroot 限制，允许搜索 sysroot 外的 SDK 库路径。

### 4.5 移除 CMakeLists.txt 中的 SDK 路径覆盖

旧版 `CMakeLists.txt` 第 44 行：

```cmake
set(HISDK_DIR "/opt/hisi-sdk/hi3516" CACHE PATH "HiSilicon SDK root directory")
```

会覆盖 `toolchain/arm-himix-linux.cmake` 中 `FORCE` 设置的正确路径，已移除。现在 `MPP_INCLUDE` / `MPP_LIB` 直接从 toolchain 的 `HISDK_DIR` 派生：

```cmake
set(MPP_INCLUDE "${HISDK_DIR}/include")
set(MPP_LIB "${HISDK_DIR}/lib")
```

### 4.6 编译选项

```cmake
# musl libc: -pthread 替代 -lpthread
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -pthread")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -pthread")

# 嵌入式编译选项
add_definitions(-DLEAVR_TARGET_EMBEDDED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
```

### 4.7 最终链接库清单

```cmake
target_link_libraries(leavr_app
    ${MPI_LIB}
    ${SECUREC_LIB}
    ${OSAL_LIB}
    ${SYSBIND_LIB}
    ${SYSMEM_LIB}
    m
)
```

---

## 五、`scripts/build.sh` 修改明细

### 5.1 移除本机构建选项

| 改动项 | 说明 |
|--------|------|
| `-c / --cross` 参数 | 移除（交叉编译是唯一模式） |
| `CROSS_COMPILE` 变量 | 移除 |
| 工具链文件检查 | 从 `if CROSS_COMPILE` 条件改为无条件执行 |
| `usage()` 帮助信息 | 移除 native build 示例 |
| 安装命令 | 移除 native 本地运行分支，始终输出 `scp` |

### 5.2 当前使用方式

```bash
./scripts/build.sh           # Release 交叉编译
./scripts/build.sh -d        # Debug 交叉编译
./scripts/build.sh -d -C     # 清理后 Debug 交叉编译
```

---

## 六、构建输出

```
Output: /home/csq/LEAVR/build/leavr_app
Type:   ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV)
        dynamically linked, interpreter /lib/ld-musl-arm.so.1
        80K, not stripped
```

---

## 七、环境依赖一览

| 组件 | 路径 | 状态 |
|------|------|------|
| 交叉编译器 | `/home/csq/HI3516/gcc-20240318-arm-v01c02-linux-musleabi/arm-v01c02-linux-musleabi-gcc/` | ✅ |
| HiMPP SDK | `/home/csq/HI3516/Hi3516CV610_SDK_V1.0.2.0/smp/a7_linux/source/out/` | ✅ |
| CMake | 系统包 `cmake` (3.22.1) | ✅ |

---

## 八、已知注意事项

1. **API 版本差异**: CV610 使用 `ot_` / `ss_` 前缀的 API，与旧版 HI3516 的 `hi_` 前缀不兼容。后续编写 HiMPP 调用代码时需注意。
2. **库冲突警告**: `libsecurec.so` 同时存在于 sysroot 和 SDK out 目录中，构建时有 warning 但不影响结果。
3. **编译器警告**: 源码中有若干 unused parameter/variable 警告（如 `icm20948_hal.cpp`、`main.cpp`），属于框架代码的 TODO 占位，不影响功能。
