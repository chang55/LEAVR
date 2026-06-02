/**
 * @file eis_processor.cpp
 * @brief EIS 防抖处理器实现
 */

#include "media/eis/eis_processor.h"
#include "utils/logger.h"
#include <unistd.h>
#include <algorithm>

namespace leavr {

// ================================================================
// KalmanFilter1D 实现
// ================================================================

KalmanFilter1D::KalmanFilter1D(double process_noise, double measure_noise)
    : x_(0), bias_(0),
      q_angle_(process_noise), q_bias_(process_noise * 0.01), r_measure_(measure_noise) {
    p_[0] = p_[3] = 1.0;
    p_[1] = p_[2] = 0.0;
}

void KalmanFilter1D::Reset(double initial_angle) {
    x_ = initial_angle;
    bias_ = 0.0;
    p_[0] = p_[3] = 1.0;
    p_[1] = p_[2] = 0.0;
}

double KalmanFilter1D::Update(double measurement, double dt) {
    // 预测
    // x = x + (measurement - bias) * dt  [简化的积分预测]
    double rate = measurement - bias_;
    x_ += rate * dt;

    // P = F * P * F^T + Q
    // F = [1, -dt; 0, 1]
    p_[0] += dt * (dt * p_[3] - p_[1] - p_[2] + q_angle_);
    p_[1] -= dt * p_[3];
    p_[2] -= dt * p_[3];
    p_[3] += q_bias_ * dt;

    // 更新 (观测 = 积分角度)
    double innov = x_ - 0;  // 这里用积分值作为观测
    double s = p_[0] + r_measure_;
    double k_gain = p_[0] / s;

    x_ -= k_gain * innov;
    bias_ -= k_gain * innov * 0.1;

    // 更新协方差
    double p00_temp = p_[0];
    double p01_temp = p_[1];
    p_[0] -= k_gain * p00_temp;
    p_[1] -= k_gain * p01_temp;
    p_[2] -= k_gain * p_[1];
    p_[3] -= k_gain * p_[3];

    return x_;
}

// ================================================================
// EisProcessor 实现
// ================================================================

EisProcessor::EisProcessor() {}

EisProcessor::~EisProcessor() {
    Stop();
}

int EisProcessor::Init(int input_w, int input_h, int output_w, int output_h) {
    input_w_ = input_w;
    input_h_ = input_h;
    output_w_ = output_w;
    output_h_ = output_h;

    // 裁剪边距 = (输入 - 输出) / 2
    crop_margin_ = std::min((input_w_ - output_w_) / 2, (input_h_ - output_h_) / 2);
    if (crop_margin_ < 0) crop_margin_ = 0;

    // 初始裁剪窗口居中
    crop_window_.x = (input_w_ - output_w_) / 2;
    crop_window_.y = (input_h_ - output_h_) / 2;
    crop_window_.width = output_w_;
    crop_window_.height = output_h_;

    // 焦距估算 (像素)
    focal_length_px_ = static_cast<double>(input_w_) * 1.2;

    LOG_INFO("EIS: Init input=%dx%d output=%dx%d margin=%d focal=%.0f",
             input_w_, input_h_, output_w_, output_h_, crop_margin_, focal_length_px_);
    return LEAVR_OK;
}

int EisProcessor::PushGyroData(const EisFrameData& data) {
    if (!running_) return LEAVR_ERR_STATE;

    // 校准期间收集静止样本
    if (!calibrated_) {
        bias_x_ += data.gyro_x;
        bias_y_ += data.gyro_y;
        calibration_samples_++;
        if (calibration_samples_ >= kCalibrationSamples) {
            bias_x_ /= calibration_samples_;
            bias_y_ /= calibration_samples_;
            calibrated_ = true;
            LOG_INFO("EIS: Calibration done, bias_x=%.6f bias_y=%.6f",
                     bias_x_, bias_y_);
        }
        return LEAVR_OK;
    }

    return gyro_buf_.Push(data) ? LEAVR_OK : LEAVR_ERR_BUSY;
}

EisCropWindow EisProcessor::GetCropWindow() {
    pthread_mutex_lock(&crop_lock_);
    EisCropWindow win = crop_window_;
    pthread_mutex_unlock(&crop_lock_);
    return win;
}

int EisProcessor::Start() {
    if (running_) return LEAVR_OK;

    // 重置滤波器
    kf_x_.Reset(0.0);
    kf_y_.Reset(0.0);

    // 重置校准
    bias_x_ = 0.0;
    bias_y_ = 0.0;
    calibration_samples_ = 0;
    calibrated_ = false;

    gyro_buf_.Clear();

    running_ = true;
    if (pthread_create(&thread_, nullptr, ThreadEntry, this) != 0) {
        running_ = false;
        LOG_ERROR("EIS: Failed to create processing thread");
        return LEAVR_ERR_EIS_INIT;
    }

    // 设置线程优先级
    struct sched_param param;
    param.sched_priority = 95;
    pthread_setschedparam(thread_, SCHED_FIFO, &param);

    LOG_INFO("EIS: Processor started");
    return LEAVR_OK;
}

int EisProcessor::Stop() {
    running_ = false;
    if (thread_) {
        pthread_join(thread_, nullptr);
        thread_ = 0;
    }
    gyro_buf_.Clear();
    LOG_INFO("EIS: Processor stopped");
    return LEAVR_OK;
}

int EisProcessor::SetStrength(int level) {
    if (level < 1) level = 1;
    if (level > 100) level = 100;
    strength_ = static_cast<double>(level) / 100.0;
    LOG_DEBUG("EIS: Strength set to %d (%.2f)", level, strength_);
    return LEAVR_OK;
}

int EisProcessor::Calibrate() {
    // 重新校准
    bias_x_ = 0.0;
    bias_y_ = 0.0;
    calibration_samples_ = 0;
    calibrated_ = false;
    LOG_INFO("EIS: Recalibration started");
    return LEAVR_OK;
}

void EisProcessor::AngleToPixel(double angle_x, double angle_y, int* dx, int* dy) {
    // dx = f * tan(angle) * strength
    *dx = static_cast<int>(focal_length_px_ * tan(angle_y) * strength_);
    *dy = static_cast<int>(focal_length_px_ * tan(angle_x) * strength_);
}

void EisProcessor::ClampCropWindow(int dx, int dy) {
    int cx = (input_w_ - output_w_) / 2 - dx;
    int cy = (input_h_ - output_h_) / 2 - dy;

    // 范围限制
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx + output_w_ > input_w_) cx = input_w_ - output_w_;
    if (cy + output_h_ > input_h_) cy = input_h_ - output_h_;

