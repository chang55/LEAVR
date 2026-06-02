cmake_minimum_required(VERSION 3.10)
project(leavr VERSION 1.0.0 LANGUAGES C CXX)

# ================================================================
# 工具链配置
# ================================================================
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    include(${CMAKE_TOOLCHAIN_FILE})
else()
    message(STATUS "Using native toolchain (for host build/debug only)")
    set(CMAKE_C_COMPILER gcc)
    set(CMAKE_CXX_COMPILER g++)
endif()

# ================================================================
# C++ 标准
# ================================================================
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ================================================================
# 编译选项
# ================================================================
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# 嵌入式目标: 禁用异常和 RTTI 以减小体积
if(HISI_CROSS_COMPILE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
    add_definitions(-DLEAVR_TARGET_EMBEDDED)
endif()

# 调试模式
option(LEAVR_DEBUG "Enable debug logging and assertions" OFF)
if(LEAVR_DEBUG)
    add_definitions(-DLEAVR_DEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
endif()

# ================================================================
# 海思 SDK 路径 (交叉编译时需要)
# ================================================================
set(HISDK_DIR "/opt/hisi-sdk/hi3516" CACHE PATH "HiSilicon SDK root directory")
set(MPP_INCLUDE "${HISDK_DIR}/include" CACHE PATH "HiMPP include path")
set(MPP_LIB "${HISDK_DIR}/lib" CACHE PATH "HiMPP library path")
set(THIRD_PARTY_DIR "${HISDK_DIR}/third_party" CACHE PATH "Third party libs path")

# ================================================================
# 查找依赖库 (交叉编译时使用 find_library, 主机编译时跳过)
# ================================================================
if(HISI_CROSS_COMPILE)
    find_library(MPI_LIB      libmpi.so      ${MPP_LIB} REQUIRED)
    find_library(PTHREAD_LIB  libpthread.so  REQUIRED)
    find_library(RT_LIB       librt.so       REQUIRED)
    find_library(DL_LIB       libdl.so       REQUIRED)
    # 第三方库 (按需)
    # find_library(LIVE555_LIB  liblive555.so  ${THIRD_PARTY_DIR})
    # find_library(SQLITE3_LIB  libsqlite3.so  ${THIRD_PARTY_DIR})
    # find_library(MBEDTLS_LIB  libmbedtls.so  ${THIRD_PARTY_DIR})
    # find_library(LVGL_LIB     liblvgl.so     ${THIRD_PARTY_DIR})
    # find_library(MP3LAME_LIB  libmp3lame.so  ${THIRD_PARTY_DIR})
else()
    find_package(Threads REQUIRED)
    set(PTHREAD_LIB Threads::Threads)
    set(RT_LIB "")
    set(DL_LIB "")
endif()

# ================================================================
# 包含路径
# ================================================================
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

if(HISI_CROSS_COMPILE)
    include_directories(${MPP_INCLUDE})
endif()

# ================================================================
# 源码文件
# ================================================================
set(SOURCES
    # 应用层
    src/app/main.cpp
    src/app/state_machine.cpp

    # 媒体层 - EIS 防抖
    src/media/eis/eis_processor.cpp

    # 硬件抽象层
    src/hal/icm20948_hal.cpp

    # 工具类
    src/utils/logger.cpp
    src/utils/config_parser.cpp
    src/utils/ring_buffer.cpp
)

# ================================================================
# 生成可执行文件
# ================================================================
add_executable(leavr_app ${SOURCES})

# 链接库
target_link_libraries(leavr_app
    ${PTHREAD_LIB}
    ${RT_LIB}
    ${DL_LIB}
    m
)

if(HISI_CROSS_COMPILE)
    target_link_libraries(leavr_app ${MPI_LIB})
    # target_link_libraries(leavr_app ${LIVE555_LIB} ${SQLITE3_LIB} ${MBEDTLS_LIB} ${LVGL_LIB} ${MP3LAME_LIB})
endif()

# ================================================================
# 安装规则
# ================================================================
install(TARGETS leavr_app RUNTIME DESTINATION /usr/bin)
install(FILES config/device.conf DESTINATION /mnt/sdcard/Config)

# ================================================================
# 版本信息
# ================================================================
add_definitions(-DLEAVR_VERSION="${PROJECT_VERSION}")
add_definitions(-DLEAVR_BUILD_DATE="${CMAKE_BUILD_DATE}")

# ================================================================
# 打印构建信息
# ================================================================
message(STATUS "========================================")
message(STATUS "  LEAVR 执法记录仪 v${PROJECT_VERSION}")
message(STATUS "  Platform: HI3516 + Linux 4.9")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  Debug: ${LEAVR_DEBUG}")
message(STATUS "  Cross Compile: ${HISI_CROSS_COMPILE}")
message(STATUS "========================================")