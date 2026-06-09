#!/bin/bash
# ============================================================
# LEAVR 执法记录仪 - 固件烧录脚本
# 用法: ./flash.sh <device_ip>
# ============================================================

set -e

DEVICE_IP="${1}"
if [ -z "${DEVICE_IP}" ]; then
    echo "Usage: $0 <device_ip>"
    echo "Example: $0 192.168.1.100"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================="
echo "  LEAVR 固件烧录"
echo "  Target: root@${DEVICE_IP}"
echo "========================================="

# 检查二进制文件
if [ ! -f "${PROJECT_DIR}/build/leavr_app" ]; then
    echo "Error: leavr_app not found. Run build.sh -C first."
    exit 1
fi

# 创建目标目录
ssh "root@${DEVICE_IP}" "mkdir -p /usr/bin /mnt/sdcard/Config /mnt/sdcard/Log"

# 停止旧进程
echo "Stopping old process..."
ssh "root@${DEVICE_IP}" "killall leavr_app 2>/dev/null || true"
sleep 1

# 烧录
echo "Uploading leavr_app..."
scp "${PROJECT_DIR}/build/leavr_app" "root@${DEVICE_IP}:/usr/bin/"

echo "Uploading device.conf..."
scp "${PROJECT_DIR}/config/device.conf" "root@${DEVICE_IP}:/mnt/sdcard/Config/"

# 设置权限
ssh "root@${DEVICE_IP}" "chmod +x /usr/bin/leavr_app"

# 启动
echo "Starting leavr_app..."
ssh "root@${DEVICE_IP}" "/usr/bin/leavr_app &"

echo "========================================="
echo "  Flash complete!"
echo "========================================="
