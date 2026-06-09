#include "network/rtsp_server.h"
#include "media/frame_utils.h"
#include "utils/logger.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sstream>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace leavr {
namespace {

constexpr size_t kMaxClients = 2;
constexpr size_t kMaxQueuedFrames = 32;
constexpr size_t kRtpPayloadMax = 1400;

struct QueuedFrame {
    std::vector<uint8_t> data;
    uint64_t pts_us;
    bool key;
};

bool SetNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

std::string HeaderValue(const std::string& request, const char* name) {
    const std::string key = std::string(name) + ":";
    size_t pos = request.find(key);
    if (pos == std::string::npos) return "";
    pos += key.size();
    while (pos < request.size() && request[pos] == ' ') ++pos;
    size_t end = request.find("\r\n", pos);
    return request.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

bool SendAllNonBlocking(int fd, const uint8_t* data, size_t size) {
    const ssize_t sent = send(fd, data, size, MSG_NOSIGNAL | MSG_DONTWAIT);
    return sent == static_cast<ssize_t>(size);
}

} // namespace

class RtspServer::Impl {
public:
    struct Client {
        int rtsp_fd = -1;
        sockaddr_in peer = {};
        std::string input;
        bool playing = false;
        bool tcp_interleaved = true;
        int udp_fd = -1;
        sockaddr_in rtp_peer = {};
        uint16_t sequence = 0;
        uint32_t ssrc = 0;
        std::string session;
    };

    int Start(int port, const char* path, const StreamProfile& profile);
    int Stop();
    void Push(const EncodedVideoFrame& frame);
    static void* ThreadEntry(void* arg);
    void ServerThread();
    void AcceptClient();
    bool HandleClientInput(Client& client);
    bool HandleRequest(Client& client, const std::string& request);
    bool SendResponse(Client& client, const std::string& response);
    void BroadcastFrame(const QueuedFrame& frame);
    bool SendNalu(Client& client, const NaluView& nalu, uint32_t timestamp, bool marker);
    bool SendRtp(Client& client, const uint8_t* payload, size_t payload_size,
                 uint32_t timestamp, bool marker);
    void CloseClient(Client& client);

    int listen_fd_ = -1;
    int port_ = 554;
    std::string path_;
    StreamProfile profile_ = {};
    pthread_t thread_ = 0;
    pthread_mutex_t queue_lock_ = PTHREAD_MUTEX_INITIALIZER;
    std::deque<QueuedFrame> queue_;
    std::vector<Client> clients_;
    std::atomic<bool> running_{false};
    std::atomic<int> client_count_{0};
};

int RtspServer::Impl::Start(int port, const char* path, const StreamProfile& profile) {
    if (running_) return LEAVR_OK;
    if (!path || profile.codec != PAYLOAD_H264) return LEAVR_ERR_PARAM;
    port_ = port;
    path_ = path[0] == '/' ? path : std::string("/") + path;
    profile_ = profile;

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return LEAVR_ERR_RTSP_INIT;
    int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(listen_fd_, static_cast<int>(kMaxClients)) != 0 ||
        !SetNonBlocking(listen_fd_)) {
        close(listen_fd_);
        listen_fd_ = -1;
        return LEAVR_ERR_RTSP_INIT;
    }

    running_ = true;
    if (pthread_create(&thread_, nullptr, ThreadEntry, this) != 0) {
        running_ = false;
        close(listen_fd_);
        listen_fd_ = -1;
        return LEAVR_ERR_RTSP_INIT;
    }
    LOG_INFO("RTSP: listening on rtsp://0.0.0.0:%d%s", port_, path_.c_str());
    return LEAVR_OK;
}

int RtspServer::Impl::Stop() {
    running_ = false;
    if (thread_) {
        pthread_join(thread_, nullptr);
        thread_ = 0;
    }
    for (auto& client : clients_) CloseClient(client);
    clients_.clear();
    client_count_ = 0;
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    pthread_mutex_lock(&queue_lock_);
    queue_.clear();
    pthread_mutex_unlock(&queue_lock_);
    return LEAVR_OK;
}

