#ifndef LEAVR_NETWORK_RTSP_SERVER_H
#define LEAVR_NETWORK_RTSP_SERVER_H

#include "leavr_interfaces.h"
#include <memory>

namespace leavr {

class RtspServer : public IRtspServer {
public:
    RtspServer();
    ~RtspServer() override;

    int Start(int port, const char* stream_path, const StreamProfile& profile) override;
    int Stop() override;
    bool IsRunning() const override;
    int GetClientCount() const override;
    void OnVideoFrame(const EncodedVideoFrame& frame) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace leavr

#endif
