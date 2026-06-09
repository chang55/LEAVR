#ifndef LEAVR_STORAGE_RAW_VIDEO_RECORDER_H
#define LEAVR_STORAGE_RAW_VIDEO_RECORDER_H

#include "leavr_interfaces.h"
#include <memory>

namespace leavr {

/*
 * 无 FFmpeg 时使用的原型录像后端。它保存 Annex-B H.264/H.265 裸流，
 * 仍采用 .tmp + fsync + rename 保证正常停止时文件原子落盘。
 */
class RawVideoRecorder : public IRecorder {
public:
    RawVideoRecorder();
    ~RawVideoRecorder() override;

    int StartRecording(const char* path, const StreamProfile& profile) override;
    int StopRecording() override;
    bool IsRecording() const override;
    uint64_t GetBytesWritten() const override;
    void OnVideoFrame(const EncodedVideoFrame& frame) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace leavr

#endif