void RtspServer::Impl::Push(const EncodedVideoFrame& frame) {
    if (!running_ || frame.stream_id != profile_.stream_id || frame.codec != PAYLOAD_H264) return;
    QueuedFrame queued = {};
    queued.data.assign(frame.data, frame.data + frame.size);
    queued.pts_us = frame.pts_us;
    queued.key = frame.is_key_frame;

    pthread_mutex_lock(&queue_lock_);
    if (queue_.size() >= kMaxQueuedFrames) {
        auto it = std::find_if(queue_.begin(), queue_.end(),
                               [](const QueuedFrame& item) { return !item.key; });
        if (it != queue_.end()) queue_.erase(it);
        else queue_.pop_front();
    }
    queue_.push_back(std::move(queued));
    pthread_mutex_unlock(&queue_lock_);
}

void* RtspServer::Impl::ThreadEntry(void* arg) {
    static_cast<Impl*>(arg)->ServerThread();
    return nullptr;
}

void RtspServer::Impl::AcceptClient() {
    while (clients_.size() < kMaxClients) {
        Client client;
        socklen_t len = sizeof(client.peer);
        client.rtsp_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client.peer), &len);
        if (client.rtsp_fd < 0) break;
        SetNonBlocking(client.rtsp_fd);
        client.ssrc = static_cast<uint32_t>(client.rtsp_fd * 2654435761U);
        client.session = std::to_string(client.ssrc);
        clients_.push_back(client);
        client_count_ = clients_.size();
        LOG_INFO("RTSP: client connected, count=%d", client_count_.load());
    }
}

bool RtspServer::Impl::SendResponse(Client& client, const std::string& response) {
    return SendAllNonBlocking(client.rtsp_fd,
                              reinterpret_cast<const uint8_t*>(response.data()), response.size());
}

bool RtspServer::Impl::HandleRequest(Client& client, const std::string& request) {
    std::istringstream first_line(request.substr(0, request.find("\r\n")));
    std::string method;
    std::string uri;
    std::string version;
    first_line >> method >> uri >> version;
    const std::string cseq = HeaderValue(request, "CSeq");
    const std::string common = "CSeq: " + cseq + "\r\nServer: LEAVR/1.0\r\n";

    if (method != "OPTIONS" && uri.find(path_) == std::string::npos) {
        return SendResponse(client, "RTSP/1.0 404 Not Found\r\n" + common + "\r\n");
    }
    if (method == "OPTIONS") {
        return SendResponse(client, "RTSP/1.0 200 OK\r\n" + common +
            "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n\r\n");
    }
    if (method == "DESCRIBE") {
        std::ostringstream sdp;
        sdp << "v=0\r\n"
            << "o=- 0 0 IN IP4 0.0.0.0\r\n"
            << "s=LEAVR Live\r\n"
            << "t=0 0\r\n"
            << "a=control:*\r\n"
            << "m=video 0 RTP/AVP 96\r\n"
            << "a=rtpmap:96 H264/90000\r\n"
            << "a=fmtp:96 packetization-mode=1;profile-level-id=42e01f\r\n"
            << "a=control:track1\r\n";
        const std::string body = sdp.str();
        std::ostringstream response;
        response << "RTSP/1.0 200 OK\r\n" << common
                 << "Content-Type: application/sdp\r\n"
                 << "Content-Base: " << uri << "/\r\n"
                 << "Content-Length: " << body.size() << "\r\n\r\n" << body;
        return SendResponse(client, response.str());
    }
    if (method == "SETUP") {
        const std::string transport = HeaderValue(request, "Transport");
        std::ostringstream response;
        if (transport.find("RTP/AVP/TCP") != std::string::npos) {
            if (client.udp_fd >= 0) {
                close(client.udp_fd);
                client.udp_fd = -1;
            }
            client.tcp_interleaved = true;
            response << "RTSP/1.0 200 OK\r\n" << common
                     << "Session: " << client.session << "\r\n"
                     << "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n\r\n";
        } else {
            const size_t pos = transport.find("client_port=");
            if (pos == std::string::npos) {
                return SendResponse(client, "RTSP/1.0 461 Unsupported Transport\r\n" + common + "\r\n");
            }
            const int client_port = atoi(transport.c_str() + pos + strlen("client_port="));
            if (client_port <= 0 || client_port > 65534) {
                return SendResponse(client,
                    "RTSP/1.0 461 Unsupported Transport\r\n" + common + "\r\n");
            }
            client.tcp_interleaved = false;
            if (client.udp_fd >= 0) close(client.udp_fd);
            client.udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (client.udp_fd < 0) {
                return SendResponse(client,
                    "RTSP/1.0 500 Internal Server Error\r\n" + common + "\r\n");
            }
            client.rtp_peer = client.peer;
            client.rtp_peer.sin_port = htons(static_cast<uint16_t>(client_port));
            response << "RTSP/1.0 200 OK\r\n" << common
                     << "Session: " << client.session << "\r\n"
                     << "Transport: RTP/AVP;unicast;client_port=" << client_port
                     << "-" << client_port + 1 << "\r\n\r\n";
        }
        return SendResponse(client, response.str());
    }
    if (method == "PLAY") {
        client.playing = true;
        return SendResponse(client, "RTSP/1.0 200 OK\r\n" + common +
            "Session: " + client.session + "\r\nRange: npt=0.000-\r\n\r\n");
    }
    if (method == "TEARDOWN") {
        SendResponse(client, "RTSP/1.0 200 OK\r\n" + common + "\r\n");
        return false;
    }
    return SendResponse(client, "RTSP/1.0 405 Method Not Allowed\r\n" + common + "\r\n");
}