    // 平滑更新 (低通滤波减少裁剪窗口抖动)
    pthread_mutex_lock(&crop_lock_);
    crop_window_.x = crop_window_.x * 0.7 + cx * 0.3;
    crop_window_.y = crop_window_.y * 0.7 + cy * 0.3;
    crop_window_.width = output_w_;
    crop_window_.height = output_h_;
    pthread_mutex_unlock(&crop_lock_);
}

void* EisProcessor::ThreadEntry(void* arg) {
    static_cast<EisProcessor*>(arg)->ProcessorThread();
    return nullptr;
}

void EisProcessor::ProcessorThread() {
    EisFrameData frame_data;
    double angle_x = 0.0, angle_y = 0.0;
    int dx = 0, dy = 0;

    while (running_) {
        // 从缓冲区获取陀螺仪数据
        if (gyro_buf_.Pop(frame_data)) {
            // 去除偏置
            double gx = frame_data.gyro_x - bias_x_;
            double gy = frame_data.gyro_y - bias_y_;

            // 卡尔曼滤波
            double dt = 0.005;  // 200Hz ODR → 5ms
            angle_x = kf_x_.Update(gx, dt);
            angle_y = kf_y_.Update(gy, dt);

            last_angle_x_ = angle_x;
            last_angle_y_ = angle_y;

            // 角度 → 像素偏移
            AngleToPixel(angle_x, angle_y, &dx, &dy);

            // 更新裁剪窗口
            ClampCropWindow(dx, dy);
        } else {
            // 无数据时短暂休眠
            usleep(500);  // 500us
        }
    }
}

void EisProcessor::GetDebugInfo(float* angle_x, float* angle_y,
                                 int* crop_dx, int* crop_dy) {
    if (angle_x) *angle_x = static_cast<float>(last_angle_x_);
    if (angle_y) *angle_y = static_cast<float>(last_angle_y_);
    if (crop_dx) *crop_dx = crop_window_.x - (input_w_ - output_w_) / 2;
    if (crop_dy) *crop_dy = crop_window_.y - (input_h_ - output_h_) / 2;
}

} // namespace leavr