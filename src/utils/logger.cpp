/**
 * @file logger.cpp
 * @brief 日志系统实现
 */

#include "logger.h"
#include <sys/time.h>
#include <unistd.h>
#include <cstring>
#include <ctime>

namespace leavr {

static const char* kLevelColors[] = {
    "\033[37m",  // DEBUG white
    "\033[32m",  // INFO  green
    "\033[33m",  // WARN  yellow
    "\033[31m",  // ERROR red
    "\033[35m",  // FATAL magenta
};

static const char* kLevelNames[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    running_ = false;
    pthread_cond_signal(&queue_cond_);
    if (writer_thread_) {
        pthread_join(writer_thread_, nullptr);
    }
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
}

int Logger::Init(Level min_level, const char* log_dir, int max_size_mb) {
    min_level_ = min_level;
    log_dir_ = log_dir;
    max_size_mb_ = max_size_mb;

    // 创建日志目录
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", log_dir);
    system(cmd);

    // 打开日志文件
    std::string file_path = log_dir_ + "/leavr.log";
    file_ = fopen(file_path.c_str(), "a");
    if (!file_) {
        fprintf(stderr, "Logger: Failed to open log file: %s\n", file_path.c_str());
        return LEAVR_ERR_FILE_OPEN;
    }

    // 启动写线程
    running_ = true;
    if (pthread_create(&writer_thread_, nullptr,
        [](void* arg) -> void* {
            static_cast<Logger*>(arg)->WriterThread();
            return nullptr;
        }, this) != 0) {
        running_ = false;
        return LEAVR_ERR_NOT_INIT;
    }

    LOG_INFO("Logger initialized, level=%s, dir=%s", kLevelNames[min_level], log_dir);
    return LEAVR_OK;
}

void Logger::Log(Level level, const char* file, int line,
                  const char* func, const char* fmt, ...) {
    if (level < min_level_) return;

    LogEntry entry;
    entry.level = level;

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    entry.timestamp_ms = static_cast<uint64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;

    // 提取文件名 (去掉路径)
    const char* basename = strrchr(file, '/');
    basename = basename ? basename + 1 : file;

    // 格式化日志
    int offset = snprintf(entry.message, sizeof(entry.message),
                           "%s[%s] ", kLevelColors[level], kLevelNames[level]);

    struct tm tm_buf;
    time_t sec = static_cast<time_t>(entry.timestamp_ms / 1000);
    localtime_r(&sec, &tm_buf);
    offset += strftime(entry.message + offset, sizeof(entry.message) - offset,
                        "%Y-%m-%d %H:%M:%S", &tm_buf);
    offset += snprintf(entry.message + offset, sizeof(entry.message) - offset,
                        ".%03d [%s:%d %s] ",
                        static_cast<int>(entry.timestamp_ms % 1000),
                        basename, line, func);

    va_list args;
    va_start(args, fmt);
    offset += vsnprintf(entry.message + offset, sizeof(entry.message) - offset, fmt, args);
    va_end(args);

    // 添加换行 + 颜色重置
    snprintf(entry.message + offset, sizeof(entry.message) - offset, "\033[0m\n");

    // 入队
    pthread_mutex_lock(&queue_lock_);
    if (queue_.size() < kMaxQueueSize) {
        queue_.push(entry);
    } else {
        // 队列满时丢弃最旧的，直接输出到 stderr
        fputs(entry.message, stderr);
    }
    pthread_cond_signal(&queue_cond_);
    pthread_mutex_unlock(&queue_lock_);

    // FATAL 立即刷新
    if (level == FATAL) {
        Flush();
    }
}

void Logger::Flush() {
    pthread_mutex_lock(&queue_lock_);
    pthread_cond_signal(&queue_cond_);
    pthread_mutex_unlock(&queue_lock_);

    if (file_) {
        fflush(file_);
    }
}

void Logger::WriterThread() {
    LogEntry entry;
    while (running_) {
        pthread_mutex_lock(&queue_lock_);
        while (queue_.empty() && running_) {
            pthread_cond_wait(&queue_cond_, &queue_lock_);
        }
        if (!running_) {
            // 写出剩余日志
            while (!queue_.empty()) {
                entry = queue_.front();
                queue_.pop();
                if (file_) {
                    fputs(entry.message, file_);
                }
            }
            pthread_mutex_unlock(&queue_lock_);
            break;
        }
        entry = queue_.front();
        queue_.pop();
        pthread_mutex_unlock(&queue_lock_);

        if (file_) {
            fputs(entry.message, file_);
            current_size_ += strlen(entry.message);

            // 日志文件分割
            if (current_size_ > max_size_mb_ * 1024 * 1024) {
                fclose(file_);
                std::string old_path = log_dir_ + "/leavr.log.old";
                std::string cur_path = log_dir_ + "/leavr.log";
                rename(cur_path.c_str(), old_path.c_str());
                file_ = fopen(cur_path.c_str(), "a");
                current_size_ = 0;
            }
        }
    }
    if (file_) {
        fflush(file_);
    }
}

} // namespace leavr