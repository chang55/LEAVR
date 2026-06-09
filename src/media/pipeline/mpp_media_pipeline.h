#ifndef LEAVR_MEDIA_PIPELINE_MPP_MEDIA_PIPELINE_H
#define LEAVR_MEDIA_PIPELINE_MPP_MEDIA_PIPELINE_H

#include "leavr_interfaces.h"
#include <memory>

namespace leavr {

class MppMediaPipeline : public IMediaPipeline {
public:
    MppMediaPipeline();
    ~MppMediaPipeline() override;

    int Init(const MediaPipelineConfig& cfg) override;
    int Start() override;
    int Pause() override;
    int Resume() override;
    int Stop() override;

    int RegisterSink(IVideoFrameSink* sink) override;
    int UnregisterSink(IVideoFrameSink* sink) override;
    int SetCropWindow(const EisCropWindow& window) override;
    int RequestIdr(VideoStreamId stream_id) override;

    int SetOsd(const OsdConfig& cfg) override;
    int CaptureJpeg(const char* path, const JpegConfig& cfg) override;
    float GetActualFps() override;
    int GetBitrate() override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

MediaPipelineConfig MakeDefaultSc4336PipelineConfig();

} // namespace leavr

#endif
