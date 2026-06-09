#include "media/pipeline/mpp_media_pipeline.h"
#include "utils/logger.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <pthread.h>
#include <sys/select.h>
#include <unistd.h>

extern "C" {
#include "sample_comm.h"
#include "ot_buffer.h"
#include "ss_mpi_venc.h"
#include "ss_mpi_vpss.h"
}

namespace leavr {
namespace {

constexpr int kVpssGroup = 0;
constexpr int kMainChannel = 0;
constexpr int kSubChannel = 1;

ot_pic_size ToPicSize(int width, int height) {
    if (width == 1920 && height == 1080) return PIC_1080P;
    if (width == 1280 && height == 720) return PIC_720P;
    if (width == 2560 && height == 1440) return PIC_2560X1440;
    if (width == 2304 && height == 1296) return PIC_2304X1296;
    return PIC_BUTT;
}

ot_payload_type ToPayload(PayloadType codec) {
    return codec == PAYLOAD_H264 ? OT_PT_H264 : OT_PT_H265;
}

void AddVbPool(ot_vb_cfg* cfg, int index, int width, int height,
               ot_pixel_format pixel_format, ot_compress_mode compress_mode,
               td_u32 count) {
    ot_pic_buf_attr attr = {};
    attr.width = width;
    attr.height = height;
    attr.align = OT_DEFAULT_ALIGN;
    attr.bit_width = OT_DATA_BIT_WIDTH_8;
    attr.pixel_format = pixel_format;
    attr.compress_mode = compress_mode;
    attr.video_format = OT_VIDEO_FORMAT_LINEAR;
    cfg->common_pool[index].blk_size = ot_common_get_pic_buf_size(&attr);
    cfg->common_pool[index].blk_cnt = count;
}

bool IsKeyFrame(PayloadType codec, const ot_venc_stream& stream) {
    for (td_u32 i = 0; i < stream.pack_cnt; ++i) {
        if (codec == PAYLOAD_H264 &&
            stream.pack[i].data_type.h264_type == OT_VENC_H264_NALU_IDR_SLICE) {
            return true;
        }
        if (codec == PAYLOAD_H265 &&
            stream.pack[i].data_type.h265_type == OT_VENC_H265_NALU_IDR_SLICE) {
            return true;
        }
    }
    return false;
}

bool HasParameterSets(PayloadType codec, const ot_venc_stream& stream) {
    for (td_u32 i = 0; i < stream.pack_cnt; ++i) {
        if (codec == PAYLOAD_H264) {
            const auto type = stream.pack[i].data_type.h264_type;
            if (type == OT_VENC_H264_NALU_SPS || type == OT_VENC_H264_NALU_PPS) return true;
        } else {
            const auto type = stream.pack[i].data_type.h265_type;
            if (type == OT_VENC_H265_NALU_VPS || type == OT_VENC_H265_NALU_SPS ||
                type == OT_VENC_H265_NALU_PPS) return true;
        }
    }
    return false;
}

} // namespace

class MppMediaPipeline::Impl {
public:
    int Init(const MediaPipelineConfig& cfg);
    int Start();
    int Stop();
    int RegisterSink(IVideoFrameSink* sink);
    int UnregisterSink(IVideoFrameSink* sink);
    int SetCropWindow(const EisCropWindow& window);
    int RequestIdr(VideoStreamId stream_id);

    static void* StreamThreadEntry(void* arg);
    void StreamThread();
    void DispatchStream(int channel, const StreamProfile& profile, uint32_t* sequence);
    int StartSystemAndVi();
    int StartVpss();
    int StartVenc(const StreamProfile& profile);
    void StopVenc();
    void StopVpssAndVi();
    int ApplyBitrate(const StreamProfile& profile);

