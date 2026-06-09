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
get_filename_component(HISDK_SOURCE "${HISDK_DIR}" DIRECTORY)
set(MPP_SAMPLE_COMMON "${HISDK_SOURCE}/mpp/sample/common")

# ================================================================
# 查找依赖库 (musl libc: pthread/rt/dl 集成在 libc 中)
find_library(MPI_LIB       libss_mpi.so        PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(OSAL_LIB      libot_osal.so       PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(SYSBIND_LIB   libss_mpi_sysbind.so PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(SYSMEM_LIB    libss_mpi_sysmem.so  PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(MPI_AE_LIB    libss_mpi_ae.so       PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(MPI_ISP_LIB   libss_mpi_isp.so      PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(OT_MPI_ISP_LIB libot_mpi_isp.so     PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(MPI_AWB_LIB   libss_mpi_awb.so      PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(SENSOR_SC4336_LIB libsns_sc4336p.so PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(DEHAZE_LIB    libdehaze.so          PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(EXT_STATS_LIB libextend_stats.so    PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(DRC_LIB       libdrc.so             PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(LDCI_LIB      libldci.so            PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(BNR_LIB       libbnr.so             PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(CALCFLICKER_LIB libcalcflicker.so   PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(IR_AUTO_LIB   libir_auto.so         PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_library(ACS_LIB       libacs.so             PATHS ${MPP_LIB} NO_CMAKE_FIND_ROOT_PATH REQUIRED)
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
)
include_directories(SYSTEM
    ${MPP_INCLUDE}
    ${MPP_SAMPLE_COMMON}
)

# ================================================================
# 源码文件
# ================================================================
set(SOURCES
    # 应用层
    src/app/main.cpp
    src/app/state_machine.cpp

    # 媒体管线和帧处理
    src/media/frame_utils.cpp
    src/media/pipeline/mpp_media_pipeline.cpp

    # 媒体层 - EIS 防抖
    src/media/eis/eis_processor.cpp

    # 网络与存储
    src/network/rtsp_server.cpp
    src/storage/raw_video_recorder.cpp

    # 硬件抽象层
    src/hal/icm20948_hal.cpp

    # 工具类
    src/utils/logger.cpp
    src/utils/config_parser.cpp
    src/utils/ring_buffer.cpp

    # 海思官方 sample 公共适配层
    ${MPP_SAMPLE_COMMON}/sample_comm_sys.c
    ${MPP_SAMPLE_COMMON}/sample_comm_isp.c
    ${MPP_SAMPLE_COMMON}/sample_comm_vi.c
    ${MPP_SAMPLE_COMMON}/sample_comm_vpss.c
    ${MPP_SAMPLE_COMMON}/sample_comm_venc.c
    ${MPP_SAMPLE_COMMON}/sample_ipc.c
)

# 海思官方 sample 适配代码包含条件编译下的未使用参数，不影响项目源码告警级别。
set_source_files_properties(
    ${MPP_SAMPLE_COMMON}/sample_comm_sys.c
    ${MPP_SAMPLE_COMMON}/sample_comm_isp.c
    ${MPP_SAMPLE_COMMON}/sample_comm_vi.c
    ${MPP_SAMPLE_COMMON}/sample_comm_vpss.c
    ${MPP_SAMPLE_COMMON}/sample_comm_venc.c
    ${MPP_SAMPLE_COMMON}/sample_ipc.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter"
)

# ================================================================
# 生成可执行文件
# ================================================================
add_executable(leavr_app ${SOURCES})
target_compile_definitions(leavr_app PRIVATE
    hi3516cv610
    OT_XXXX
    ISP_V2
    SENSOR0_TYPE=SC4336P_MIPI_4M_30FPS_10BIT
    SENSOR1_TYPE=SC4336P_MIPI_4M_30FPS_10BIT
    SC4336P_MIPI_4M_30FPS_10BIT_SELECT=y
    BOARD_TYPE=DMEB_QFN
)

# 目标板从系统库目录加载依赖，禁止写入开发机绝对路径。
set_target_properties(leavr_app PROPERTIES
    SKIP_BUILD_RPATH TRUE
    BUILD_WITH_INSTALL_RPATH FALSE
    INSTALL_RPATH ""
)

# 链接库
target_link_libraries(leavr_app
    ${MPI_LIB}
    securec
    ${OSAL_LIB}
    ${SYSBIND_LIB}
    ${SYSMEM_LIB}
    ${MPI_AE_LIB}
    ${MPI_ISP_LIB}
    ${OT_MPI_ISP_LIB}
    ${MPI_AWB_LIB}
    ${SENSOR_SC4336_LIB}
    ${DEHAZE_LIB}
    ${EXT_STATS_LIB}
    ${DRC_LIB}
    ${LDCI_LIB}
    ${BNR_LIB}
    ${CALCFLICKER_LIB}
    ${IR_AUTO_LIB}
    ${ACS_LIB}
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
