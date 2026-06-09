#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT="/tmp/leavr_core_tests"

g++ -std=c++11 -Wall -Wextra -pthread \
    -I"${PROJECT_DIR}/include" \
    -I"${PROJECT_DIR}/src" \
    "${PROJECT_DIR}/tests/test_core.cpp" \
    "${PROJECT_DIR}/src/app/state_machine.cpp" \
    "${PROJECT_DIR}/src/media/eis/eis_processor.cpp" \
    "${PROJECT_DIR}/src/media/frame_utils.cpp" \
    "${PROJECT_DIR}/src/utils/logger.cpp" \
    -o "${OUTPUT}"

"${OUTPUT}"
