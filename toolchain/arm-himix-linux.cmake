# LEAVR 执法记录仪 - 海思 HI3516 交叉编译工具链
# 平台: HI3516CV610 / arm-v01c02-linux-musleabi (musl libc)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 工具链路径
set(TOOLCHAIN_PREFIX "/home/csq/HI3516/gcc-20240318-arm-v01c02-linux-musleabi/arm-v01c02-linux-musleabi-gcc")

# 编译器 (arm-v01c02-linux-musleabi 前缀)
set(CMAKE_C_COMPILER    "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-gcc")
set(CMAKE_CXX_COMPILER  "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-g++")
set(CMAKE_AR             "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-ar")
set(CMAKE_LINKER         "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-ld")
set(CMAKE_STRIP          "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-strip")
set(CMAKE_OBJCOPY        "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-objcopy")
set(CMAKE_OBJDUMP        "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-objdump")
set(CMAKE_RANLIB         "${TOOLCHAIN_PREFIX}/bin/arm-v01c02-linux-musleabi-ranlib")

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

# 海思 SDK 路径 (HiMPP out 目录)
set(HISDK_DIR "/home/csq/HI3516/Hi3516CV610_SDK_V1.0.2.0/smp/a7_linux/source/out" CACHE PATH "HiSilicon SDK root" FORCE)

message(STATUS "Toolchain: ${TOOLCHAIN_PREFIX}")
message(STATUS "HisSDK: ${HISDK_DIR}")