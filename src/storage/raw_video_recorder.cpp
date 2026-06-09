#include "storage/raw_video_recorder.h"
#include "utils/logger.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace leavr {

class RawVideoRecorder::Impl {
public:
    struct QueuedFrame {
        std::vector<uint8_t> data;
    };

    static constexpr size_t kMaxQueuedFrames = 300;

    int Start(const char* path, const StreamProfile& profile);
    int Stop();
    void Push(const EncodedVideoFrame& frame);
    static void* ThreadEntry(void* arg);
    void WriterThread();
    static bool WriteAll(int fd, const uint8_t* data, size_t size);

    StreamProfile profile_ = {};
    std::string final_path_;
    std::string temp_path_;
    int fd_ = -1;
    pthread_t thread_ = 0;
    pthread_mutex_t lock_ = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond_ = PTHREAD_COND_INITIALIZER;
    std::deque<QueuedFrame> queue_;
    std::atomic<bool> running_{false};
    std::atomic<bool> recording_{false};
    std::atomic<uint64_t> bytes_written_{0};
    std::atomic<uint64_t> dropped_frames_{0};
    std::atomic<bool> write_failed_{false};
};

bool RawVideoRecorder::Impl::WriteAll(int fd, const uint8_t* data, size_t size) {
    size_t written = 0;
    while (written < size) {
        const ssize_t ret = write(fd, data + written, size - written);
        if (ret < 0 && errno == EINTR) continue;
        if (ret <= 0) return false;
        written += static_cast<size_t>(ret);
    }
    return true;
}

int RawVideoRecorder::Impl::Start(const char* path, const StreamProfile& profile) {
    if (!path || path[0] == '\0') return LEAVR_ERR_PARAM;
    if (recording_) return LEAVR_ERR_BUSY;
    profile_ = profile;
    final_path_ = path;
    temp_path_ = final_path_ + ".tmp";
    pthread_mutex_lock(&lock_);
    queue_.clear();
    pthread_mutex_unlock(&lock_);
    fd_ = open(temp_path_.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd_ < 0) {
        LOG_ERROR("Recorder: open %s failed: %s", temp_path_.c_str(), strerror(errno));
        return LEAVR_ERR_FILE_OPEN;
    }
    bytes_written_ = 0;
    dropped_frames_ = 0;
    write_failed_ = false;
    running_ = true;
    recording_ = true;
    if (pthread_create(&thread_, nullptr, ThreadEntry, this) != 0) {
        running_ = false;
        recording_ = false;
        close(fd_);
        fd_ = -1;
        return LEAVR_ERR_NOT_INIT;
    }
    LOG_INFO("Recorder: raw stream started: %s", final_path_.c_str());
    return LEAVR_OK;
}

void RawVideoRecorder::Impl::Push(const EncodedVideoFrame& frame) {
    if (!recording_ || frame.stream_id != profile_.stream_id || !frame.data || frame.size == 0) return;
    QueuedFrame queued;
    queued.data.assign(frame.data, frame.data + frame.size);

    pthread_mutex_lock(&lock_);
    if (queue_.size() >= kMaxQueuedFrames) {
        ++dropped_frames_;
        pthread_mutex_unlock(&lock_);
        return;
    }
    queue_.push_back(std::move(queued));
    pthread_cond_signal(&cond_);
    pthread_mutex_unlock(&lock_);
}

void* RawVideoRecorder::Impl::ThreadEntry(void* arg) {
    static_cast<Impl*>(arg)->WriterThread();
    return nullptr;
}

void RawVideoRecorder::Impl::WriterThread() {
    while (true) {
        QueuedFrame frame;
        pthread_mutex_lock(&lock_);
        while (queue_.empty() && running_) pthread_cond_wait(&cond_, &lock_);
        if (queue_.empty() && !running_) {
            pthread_mutex_unlock(&lock_);
            break;
        }
        frame = std::move(queue_.front());
        queue_.pop_front();
        pthread_mutex_unlock(&lock_);

        if (!WriteAll(fd_, frame.data.data(), frame.data.size())) {
            write_failed_ = true;
            LOG_ERROR("Recorder: write failed: %s", strerror(errno));
            continue;
        }
        bytes_written_ += frame.data.size();
    }
}

int RawVideoRecorder::Impl::Stop() {
    if (!recording_) return LEAVR_OK;
    recording_ = false;
    running_ = false;
    pthread_cond_signal(&cond_);
    if (thread_) {
        pthread_join(thread_, nullptr);
        thread_ = 0;
    }

    int result = LEAVR_OK;
    if (fd_ >= 0) {
        if (fsync(fd_) != 0 || close(fd_) != 0) result = LEAVR_ERR_FILE_CLOSE;
        fd_ = -1;
    }
    if (write_failed_) result = LEAVR_ERR_FILE_WRITE;
    if (result == LEAVR_OK && rename(temp_path_.c_str(), final_path_.c_str()) != 0) {
        result = LEAVR_ERR_FILE_CLOSE;
    }
    LOG_INFO("Recorder: stopped, bytes=%llu dropped=%llu result=%d",
             static_cast<unsigned long long>(bytes_written_.load()),
             static_cast<unsigned long long>(dropped_frames_.load()), result);
    return result;
}

RawVideoRecorder::RawVideoRecorder() : impl_(new Impl) {}
RawVideoRecorder::~RawVideoRecorder() { impl_->Stop(); }
int RawVideoRecorder::StartRecording(const char* path, const StreamProfile& profile) {
    return impl_->Start(path, profile);
}
int RawVideoRecorder::StopRecording() { return impl_->Stop(); }
bool RawVideoRecorder::IsRecording() const { return impl_->recording_; }
uint64_t RawVideoRecorder::GetBytesWritten() const { return impl_->bytes_written_; }
void RawVideoRecorder::OnVideoFrame(const EncodedVideoFrame& frame) { impl_->Push(frame); }

} // namespace leavr
