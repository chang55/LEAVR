/**
 * @file icm20948_hal.cpp
 * @brief ICM-20948 I2C 驱动实现
 *        在真实海思平台上通过 /dev/i2c-N 操作,
 *        此处提供带有模拟数据的框架实现用于编译验证
 */

#include "hal/icm20948_hal.h"
#include "utils/logger.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <ctime>

// 如果不在海思平台, 使用 linux/i2c-dev.h
#ifdef __linux__
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#endif

namespace leavr {

// ICM-20948 寄存器定义
enum Icm20948Reg : uint8_t {
    REG_WHO_AM_I       = 0x00,
    REG_USER_CTRL      = 0x03,
    REG_LP_CONFIG      = 0x05,
    REG_PWR_MGMT_1     = 0x06,
    REG_PWR_MGMT_2     = 0x07,

    REG_INT_PIN_CFG    = 0x0F,
    REG_INT_ENABLE     = 0x10,
    REG_INT_ENABLE_1   = 0x11,
    REG_INT_ENABLE_2   = 0x12,
    REG_INT_ENABLE_3   = 0x13,

    REG_I2C_MST_STATUS = 0x17,

    REG_ACCEL_XOUT_H   = 0x2D,
    REG_GYRO_XOUT_H    = 0x33,

    REG_REG_BANK_SEL   = 0x7F,

    // Bank 2 - Accelerometer Configuration
    REG_ACCEL_SMPLRT_DIV_1 = 0x10,
    REG_ACCEL_SMPLRT_DIV_2 = 0x11,
    REG_ACCEL_CONFIG_1  = 0x14,

