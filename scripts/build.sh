#!/bin/bash
# ============================================================
# LEAVR 执法记录仪 - 构建脚本 (arm-himix-linux 交叉编译)
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  -h, --help          Show this help"
    echo "  -d, --debug         Build with debug symbols"
    echo "  -r, --release       Release build (default)"
    echo "  -C, --clean         Clean build directory first"
    echo "  -j N                Parallel jobs (default: nproc)"
    echo ""
    echo "Examples:"
    echo "  $0                  # Release build"
    echo "  $0 -d               # Debug build"
    echo "  $0 -d -C            # Clean debug build"
}

# 默认参数
BUILD_TYPE="Release"
CLEAN=0
DEBUG=0
JOBS=$(nproc 2>/dev/null || echo 4)

# 解析参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        -d|--debug)
            DEBUG=1
            BUILD_TYPE="Debug"
            shift
            ;;
        -r|--release)
            BUILD_TYPE="Release"
            shift
            ;;
        -C|--clean)
            CLEAN=1
            shift
            ;;
        -j)
            JOBS="$2"
            shift 2
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            usage
            exit 1
            ;;
    esac
done

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  LEAVR 执法记录仪 - Build Script (arm-himix-linux)${NC}"
echo -e "${GREEN}========================================${NC}"
echo "  Project:    ${PROJECT_DIR}"
echo "  Build Dir:  ${BUILD_DIR}"
echo "  Build Type: ${BUILD_TYPE}"
echo "  Debug:      ${DEBUG}"
echo "  Jobs:       ${JOBS}"
echo -e "${GREEN}========================================${NC}"

# 清理
if [[ ${CLEAN} -eq 1 ]]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "${BUILD_DIR}"
fi

# 创建构建目录
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# CMake 参数
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"

if [[ ${DEBUG} -eq 1 ]]; then
    CMAKE_ARGS="${CMAKE_ARGS} -DLEAVR_DEBUG=ON"
fi

# 工具链文件
TOOLCHAIN_FILE="${PROJECT_DIR}/toolchain/arm-himix-linux.cmake"
if [[ ! -f "${TOOLCHAIN_FILE}" ]]; then
    echo -e "${RED}Error: Toolchain file not found: ${TOOLCHAIN_FILE}${NC}"
    echo -e "${YELLOW}Please edit toolchain/arm-himix-linux.cmake to set your toolchain path${NC}"
    exit 1
fi
CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"

# 配置
echo -e "${GREEN}Running CMake...${NC}"
cmake ${CMAKE_ARGS} "${PROJECT_DIR}"

# 构建
echo -e "${GREEN}Building...${NC}"
make -j${JOBS}

# 成功
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Build Successful!${NC}"
echo -e "${GREEN}========================================${NC}"

# 输出文件信息
if [[ -f "${BUILD_DIR}/leavr_app" ]]; then
    echo ""
    echo "Output: ${BUILD_DIR}/leavr_app"
    file "${BUILD_DIR}/leavr_app"
    du -h "${BUILD_DIR}/leavr_app"
fi

echo ""
echo "Install commands:"
echo "  scp build/leavr_app root@<device_ip>:/usr/bin/"
echo "  scp config/device.conf root@<device_ip>:/mnt/sdcard/Config/"