    MediaPipelineConfig config_ = {};
    sample_vi_cfg vi_cfg_ = {};
    td_bool vpss_enabled_[OT_VPSS_MAX_PHYS_CHN_NUM] = {};
    std::vector<IVideoFrameSink*> sinks_;
    pthread_mutex_t sinks_lock_ = PTHREAD_MUTEX_INITIALIZER;
    pthread_t stream_thread_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> paused_{false};
    bool sys_started_ = false;
    bool vi_started_ = false;
    bool vpss_started_ = false;
    bool venc_started_[2] = {false, false};
    uint64_t frames_[2] = {0, 0};
    uint64_t bytes_[2] = {0, 0};
    uint64_t start_pts_us_ = 0;
    uint64_t last_main_pts_us_ = 0;
};

int MppMediaPipeline::Impl::Init(const MediaPipelineConfig& cfg) {
    if (running_) return LEAVR_ERR_BUSY;
    if (cfg.sensor.type != SENSOR_TYPE_SC4336) {
        LOG_ERROR("Media: sensor %s has no board implementation yet", cfg.sensor.name);
        return LEAVR_ERR_NOT_SUPPORTED;
    }
    if (cfg.main_stream.codec != PAYLOAD_H265 || cfg.sub_stream.codec != PAYLOAD_H264) {
        return LEAVR_ERR_PARAM;
    }
    config_ = cfg;
    initialized_ = true;
    LOG_INFO("Media: configured %s main=%dx%d H265@%dfps sub=%dx%d H264@%dfps",
             config_.sensor.name,
             config_.main_stream.width, config_.main_stream.height, config_.main_stream.fps,
             config_.sub_stream.width, config_.sub_stream.height, config_.sub_stream.fps);
    return LEAVR_OK;
}

int MppMediaPipeline::Impl::StartSystemAndVi() {
    ot_vb_cfg vb_cfg = {};
    vb_cfg.max_pool_cnt = 3;
    AddVbPool(&vb_cfg, 0, config_.sensor.native_width, config_.sensor.native_height,
              OT_PIXEL_FORMAT_YUV_SEMIPLANAR_422, OT_COMPRESS_MODE_NONE, 6);
    AddVbPool(&vb_cfg, 1, config_.main_stream.width, config_.main_stream.height,
              OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420, OT_COMPRESS_MODE_SEG_COMPACT, 8);
    AddVbPool(&vb_cfg, 2, config_.sub_stream.width, config_.sub_stream.height,
              OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420, OT_COMPRESS_MODE_NONE, 6);

    int ret = sample_comm_sys_init_with_vb_supplement(
        &vb_cfg, OT_VB_SUPPLEMENT_JPEG_MASK | OT_VB_SUPPLEMENT_BNR_MOT_MASK);
    if (ret != TD_SUCCESS) return LEAVR_ERR_VI_INIT;
    sys_started_ = true;

    ret = sample_comm_vi_set_vi_vpss_mode(OT_VI_OFFLINE_VPSS_OFFLINE,
                                          OT_VI_AIISP_MODE_DEFAULT);
    if (ret != TD_SUCCESS) return LEAVR_ERR_VI_INIT;

    sample_comm_vi_get_default_vi_cfg(SC4336P_MIPI_4M_30FPS_10BIT, &vi_cfg_);
    ret = sample_comm_vi_start_vi(&vi_cfg_);
    if (ret != TD_SUCCESS) return LEAVR_ERR_VI_INIT;
    vi_started_ = true;
    return LEAVR_OK;
}

int MppMediaPipeline::Impl::StartVpss() {
    ot_vpss_grp_attr grp_attr = {};
    sample_comm_vpss_get_default_grp_attr(&grp_attr);
    grp_attr.max_width = config_.sensor.native_width;
    grp_attr.max_height = config_.sensor.native_height;
    grp_attr.frame_rate.src_frame_rate = -1;
    grp_attr.frame_rate.dst_frame_rate = -1;

    sample_vpss_chn_attr attr = {};
    const StreamProfile profiles[2] = {config_.main_stream, config_.sub_stream};
    for (int i = 0; i < 2; ++i) {
        vpss_enabled_[i] = TD_TRUE;
        attr.chn_enable[i] = TD_TRUE;
        sample_comm_vpss_get_default_chn_attr(&attr.chn_attr[i]);
        attr.chn_attr[i].width = profiles[i].width;
        attr.chn_attr[i].height = profiles[i].height;
        attr.chn_attr[i].chn_mode = OT_VPSS_CHN_MODE_USER;
        attr.chn_attr[i].compress_mode =
            i == 0 ? OT_COMPRESS_MODE_SEG_COMPACT : OT_COMPRESS_MODE_NONE;
        attr.chn_attr[i].frame_rate.src_frame_rate = config_.sensor.max_fps;
        attr.chn_attr[i].frame_rate.dst_frame_rate = profiles[i].fps;
    }
    attr.chn_array_size = OT_VPSS_MAX_PHYS_CHN_NUM;

    int ret = sample_common_vpss_start(kVpssGroup, &grp_attr, &attr);
    if (ret != TD_SUCCESS) return LEAVR_ERR_VPSS_INIT;
    vpss_started_ = true;

    ret = sample_comm_vi_bind_vpss(0, 0, kVpssGroup, 0);
    return ret == TD_SUCCESS ? LEAVR_OK : LEAVR_ERR_VPSS_INIT;
}

int MppMediaPipeline::Impl::ApplyBitrate(const StreamProfile& profile) {
    ot_venc_chn_attr attr = {};
    int ret = ss_mpi_venc_get_chn_attr(profile.channel_id, &attr);
    if (ret != TD_SUCCESS) return LEAVR_ERR_VENC_INIT;
    if (profile.codec == PAYLOAD_H264 &&
        attr.rc_attr.rc_mode == OT_VENC_RC_MODE_H264_CBR) {
        attr.rc_attr.h264_cbr.bit_rate = profile.bitrate;
        attr.rc_attr.h264_cbr.dst_frame_rate = profile.fps;
    } else if (profile.codec == PAYLOAD_H265 &&
               attr.rc_attr.rc_mode == OT_VENC_RC_MODE_H265_CBR) {
        attr.rc_attr.h265_cbr.bit_rate = profile.bitrate;
        attr.rc_attr.h265_cbr.dst_frame_rate = profile.fps;
    }
    ret = ss_mpi_venc_set_chn_attr(profile.channel_id, &attr);
    return ret == TD_SUCCESS ? LEAVR_OK : LEAVR_ERR_VENC_INIT;
}

int MppMediaPipeline::Impl::StartVenc(const StreamProfile& profile) {
    sample_comm_venc_chn_param param = {};
    param.frame_rate = profile.fps;
    param.gop = profile.gop;
    param.stats_time = 1;
    param.venc_size.width = profile.width;
    param.venc_size.height = profile.height;
    param.size = ToPicSize(profile.width, profile.height);
    if (param.size == PIC_BUTT) return LEAVR_ERR_PARAM;
    param.profile = 0;
    param.is_rcn_ref_share_buf = TD_TRUE;
    param.type = ToPayload(profile.codec);
    param.rc_mode = SAMPLE_RC_CBR;
    if (sample_comm_venc_get_gop_attr(OT_VENC_GOP_MODE_NORMAL_P, &param.gop_attr) != TD_SUCCESS) {
        return LEAVR_ERR_VENC_INIT;
    }
    if (sample_comm_venc_start(profile.channel_id, &param) != TD_SUCCESS) {
        return LEAVR_ERR_VENC_INIT;
    }
    venc_started_[profile.channel_id] = true;
    if (ApplyBitrate(profile) != LEAVR_OK) return LEAVR_ERR_VENC_INIT;
    if (sample_comm_vpss_bind_venc(kVpssGroup, profile.channel_id, profile.channel_id) != TD_SUCCESS) {
        return LEAVR_ERR_VENC_INIT;
    }
    return LEAVR_OK;
}

int MppMediaPipeline::Impl::Start() {
    if (!initialized_) return LEAVR_ERR_NOT_INIT;
    if (running_) return LEAVR_OK;

    int ret = StartSystemAndVi();
    if (ret == LEAVR_OK) ret = StartVpss();
    if (ret == LEAVR_OK) ret = StartVenc(config_.main_stream);
    if (ret == LEAVR_OK) ret = StartVenc(config_.sub_stream);
    if (ret != LEAVR_OK) {
        Stop();
        return ret;
    }

    running_ = true;
    paused_ = false;
    if (pthread_create(&stream_thread_, nullptr, StreamThreadEntry, this) != 0) {
        running_ = false;
        Stop();
        return LEAVR_ERR_VENC_STREAM;
    }
    LOG_INFO("Media: SC4336 dual-stream pipeline started");
    return LEAVR_OK;
}

void MppMediaPipeline::Impl::StopVenc() {
    for (int i = 1; i >= 0; --i) {
        if (!venc_started_[i]) continue;
        sample_comm_vpss_un_bind_venc(kVpssGroup, i, i);
        sample_comm_venc_stop(i);
        venc_started_[i] = false;
    }
}

void MppMediaPipeline::Impl::StopVpssAndVi() {
    if (vpss_started_) {
        sample_comm_vi_un_bind_vpss(0, 0, kVpssGroup, 0);
        sample_common_vpss_stop(kVpssGroup, vpss_enabled_, OT_VPSS_MAX_PHYS_CHN_NUM);
        vpss_started_ = false;
    }
    if (vi_started_) {
        sample_comm_vi_stop_vi(&vi_cfg_);
        vi_started_ = false;
    }
    if (sys_started_) {
        sample_comm_sys_exit();
        sys_started_ = false;
    }
}

int MppMediaPipeline::Impl::Stop() {
    running_ = false;
    if (stream_thread_) {
        pthread_join(stream_thread_, nullptr);
        stream_thread_ = 0;
    }
    StopVenc();
    StopVpssAndVi();
    paused_ = false;
    LOG_INFO("Media: pipeline stopped");
    return LEAVR_OK;
}

int MppMediaPipeline::Impl::RegisterSink(IVideoFrameSink* sink) {
    if (!sink) return LEAVR_ERR_PARAM;
    pthread_mutex_lock(&sinks_lock_);
    if (std::find(sinks_.begin(), sinks_.end(), sink) == sinks_.end()) sinks_.push_back(sink);
    pthread_mutex_unlock(&sinks_lock_);
    return LEAVR_OK;
}

int MppMediaPipeline::Impl::UnregisterSink(IVideoFrameSink* sink) {
    pthread_mutex_lock(&sinks_lock_);
    sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end());
    pthread_mutex_unlock(&sinks_lock_);
    return LEAVR_OK;
}

