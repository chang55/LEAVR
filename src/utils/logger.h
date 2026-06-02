/**
 * @file logger.h
 * @brief 日志系统 - 线程安全的环形缓冲日志
 */

#ifndef LEAVR_UTILS_LOGGER_H
#define LEAVR_UTILS_LOGGER_H

#include "leavr_interfaces.h"
#include <pthread.h>
#include <cstdio>
#include <cstdarg>
#include <atomic>
#include <queue>
#include <mutex>
#include <string>

namespace leavr {

class Logger : public ILogger {
public:
    static Logger& Instance();

    int Init(Level min_level, const char* log_dir, int max_size_mb) override;
    void Log(Level level, const char* file, int line,
             const char* func, const char* fmt, ...) override;
    void Flush() override;

    void SetMinLevel(Level level) { min_level_ = level; }

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    struct LogEntry {
        Level level;
        uint64_t timestamp_ms;
        char message[512];
    };

    static constexpr size_t kMaxQueueSize = 4096;
    static const char* LevelStr(Level level);

    void WriterThread();

    Level min_level_ = INFO;
    std::string log_dir_;
    int max_size_mb_ = 50;
    FILE* file_ = nullptr;
    int current_size_ = 0;

    std::queue<LogEntry> queue_;
    pthread_mutex_t queue_lock_ = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t queue_cond_ = PTHREAD_COND_INITIALIZER;
    pthread_t writer_thread_ = 0;
    std::atomic<bool> running_{false};
};

// 便利宏
#define LOG_DEBUG(fmt, ...) \
    Logger::Instance().Log(ILogger::DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) \
    Logger::Instance().Log(ILogger::INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) \
    Logger::Instance().Log(ILogger::WARN, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) \
    Logger::Instance().Log(ILogger::ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) \
    Logger::Instance().Log(ILogger::FATAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_FLUSH() Logger::Instance().Flush()

} // namespace leavr

#endif // LEAVR_UTILS_LOGGER_H