bool RtspServer::Impl::HandleClientInput(Client& client) {
    char buf[2048];
    const ssize_t len = recv(client.rtsp_fd, buf, sizeof(buf), 0);
    if (len == 0) return false;
    if (len < 0) return errno == EAGAIN || errno == EWOULDBLOCK;
    client.input.append(buf, static_cast<size_t>(len));
    size_t end;
    while ((end = client.input.find("\r\n\r\n")) != std::string::npos) {
        const std::string request = client.input.substr(0, end + 4);
        client.input.erase(0, end + 4);
        if (!HandleRequest(client, request)) return false;
    }
    return true;
}

bool RtspServer::Impl::SendRtp(Client& client, const uint8_t* payload, size_t payload_size,
                               uint32_t timestamp, bool marker) {
    std::vector<uint8_t> packet(12 + payload_size);
    packet[0] = 0x80;
    packet[1] = static_cast<uint8_t>(96 | (marker ? 0x80 : 0));
    const uint16_t seq = client.sequence++;
    packet[2] = seq >> 8;
    packet[3] = seq & 0xff;
    packet[4] = timestamp >> 24;
    packet[5] = timestamp >> 16;
    packet[6] = timestamp >> 8;
    packet[7] = timestamp;
    packet[8] = client.ssrc >> 24;
    packet[9] = client.ssrc >> 16;
    packet[10] = client.ssrc >> 8;
    packet[11] = client.ssrc;
    memcpy(packet.data() + 12, payload, payload_size);

    if (!client.tcp_interleaved) {
        return sendto(client.udp_fd, packet.data(), packet.size(), MSG_DONTWAIT,
                      reinterpret_cast<sockaddr*>(&client.rtp_peer),
                      sizeof(client.rtp_peer)) == static_cast<ssize_t>(packet.size());
    }
    std::vector<uint8_t> interleaved(4 + packet.size());
    interleaved[0] = '$';
    interleaved[1] = 0;
    interleaved[2] = static_cast<uint8_t>(packet.size() >> 8);
    interleaved[3] = static_cast<uint8_t>(packet.size());
    memcpy(interleaved.data() + 4, packet.data(), packet.size());
    return SendAllNonBlocking(client.rtsp_fd, interleaved.data(), interleaved.size());
}