int MppMediaPipeline::Impl::SetCropWindow(const EisCropWindow& window) {
    if (!vpss_started_) return LEAVR_ERR_NOT_INIT;
    if (window.x < 0 || window.y < 0 || window.width <= 0 || window.height <= 0 ||
        window.x + window.width > config_.sensor.native_width ||
        window.y + window.height > config_.sensor.native_height) {
        return LEAVR_ERR_PARAM;
    }
    ot_vpss_crop_info crop = {};
    crop.enable = TD_TRUE;
    crop.crop_mode = OT_COORD_ABS;
    crop.crop_rect.x = window.x;
    crop.crop_rect.y = window.y;
    crop.crop_rect.width = window.width;
    crop.crop_rect.height = window.height;
    return ss_mpi_vpss_set_grp_crop(kVpssGroup, &crop) == TD_SUCCESS
               ? LEAVR_OK : LEAVR_ERR_VPSS_CROP;
}

int MppMediaPipeline::Impl::RequestIdr(VideoStreamId stream_id) {
    const int channel = stream_id == VIDEO_STREAM_MAIN
                            ? config_.main_stream.channel_id : config_.sub_stream.channel_id;
    return ss_mpi_venc_request_idr(channel, TD_TRUE) == TD_SUCCESS
               ? LEAVR_OK : LEAVR_ERR_VENC_STREAM;
}

