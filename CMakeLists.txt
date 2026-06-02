cmake_minimum_required(VERSION 3.10)
project(leavr VERSION 1.0.0 LANGUAGES C CXX)

# ================================================================
# 工具链配置 (仅交叉编译)
# ================================================================
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(FATAL_ERROR
        "CMAKE_TOOLCHAIN_FILE must be set.\n"
        "Usage: cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain/arm-himix-linux.cmake")
endif()
include(${CMAKE_TOOLCHAIN_FILE})

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
# 编译选项 - musl libc: -pthread 替代 -lpthread
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -pthread")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -pthread")

# 嵌入式编译选项
add_definitions(-DLEAVR_TARGET_EMBEDDED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")

# 调试模式
option(LEAVR_DEBUG "Enable debug logging and assertions" OFF)
if(LEAVR_DEBUG)
    add_definitions(-DLEAVR_DEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0")
endif()

# ================================================================
# 海思 SDK 路径 (由 toolchain 文件 HISDK_DIR 提供)
# ================================================================
set(MPP_INCLUDE "${HISDK_DIR}/include")
set(MPP_LIB "${HISDK_DIR}/lib")

# ================================================================
# 查找依赖库 (musl libc: pthread/rt/dl 集成在 libc 中)
find_library(MPI_LIB       libss_mpi.so        PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(SECUREC_LIB   libsecurec.so       PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(OSAL_LIB      libot_osal.so       PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(SYSBIND_LIB   libss_mpi_sysbind.so PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(SYSMEM_LIB    libss_mpi_sysmem.so  PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
# 第三方库 (按需)
# find_library(LIVE555_LIB  liblive555.so  ${THIRD_PARTY_DIR})
# find_library(SQLITE3_LIB  libsqlite3.so  ${THIRD_PARTY_DIR})
# find_library(MBEDTLS_LIB  libmbedtls.so  ${THIRD_PARTY_DIR})
# find_library(LVGL_LIB     liblvgl.so     ${THIRD_PARTY_DIR})
# find_library(MP3LAME_LIB  libmp3lame.so  ${THIRD_PARTY_DIR})

# ================================================================
# 包含路径
# ================================================================
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${MPP_INCLUDE}
)

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
    ${MPI_LIB}
    ${SECUREC_LIB}
    ${OSAL_LIB}
    ${SYSBIND_LIB}
    ${SYSMEM_LIB}
    m
)

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
message(STATUS "  Platform: HI3516 + Linux 4.9 (arm-himix-linux)")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Debug: ${LEAVR_DEBUG}")
message(STATUS "========================================")