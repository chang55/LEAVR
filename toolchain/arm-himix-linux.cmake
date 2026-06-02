# LEAVR 执法记录仪 - 海思 HI3516 交叉编译工具链
# 适用: arm-himix610-linux / arm-himix510-linux 等

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 工具链路径 (根据实际 SDK 路径修改)
set(TOOLCHAIN_PREFIX "/opt/hisi-linux/arm-himix-linux")

# 编译器
set(CMAKE_C_COMPILER    "${TOOLCHAIN_PREFIX}/bin/arm-himix-linux-gcc")
set(CMAKE_CXX_COMPILER  "${TOOLCHAIN_PREFIX}/bin/arm-himix-linux-g++")
set(CMAKE_AR             "${TOOLCHAIN_PREFIX}/bin/arm-himix-linux-ar")
set(CMAKE_LINKER         "${TOOLCHAIN_PREFIX}/bin/arm-himix-linux-ld")
set(CMAKE_STRIP          "${TOOLCHAIN_PREFIX}/bin/arm-himix-linux-strip")
set(CMAKE_OBJCOPY        "${TOOLCHAIN_PREFIX}/bin/arm-himix-linux-objcopy")
set(CMAKE_OBJDUMP        "${TOOLCHAIN_PREFIX}/bin/arm-himix-linux-objdump")
set(CMAKE_RANLIB         "${TOOLCHAIN_PREFIX}/bin/arm-himix-linux-ranlib")

# 系统根目录
set(CMAKE_SYSROOT "${TOOLCHAIN_PREFIX}/target")
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")

# 仅在目标 sysroot 中查找库和头文件
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 交叉编译标志
set(HISI_CROSS_COMPILE ON CACHE BOOL "Cross compiling for HiSilicon" FORCE)

# 海思 SDK 路径
set(HISDK_DIR "/opt/hisi-sdk/hi3516" CACHE PATH "HiSilicon SDK root" FORCE)

message(STATUS "Toolchain: ${TOOLCHAIN_PREFIX}")
message(STATUS "HisSDK: ${HISDK_DIR}")