    // Bank 2 - Gyroscope Configuration
    REG_GYRO_SMPLRT_DIV = 0x00,
    REG_GYRO_CONFIG_1   = 0x01,
    REG_GYRO_CONFIG_2   = 0x02,
};

static constexpr uint8_t ICM20948_WHO_AM_I_VALUE = 0xEA;

Icm20948Hal::~Icm20948Hal() {
    Stop();
    if (i2c_fd_ >= 0) {
        close(i2c_fd_);
        i2c_fd_ = -1;
    }
}

int Icm20948Hal::WriteRegister(uint8_t reg, uint8_t value) {
    if (i2c_fd_ < 0) return LEAVR_ERR_NOT_INIT;

#ifdef __linux__
    uint8_t buf[2] = {reg, value};
    if (write(i2c_fd_, buf, 2) != 2) {
        return LEAVR_ERR_I2C_FAILED;
    }
#endif
    return LEAVR_OK;
}

int Icm20948Hal::ReadRegisters(uint8_t reg, uint8_t* buf, size_t len) {
    if (i2c_fd_ < 0) return LEAVR_ERR_NOT_INIT;

#ifdef __linux__
    if (write(i2c_fd_, &reg, 1) != 1) {
        return LEAVR_ERR_I2C_FAILED;
    }
    if (read(i2c_fd_, buf, len) != static_cast<ssize_t>(len)) {
        return LEAVR_ERR_I2C_FAILED;
    }
#endif
    return LEAVR_OK;
}

int Icm20948Hal::SelectBank(uint8_t bank) {
    if (bank > 3) return LEAVR_ERR_PARAM;
    return WriteRegister(REG_REG_BANK_SEL, static_cast<uint8_t>(bank << 4));
}

int Icm20948Hal::Init(const char* i2c_dev, uint8_t addr) {
    if (!i2c_dev || (addr != 0x68 && addr != 0x69)) return LEAVR_ERR_PARAM;
    addr_ = addr;
    LOG_INFO("ICM-20948: Opening %s addr=0x%02X", i2c_dev, addr);

#ifdef __linux__
    i2c_fd_ = open(i2c_dev, O_RDWR);
    if (i2c_fd_ < 0) {
        LOG_ERROR("ICM-20948: Failed to open %s", i2c_dev);
        return LEAVR_ERR_I2C_FAILED;
    }

    if (ioctl(i2c_fd_, I2C_SLAVE, addr_) < 0) {
        LOG_ERROR("ICM-20948: Failed to set slave address");
        close(i2c_fd_);
        i2c_fd_ = -1;
        return LEAVR_ERR_I2C_FAILED;
    }
#else
    // 非 Linux 平台模拟: 跳过 I2C
    i2c_fd_ = 0;
#endif

    if (SelectBank(0) != LEAVR_OK) return LEAVR_ERR_I2C_FAILED;

    // WHO_AM_I 验证
#ifdef __linux__
    uint8_t whoami = 0;
    if (ReadRegisters(REG_WHO_AM_I, &whoami, 1) != LEAVR_OK) {
        return LEAVR_ERR_I2C_FAILED;
    }
    if (whoami != ICM20948_WHO_AM_I_VALUE) {
        LOG_ERROR("ICM-20948: Wrong WHO_AM_I: 0x%02X (expected 0x%02X)",
                  whoami, ICM20948_WHO_AM_I_VALUE);
        return LEAVR_ERR_SENSOR_INIT;
    }
#else
    LOG_INFO("ICM-20948: WHO_AM_I check skipped (simulated)");
#endif

    // 唤醒设备并应用 MVP 所需的 200Hz 配置。
    if (WriteRegister(REG_PWR_MGMT_1, 0x01) != LEAVR_OK ||
        SetGyroRange(2000) != LEAVR_OK ||
        SetAccelRange(16) != LEAVR_OK ||
        SetOdr(200, 200) != LEAVR_OK) {
        LOG_ERROR("ICM-20948: Failed to apply sensor configuration");
#ifdef __linux__
        close(i2c_fd_);
#endif
        i2c_fd_ = -1;
        return LEAVR_ERR_SENSOR_INIT;
    }

    LOG_INFO("ICM-20948: Init success, gyro=±%ddps acc=±%dG ODR=%dHz",
             gyro_range_dps_, accel_range_g_, gyro_odr_hz_);
    return LEAVR_OK;
}

int Icm20948Hal::SetOdr(uint16_t acc_odr_hz, uint16_t gyro_odr_hz) {
    if (acc_odr_hz == 0 || gyro_odr_hz == 0 ||
        acc_odr_hz > 1125 || gyro_odr_hz > 1125) {
        return LEAVR_ERR_PARAM;
    }
    acc_odr_hz_ = acc_odr_hz;
    gyro_odr_hz_ = gyro_odr_hz;

    const uint16_t acc_div = static_cast<uint16_t>(1125 / acc_odr_hz - 1);
    const uint8_t gyro_div = static_cast<uint8_t>(1125 / gyro_odr_hz - 1);
    if (SelectBank(2) != LEAVR_OK ||
        WriteRegister(REG_ACCEL_SMPLRT_DIV_1, static_cast<uint8_t>((acc_div >> 8) & 0x0f)) != LEAVR_OK ||
        WriteRegister(REG_ACCEL_SMPLRT_DIV_2, static_cast<uint8_t>(acc_div & 0xff)) != LEAVR_OK ||
        WriteRegister(REG_GYRO_SMPLRT_DIV, gyro_div) != LEAVR_OK ||
        SelectBank(0) != LEAVR_OK) {
        return LEAVR_ERR_I2C_FAILED;
    }
    return LEAVR_OK;
}

int Icm20948Hal::SetAccelRange(int g) {
    accel_range_g_ = g;

    // 设置量程
    uint8_t fs_sel;
    switch (g) {
        case 2:  fs_sel = 0x00; accel_scale_ = 2.0  / 32768.0; break;
        case 4:  fs_sel = 0x01; accel_scale_ = 4.0  / 32768.0; break;
        case 8:  fs_sel = 0x02; accel_scale_ = 8.0  / 32768.0; break;
        case 16: fs_sel = 0x03; accel_scale_ = 16.0 / 32768.0; break;
        default: return LEAVR_ERR_PARAM;
    }

    if (SelectBank(2) != LEAVR_OK ||
        WriteRegister(REG_ACCEL_CONFIG_1, static_cast<uint8_t>((fs_sel << 1) | 0x01)) != LEAVR_OK ||
        SelectBank(0) != LEAVR_OK) {
        return LEAVR_ERR_I2C_FAILED;
    }
    return LEAVR_OK;
}

int Icm20948Hal::SetGyroRange(int dps) {
    gyro_range_dps_ = dps;

    uint8_t fs_sel;
    switch (dps) {
        case 250:  fs_sel = 0x00; gyro_scale_ = 250.0  / 32768.0; break;
        case 500:  fs_sel = 0x01; gyro_scale_ = 500.0  / 32768.0; break;
        case 1000: fs_sel = 0x02; gyro_scale_ = 1000.0 / 32768.0; break;
        case 2000: fs_sel = 0x03; gyro_scale_ = 2000.0 / 32768.0; break;
        default: return LEAVR_ERR_PARAM;
    }

    if (SelectBank(2) != LEAVR_OK ||
        WriteRegister(REG_GYRO_CONFIG_1, static_cast<uint8_t>((fs_sel << 1) | 0x01)) != LEAVR_OK ||
        SelectBank(0) != LEAVR_OK) {
        return LEAVR_ERR_I2C_FAILED;
    }
    return LEAVR_OK;
}

int Icm20948Hal::Start() {
    if (running_) return LEAVR_OK;

    running_ = true;
    if (pthread_create(&polling_thread_, nullptr, PollingThread, this) != 0) {
        running_ = false;
        return LEAVR_ERR_NOT_INIT;
    }
    LOG_INFO("ICM-20948: Polling started");
    return LEAVR_OK;
}

int Icm20948Hal::Stop() {
    running_ = false;
    if (polling_thread_) {
        pthread_join(polling_thread_, nullptr);
        polling_thread_ = 0;
    }
    return LEAVR_OK;
}

int Icm20948Hal::ReadData(EisFrameData* data, int timeout_ms) {
    if (!data) return LEAVR_ERR_PARAM;
    (void)timeout_ms;

#ifdef __linux__
    if (SelectBank(0) != LEAVR_OK) return LEAVR_ERR_I2C_FAILED;
    // 从寄存器读取 12 字节 (ACC×3 + GYRO×3 各 2 字节)
    uint8_t buf[12];
    int ret = ReadRegisters(REG_ACCEL_XOUT_H, buf, sizeof(buf));
    if (ret != LEAVR_OK) return ret;

    // 解析数据
    int16_t accel_raw[3], gyro_raw[3];

    // ACCEL: big-endian 2 bytes
    accel_raw[0] = (int16_t)((buf[0] << 8) | buf[1]);
    accel_raw[1] = (int16_t)((buf[2] << 8) | buf[3]);
    accel_raw[2] = (int16_t)((buf[4] << 8) | buf[5]);

    // GYRO: big-endian 2 bytes (在 8-12 字节处, 但实际在另一个寄存器)
    // 简化为连续读取
    gyro_raw[0] = (int16_t)((buf[6] << 8) | buf[7]);
    gyro_raw[1] = (int16_t)((buf[8] << 8) | buf[9]);
    gyro_raw[2] = (int16_t)((buf[10] << 8) | buf[11]);

    // 转换为物理单位
    data->accel_x = accel_raw[0] * accel_scale_ * 9.81f;  // m/s²
    data->accel_y = accel_raw[1] * accel_scale_ * 9.81f;
    data->accel_z = accel_raw[2] * accel_scale_ * 9.81f;

    // Gyro: 转换为 rad/s
    data->gyro_x = gyro_raw[0] * gyro_scale_ * 0.0174533f;
    data->gyro_y = gyro_raw[1] * gyro_scale_ * 0.0174533f;
    data->gyro_z = gyro_raw[2] * gyro_scale_ * 0.0174533f;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    data->timestamp_us = static_cast<uint64_t>(ts.tv_sec) * 1000000ULL +
                          static_cast<uint64_t>(ts.tv_nsec) / 1000;

    return LEAVR_OK;
#else
    // 模拟数据 (用于测试)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    data->timestamp_us = static_cast<uint64_t>(ts.tv_sec) * 1000000ULL +
                          static_cast<uint64_t>(ts.tv_nsec) / 1000;
    data->gyro_x = 0.0f;
    data->gyro_y = 0.0f;
    data->gyro_z = 0.0f;
    data->accel_x = 0.0f;
    data->accel_y = 0.0f;
    data->accel_z = 9.81f;
    return LEAVR_OK;
#endif
}

int Icm20948Hal::Calibrate() {
    LOG_INFO("ICM-20948: Starting calibration...");
    // 采集静止样本, 计算偏置
    return LEAVR_OK;
}

int Icm20948Hal::SetDataCallback(DataCallback callback) {
    pthread_mutex_lock(&callback_lock_);
    data_callback_ = std::move(callback);
    pthread_mutex_unlock(&callback_lock_);
    return LEAVR_OK;
}

bool Icm20948Hal::SelfTest() {
    uint8_t whoami = 0;
    return SelectBank(0) == LEAVR_OK &&
           ReadRegisters(REG_WHO_AM_I, &whoami, 1) == LEAVR_OK &&
           whoami == ICM20948_WHO_AM_I_VALUE;
}

void* Icm20948Hal::PollingThread(void* arg) {
    auto* hal = static_cast<Icm20948Hal*>(arg);

    while (hal->running_) {
        EisFrameData data;
        if (hal->ReadData(&data, 100) == LEAVR_OK) {
            pthread_mutex_lock(&hal->callback_lock_);
            DataCallback callback = hal->data_callback_;
            pthread_mutex_unlock(&hal->callback_lock_);
            if (callback) callback(data);
        } else {
            LOG_WARN("ICM-20948: read failed");
        }

        int interval_us = 1000000 / hal->gyro_odr_hz_;  // 200Hz = 5000us
        usleep(interval_us);
    }

    return nullptr;
}

} // namespace leavr