void* MppMediaPipeline::Impl::StreamThreadEntry(void* arg) {
    static_cast<Impl*>(arg)->StreamThread();
    return nullptr;
}

void MppMediaPipeline::Impl::DispatchStream(int channel, const StreamProfile& profile,
                                            uint32_t* sequence) {
    ot_venc_chn_status status = {};
    if (ss_mpi_venc_query_status(channel, &status) != TD_SUCCESS || status.cur_packs == 0) return;
    std::vector<ot_venc_pack> packs(status.cur_packs);
    ot_venc_stream stream = {};
    stream.pack = packs.data();
    stream.pack_cnt = status.cur_packs;
    if (ss_mpi_venc_get_stream(channel, &stream, 0) != TD_SUCCESS) return;

    size_t total = 0;
    for (td_u32 i = 0; i < stream.pack_cnt; ++i) total += stream.pack[i].len - stream.pack[i].offset;
    std::vector<uint8_t> data(total);
    size_t offset = 0;
    for (td_u32 i = 0; i < stream.pack_cnt; ++i) {
        const size_t len = stream.pack[i].len - stream.pack[i].offset;
        memcpy(data.data() + offset, stream.pack[i].addr + stream.pack[i].offset, len);
        offset += len;
    }

    EncodedVideoFrame frame = {};
    frame.stream_id = profile.stream_id;
    frame.codec = profile.codec;
    frame.data = data.data();
    frame.size = data.size();
    frame.pts_us = stream.pack_cnt ? stream.pack[0].pts : 0;
    frame.sequence = (*sequence)++;
    frame.is_key_frame = IsKeyFrame(profile.codec, stream);
    frame.has_parameter_sets = HasParameterSets(profile.codec, stream);

    pthread_mutex_lock(&sinks_lock_);
    const auto sinks = sinks_;
    pthread_mutex_unlock(&sinks_lock_);
    for (auto* sink : sinks) sink->OnVideoFrame(frame);

    ++frames_[channel];
    bytes_[channel] += data.size();
    if (channel == config_.main_stream.channel_id) {
        if (start_pts_us_ == 0) start_pts_us_ = frame.pts_us;
        last_main_pts_us_ = frame.pts_us;
    }
    ss_mpi_venc_release_stream(channel, &stream);
}