bool RtspServer::Impl::SendNalu(Client& client, const NaluView& nalu,
                                uint32_t timestamp, bool marker) {
    if (nalu.size <= kRtpPayloadMax) return SendRtp(client, nalu.data, nalu.size, timestamp, marker);
    if (nalu.size < 2) return true;
    const uint8_t nal_header = nalu.data[0];
    const uint8_t fu_indicator = (nal_header & 0xe0) | 28;
    const uint8_t nal_type = nal_header & 0x1f;
    size_t offset = 1;
    bool first = true;
    while (offset < nalu.size) {
        const size_t chunk = std::min(kRtpPayloadMax - 2, nalu.size - offset);
        const bool last = offset + chunk == nalu.size;
        std::vector<uint8_t> payload(2 + chunk);
        payload[0] = fu_indicator;
        payload[1] = nal_type | (first ? 0x80 : 0) | (last ? 0x40 : 0);
        memcpy(payload.data() + 2, nalu.data + offset, chunk);
        if (!SendRtp(client, payload.data(), payload.size(), timestamp, marker && last)) return false;
        first = false;
        offset += chunk;
    }
    return true;
}

void RtspServer::Impl::BroadcastFrame(const QueuedFrame& frame) {
    const auto nalus = SplitAnnexBNalus(frame.data.data(), frame.data.size());
    const uint32_t timestamp = static_cast<uint32_t>((frame.pts_us * 90ULL) / 1000ULL);
    for (auto& client : clients_) {
        if (!client.playing) continue;
        bool ok = true;
        for (size_t i = 0; i < nalus.size(); ++i) {
            if (!SendNalu(client, nalus[i], timestamp, i + 1 == nalus.size())) {
                ok = false;
                break;
            }
        }
        if (!ok) client.playing = false;
    }
}

void RtspServer::Impl::CloseClient(Client& client) {
    if (client.rtsp_fd >= 0) close(client.rtsp_fd);
    if (client.udp_fd >= 0) close(client.udp_fd);
    client.rtsp_fd = -1;
    client.udp_fd = -1;
}

void RtspServer::Impl::ServerThread() {
    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd_, &read_fds);
        int max_fd = listen_fd_;
        for (const auto& client : clients_) {
            FD_SET(client.rtsp_fd, &read_fds);
            max_fd = std::max(max_fd, client.rtsp_fd);
        }
        timeval timeout = {0, 20000};
        const int ready = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        if (ready > 0 && FD_ISSET(listen_fd_, &read_fds)) AcceptClient();

        for (auto it = clients_.begin(); it != clients_.end();) {
            if (ready > 0 && FD_ISSET(it->rtsp_fd, &read_fds) && !HandleClientInput(*it)) {
                CloseClient(*it);
                it = clients_.erase(it);
                client_count_ = clients_.size();
            } else {
                ++it;
            }
        }

        QueuedFrame frame;
        bool have_frame = false;
        pthread_mutex_lock(&queue_lock_);
        if (!queue_.empty()) {
            frame = std::move(queue_.front());
            queue_.pop_front();
            have_frame = true;
        }
        pthread_mutex_unlock(&queue_lock_);
        if (have_frame) BroadcastFrame(frame);
    }
}

RtspServer::RtspServer() : impl_(new Impl) {}
RtspServer::~RtspServer() { impl_->Stop(); }
int RtspServer::Start(int port, const char* path, const StreamProfile& profile) {
    return impl_->Start(port, path, profile);
}
int RtspServer::Stop() { return impl_->Stop(); }
bool RtspServer::IsRunning() const { return impl_->running_; }
int RtspServer::GetClientCount() const { return impl_->client_count_; }
void RtspServer::OnVideoFrame(const EncodedVideoFrame& frame) { impl_->Push(frame); }

} // namespace leavr
