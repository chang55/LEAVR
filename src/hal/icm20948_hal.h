/**
 * @file icm20948_hal.h
 * @brief ICM-20948 9轴陀螺仪 HAL 驱动 (I2C)
 */

#ifndef LEAVR_HAL_ICM20948_H
#define LEAVR_HAL_ICM20948_H

#include "leavr_interfaces.h"
#include <pthread.h>

namespace leavr {

class Icm20948Hal : public IIcm20948Hal {
public:
    Icm20948Hal() = default;
    ~Icm20948Hal() override;

    int Init(const char* i2c_dev, uint8_t addr) override;
    int SetOdr(uint16_t acc_odr_hz, uint16_t gyro_odr_hz) override;
    int SetAccelRange(int g) override;
    int SetGyroRange(int dps) override;
    int Start() override;
    int Stop() override;
    int ReadData(EisFrameData* data, int timeout_ms) override;
    int Calibrate() override;
    bool SelfTest() override;

private:
    static void* PollingThread(void* arg);

    int WriteRegister(uint8_t reg, uint8_t value);
    int ReadRegisters(uint8_t reg, uint8_t* buf, size_t len);

    int i2c_fd_ = -1;
    uint8_t addr_ = 0x68;
    uint16_t acc_odr_hz_ = 200;
    uint16_t gyro_odr_hz_ = 200;
    int gyro_range_dps_ = 2000;
    int accel_range_g_ = 16;
    double gyro_scale_ = 0.0;
    double accel_scale_ = 0.0;

    pthread_t polling_thread_ = 0;
    volatile bool running_ = false;

    // TODO: 在实际海思平台上实现 I2C 读写
    // 对应 /dev/i2c-1 设备操作
};

} // namespace leavr

#endif // LEAVR_HAL_ICM20948_H