void MppMediaPipeline::Impl::StreamThread() {
    const int main_fd = ss_mpi_venc_get_fd(config_.main_stream.channel_id);
    const int sub_fd = ss_mpi_venc_get_fd(config_.sub_stream.channel_id);
    uint32_t sequence[2] = {0, 0};
    while (running_) {
        if (paused_) {
            usleep(10000);
            continue;
        }
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(main_fd, &read_fds);
        FD_SET(sub_fd, &read_fds);
        timeval timeout = {0, 200000};
        const int ret = select(std::max(main_fd, sub_fd) + 1, &read_fds, nullptr, nullptr, &timeout);
        if (ret <= 0) continue;
        if (FD_ISSET(main_fd, &read_fds)) {
            DispatchStream(config_.main_stream.channel_id, config_.main_stream, &sequence[0]);
        }
        if (FD_ISSET(sub_fd, &read_fds)) {
            DispatchStream(config_.sub_stream.channel_id, config_.sub_stream, &sequence[1]);
        }
    }
}

MppMediaPipeline::MppMediaPipeline() : impl_(new Impl) {}
MppMediaPipeline::~MppMediaPipeline() = default;
int MppMediaPipeline::Init(const MediaPipelineConfig& cfg) { return impl_->Init(cfg); }
int MppMediaPipeline::Start() { return impl_->Start(); }
int MppMediaPipeline::Pause() { impl_->paused_ = true; return LEAVR_OK; }
int MppMediaPipeline::Resume() { impl_->paused_ = false; return LEAVR_OK; }
int MppMediaPipeline::Stop() { return impl_->Stop(); }
int MppMediaPipeline::RegisterSink(IVideoFrameSink* sink) { return impl_->RegisterSink(sink); }
int MppMediaPipeline::UnregisterSink(IVideoFrameSink* sink) { return impl_->UnregisterSink(sink); }
int MppMediaPipeline::SetCropWindow(const EisCropWindow& window) { return impl_->SetCropWindow(window); }
int MppMediaPipeline::RequestIdr(VideoStreamId id) { return impl_->RequestIdr(id); }
int MppMediaPipeline::SetOsd(const OsdConfig&) { return LEAVR_ERR_NOT_SUPPORTED; }
int MppMediaPipeline::CaptureJpeg(const char*, const JpegConfig&) { return LEAVR_ERR_NOT_SUPPORTED; }
float MppMediaPipeline::GetActualFps() {
    if (impl_->start_pts_us_ == 0 || impl_->last_main_pts_us_ <= impl_->start_pts_us_) {
        return 0.0f;
    }
    const double elapsed =
        static_cast<double>(impl_->last_main_pts_us_ - impl_->start_pts_us_) / 1000000.0;
    return static_cast<float>((impl_->frames_[0] - 1) / elapsed);
}
int MppMediaPipeline::GetBitrate() { return impl_->config_.main_stream.bitrate; }

MediaPipelineConfig MakeDefaultSc4336PipelineConfig() {
    MediaPipelineConfig cfg = {};
    cfg.sensor.type = SENSOR_TYPE_SC4336;
    snprintf(cfg.sensor.name, sizeof(cfg.sensor.name), "SC4336P");
    cfg.sensor.native_width = 2560;
    cfg.sensor.native_height = 1440;
    cfg.sensor.max_fps = 30;
    cfg.sensor.eis_crop_width = 2304;
    cfg.sensor.eis_crop_height = 1296;

    cfg.main_stream = {VIDEO_STREAM_MAIN, PAYLOAD_H265, 1920, 1080, 30, 4096,
                       30, RC_MODE_CBR, kMainChannel};
    cfg.sub_stream = {VIDEO_STREAM_SUB, PAYLOAD_H264, 1280, 720, 15, 1024,
                      15, RC_MODE_CBR, kSubChannel};
    cfg.eis_enable = true;
    return cfg;
}

} // namespace leavr
