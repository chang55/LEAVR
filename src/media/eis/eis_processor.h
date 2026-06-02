/**
 * @file eis_processor.h
 * @brief EIS 电子防抖处理器 - 卡尔曼滤波 + VPSS 裁剪稳像
 *        ICM-20948 陀螺仪角速度 → 姿态估计 → 像素偏移 → 裁剪窗口
 */

#ifndef LEAVR_MEDIA_EIS_PROCESSOR_H
#define LEAVR_MEDIA_EIS_PROCESSOR_H

#include "leavr_interfaces.h"
#include "utils/ring_buffer.h"
#include <pthread.h>
#include <atomic>
#include <deque>
#include <cmath>

namespace leavr {

/**
 * @brief 一维卡尔曼滤波器 (用于角度估计)
 */
class KalmanFilter1D {
public:
    KalmanFilter1D(double process_noise = 0.001, double measure_noise = 0.1);

    void Reset(double initial_angle = 0.0);
    double Update(double measurement, double dt);

private:
    double x_;       // 状态: [angle, bias]
    double bias_;
    double p_[4];    // 协方差矩阵 [2x2] 扁平化存储
    double q_angle_;
    double q_bias_;
    double r_measure_;
};

/**
 * @brief EIS 防抖处理器完整实现
 */
class EisProcessor : public IEisProcessor {
public:
    EisProcessor();
    ~EisProcessor() override;

    int Init(int input_w, int input_h, int output_w, int output_h) override;
    int PushGyroData(const EisFrameData& data) override;
    EisCropWindow GetCropWindow() override;
    int Start() override;
    int Stop() override;
    int SetStrength(int level) override;  // 1-100
    int Calibrate() override;

    /** 获取当前 EIS 状态 (供调试用) */
    void GetDebugInfo(float* angle_x, float* angle_y, int* crop_dx, int* crop_dy);

private:
    /** 处理线程 */
    void ProcessorThread();
    static void* ThreadEntry(void* arg);

    /** 将角度偏移转换为像素偏移 */
    void AngleToPixel(double angle_x, double angle_y, int* dx, int* dy);

    /** 限制裁剪窗口在有效范围 */
    void ClampCropWindow(int dx, int dy);

    // 参数
    int input_w_ = 1920;
    int input_h_ = 1080;
    int output_w_ = 1920;
    int output_h_ = 1080;
    int crop_margin_ = 80;     // 最大裁剪边距 (像素)
    double strength_ = 0.5;    // 防抖强度 0.0-1.0
    double focal_length_px_ = 2000.0;  // 焦距 (像素单位估算)

    // 卡尔曼滤波器
    KalmanFilter1D kf_x_;
    KalmanFilter1D kf_y_;

    // 陀螺仪数据缓冲
    RingBuffer<EisFrameData, 256> gyro_buf_;

    // 当前裁剪窗口
    EisCropWindow crop_window_;
    pthread_mutex_t crop_lock_ = PTHREAD_MUTEX_INITIALIZER;

    // 校准
    double bias_x_ = 0.0;
    double bias_y_ = 0.0;
    int calibration_samples_ = 0;
    static constexpr int kCalibrationSamples = 200;

    // 线程
    pthread_t thread_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<bool> calibrated_{false};

    // 调试
    double last_angle_x_ = 0.0;
    double last_angle_y_ = 0.0;
};

} // namespace leavr

#endif // LEAVR_MEDIA_EIS_PROCESSOR_H