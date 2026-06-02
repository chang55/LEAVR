/**
 * @file main.cpp
 * @brief LEAVR 执法记录仪 - 主入口
 *        HI3516 + Linux 4.9 + IMX586 + ICM-20948 + 5G/WiFi
 */

#include "app/state_machine.h"
#include "utils/logger.h"
#include "utils/ring_buffer.h"
#include "media/eis/eis_processor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

using namespace leavr;

// ================================================================
// 全局变量
// ================================================================

static StateMachine g_state_machine;
static EisProcessor g_eis;
static PreRecordBuffer g_pre_record;

static volatile bool g_running = true;
static pthread_t g_main_thread_id = 0;

// ================================================================
// 信号处理
// ================================================================

static void signal_handler(int sig) {
    LOG_INFO("Received signal %d, shutting down...", sig);
    g_running = false;
}

static void setup_signal_handlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    // 忽略 SIGHUP 防止串口断开导致退出
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
}

// ================================================================
// 系统初始化
// ================================================================

static int system_init() {
    // 1. 日志初始化
    int ret = Logger::Instance().Init(ILogger::DEBUG, "/mnt/sdcard/Log", 50);
    if (ret != LEAVR_OK) {
        fprintf(stderr, "Logger init failed: %d\n", ret);
        return ret;
    }

    LOG_INFO("========================================");
    LOG_INFO("  LEAVR 执法记录仪 v1.0");
    LOG_INFO("  Platform: HI3516 + Linux 4.9");
    LOG_INFO("  Sensor: IMX586, EIS: ICM-20948");
    LOG_INFO("  Build: %s %s", __DATE__, __TIME__);
    LOG_INFO("========================================");

    // 2. SD 卡检测
    if (access("/mnt/sdcard", F_OK) != 0) {
        LOG_WARN("SD card not mounted at /mnt/sdcard");
    }

    // 3. 初始化预录缓冲区 (10秒预录)
    if (g_pre_record.Init(10) != LEAVR_OK) {
        LOG_WARN("Pre-record buffer init failed");
    }

    // 4. 初始化 EIS 防抖处理器
    if (g_eis.Init(4000, 2250, 3840, 2160) != LEAVR_OK) {
        LOG_WARN("EIS processor init failed");
    }

    // 5. 初始化状态机
    g_state_machine.SetEventCallback([](EventType event, int result) {
        if (result != LEAVR_OK) {
            LOG_ERROR("Event %s failed: %s",
                      StateMachine::GetEventName(event),
                      leavr_strerror(result));
        }
    });

    // 配置状态转移动作
    // BOOT → STANDBY
    g_state_machine.RegisterTransition(
        DEVICE_STATE_BOOT, EVENT_INIT_DONE, DEVICE_STATE_STANDBY,
        []() -> int {
            LOG_INFO("Device boot complete, entering STANDBY mode");
            // TODO: 开启 VI 预览
            // TODO: 启动 EIS 预录缓冲
            return LEAVR_OK;
        });

    // STANDBY → RECORDING
    g_state_machine.RegisterTransition(
        DEVICE_STATE_STANDBY, EVENT_KEY_RECORD, DEVICE_STATE_RECORDING,
        []() -> int {
            LOG_INFO("=== START RECORDING ===");
            // TODO: 创建录像文件 .tmp
            // TODO: 从预录缓冲区写入初始 GOP
            // TODO: 启动录像线程 (VI→VPSS→VENC→AVS)
            // TODO: 启动音频采集 (AI→AENC)
            // TODO: 启动 EIS 防抖 + 启动网络推流
            return LEAVR_OK;
        });

    // RECORDING → STANDBY (停止录像)
    g_state_machine.RegisterTransition(
        DEVICE_STATE_RECORDING, EVENT_KEY_STOP, DEVICE_STATE_STANDBY,
        []() -> int {
            LOG_INFO("=== STOP RECORDING ===");
            // TODO: 停止录像线程
            // TODO: 刷新 AVS Buffer
            // TODO: .tmp → .mp4 重命名 + 写入 .idx
            // TODO: SQLite INSERT
            return LEAVR_OK;
        });

    // STANDBY → CAPTURING
    g_state_machine.RegisterTransition(
        DEVICE_STATE_STANDBY, EVENT_KEY_CAPTURE, DEVICE_STATE_CAPTURING,
        []() -> int {
            LOG_INFO("=== CAPTURE PHOTO ===");
            // TODO: 停止预览 → 切换到 Remosaic 48MP 模式
            // TODO: 抓取一帧 → JPEG 编码 → 写入文件
            // TODO: 恢复预览
            // 异步发送 CAPTURE_DONE 事件
            return LEAVR_OK;
        });

    // CAPTURING → STANDBY
    g_state_machine.RegisterTransition(
        DEVICE_STATE_CAPTURING, EVENT_CAPTURE_DONE, DEVICE_STATE_STANDBY,
        []() -> int {
            LOG_INFO("Capture done, back to STANDBY");
            return LEAVR_OK;
        });

    g_state_machine.Init();
    g_state_machine.PostEvent(EVENT_INIT_DONE);

    LOG_INFO("System initialization complete");
    return LEAVR_OK;
}

// ================================================================
// 状态监视器线程
// ================================================================

static void* status_monitor_thread(void* arg) {
    while (g_running) {
        DeviceState state = g_state_machine.GetState();
        LOG_INFO("Status: state=%s",
                 StateMachine::GetStateName(state));
        sleep(10);
    }
    return nullptr;
}

// ================================================================
// 模拟按键线程 (用于开发测试)
// ================================================================

static void* mock_key_thread(void* arg) {
    // 等待初始化完成
    sleep(2);

    // 模拟按键序列: 录像 5 秒 → 停止
    LOG_INFO("[MOCK] Press RECORD key");
    g_state_machine.PostEvent(EVENT_KEY_RECORD);

    sleep(5);

    LOG_INFO("[MOCK] Press STOP key");
    g_state_machine.PostEvent(EVENT_KEY_STOP);

    sleep(2);

    LOG_INFO("[MOCK] Press CAPTURE key");
    g_state_machine.PostEvent(EVENT_KEY_CAPTURE);

    sleep(1);

    LOG_INFO("[MOCK] Capture done");
    g_state_machine.PostEvent(EVENT_CAPTURE_DONE);

    return nullptr;
}

// ================================================================
// 主函数
// ================================================================

int main(int argc, char* argv[]) {
    setup_signal_handlers();

    int ret = system_init();
    if (ret != LEAVR_OK) {
        fprintf(stderr, "System init failed: %d\n", ret);
        return EXIT_FAILURE;
    }

    // 启动状态监视线程
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, nullptr, status_monitor_thread, nullptr);

    // 启动模拟按键线程 (仅开发/调试用)
    pthread_t mock_thread;
    pthread_create(&mock_thread, nullptr, mock_key_thread, nullptr);

    // 主循环: 等待事件 + 看门狗喂狗
    LOG_INFO("Main loop started, waiting for events...");
    while (g_running) {
        // TODO: 从按键 HAL 读取事件
        // TODO: 从传感器监测线程读取事件
        // TODO: 看门狗喂狗

        usleep(100000);  // 100ms
    }

    LOG_INFO("Shutting down...");
    LOG_FLUSH();

    pthread_join(mock_thread, nullptr);
    pthread_join(monitor_thread, nullptr);

    return EXIT_SUCCESS;
}