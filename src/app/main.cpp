/**
 * @file main.cpp
 * @brief LEAVR 执法记录仪 - 主入口
 *
 * 硬件平台:
 *   - SoC:      海思 HI3516DV300 (双核 Cortex-A7)
 *   - 系统:     Linux 4.9 + Buildroot
 *   - 主摄:     Sony IMX586 (48MP, 4K@60fps)
 *   - 防抖:     TDK ICM-20948 (6轴 IMU)
 *   - 通信:     5G 模组 / WiFi
 *
 * 核心功能:
 *   1. 系统初始化: 日志 → SD卡检测 → 预录缓冲 → EIS → 状态机
 *   2. 状态机驱动: 管理设备运行状态 (BOOT → STANDBY → RECORDING/CAPTURING)
 *   3. 主循环:    100ms 轮询等待按键/传感器事件，并定期喂看门狗
 *   4. 辅助线程:  状态监视 + 模拟按键(仅开发调试用)
 *
 * 进程模型:
 *   - 所有功能运行在单一进程中，通过多线程协作
 *   - Media Pipeline (VI→VPSS→VENC→AVS) 在独立的录像线程中运行
 *   - 按键输入、传感器监测分别由 HAL 层线程处理
 */

#include "app/state_machine.h"       // 状态机: 管理设备运行状态及转移
#include "utils/logger.h"            // 日志模块: 带级别过滤和文件轮转
#include "utils/ring_buffer.h"       // 环形缓冲区: 用于预录 GOP 缓存
#include "media/eis/eis_processor.h" // EIS 电子防抖: 基于 ICM-20948 IMU 数据

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

// 设备状态机: 管理 BOOT/STANDBY/RECORDING/CAPTURING 等状态转移
static StateMachine g_state_machine;

// EIS 电子防抖处理器: 读取 IMU 数据，计算裁剪偏移，输出稳定画面
static EisProcessor g_eis;

// 预录缓冲区: 录像开始前缓存最近 N 秒的编码 GOP，实现"预录"功能
static PreRecordBuffer g_pre_record;

// 主循环运行标志: 收到 SIGINT/SIGTERM 时置 false，触发优雅退出
static volatile bool g_running = true;

// 主线程 ID (预留，用于信号处理时的线程同步)
static pthread_t g_main_thread_id = 0;

// ================================================================
// 信号处理
// ================================================================

/**
 * @brief 信号处理回调
 *
 * 收到 SIGINT/SIGTERM/SIGQUIT 时，将 g_running 置 false，
 * 主循环检测到后进入退出流程，保证录像文件正确关闭。
 */
static void signal_handler(int sig) {
    LOG_INFO("Received signal %d, shutting down...", sig);
    g_running = false;
}

/**
 * @brief 注册信号处理函数
 *
 * 监听:
 *   - SIGINT (Ctrl+C): 终端中断
 *   - SIGTERM:         系统关机 / kill 默认信号
 *   - SIGQUIT (Ctrl+\):终端退出
 *
 * 忽略:
 *   - SIGHUP:  串口/SSH 断开时避免进程退出 (设备常无头运行)
 *   - SIGPIPE: socket 写失败时避免进程退出
 */
static void setup_signal_handlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    // 忽略 SIGHUP 防止串口断开导致退出
    signal(SIGHUP, SIG_IGN);
    // 忽略 SIGPIPE 防止 socket 写入失败时退出
    signal(SIGPIPE, SIG_IGN);
}

// ================================================================
// 系统初始化
// ================================================================

/**
 * @brief 系统初始化主流程
 *
 * 初始化顺序 (严格按依赖关系排列):
 *   1. 日志模块   — 其他模块依赖日志输出，必须最先初始化
 *   2. SD 卡检测  — 确认存储介质可用，失败仅告警不阻塞
 *   3. 预录缓冲区 — 10秒循环缓冲，录像开始时可回填历史 GOP
 *   4. EIS 处理器 — 电子防抖核心，IMU 数据 → 裁剪偏移计算
 *   5. 状态机     — 注册所有状态转移规则，发送 INIT_DONE 进入 STANDBY
 *
 * @return LEAVR_OK 成功，否则失败
 */
static int system_init() {
    // ---- 1. 日志模块初始化 ----
    // 参数: 级别=DEBUG(输出所有日志), 路径=/mnt/sdcard/Log, 单文件上限=50MB
    int ret = Logger::Instance().Init(ILogger::DEBUG, "/mnt/sdcard/Log", 50);
    if (ret != LEAVR_OK) {
        // 日志未就绪，直接用 stderr 输出错误信息
        fprintf(stderr, "Logger init failed: %d\n", ret);
        return ret;
    }

    // 设备启动 Banner，方便从日志中定位版本和编译时间
    LOG_INFO("========================================");
    LOG_INFO("  LEAVR 执法记录仪 v1.0");
    LOG_INFO("  Platform: HI3516 + Linux 4.9");
    LOG_INFO("  Sensor: IMX586, EIS: ICM-20948");
    LOG_INFO("  Build: %s %s", __DATE__, __TIME__);
    LOG_INFO("========================================");

    // ---- 2. SD 卡检测 ----
    // 仅检查挂载点是否存在，不阻塞启动 (可能后续才插入卡)
    if (access("/mnt/sdcard", F_OK) != 0) {
        LOG_WARN("SD card not mounted at /mnt/sdcard");
    }

    // ---- 3. 预录缓冲区初始化 ----
    // 参数 10 = 最多缓存 10 秒的 H.264/H.265 编码数据
    // 录像开始时先写入缓冲区中的历史 GOP，实现"按下录像键前 10 秒已录"的效果
    if (g_pre_record.Init(10) != LEAVR_OK) {
        LOG_WARN("Pre-record buffer init failed");
    }

    // ---- 4. EIS 电子防抖处理器初始化 ----
    // 参数: crop_w=4000, crop_h=2250, input_w=3840, input_h=2160
    // 含义: IMX586 全像素 4000×2250 → 裁剪后输出 3840×2160 (4K)
    // 留下 160×90 像素余量用于电子防抖的平移补偿
    if (g_eis.Init(4000, 2250, 3840, 2160) != LEAVR_OK) {
        LOG_WARN("EIS processor init failed");
    }

    // ---- 5. 状态机初始化 ----
    // 设置全局事件回调: 任何状态转移失败时记录错误日志
    g_state_machine.SetEventCallback([](EventType event, int result) {
        if (result != LEAVR_OK) {
            LOG_ERROR("Event %s failed: %s",
                      StateMachine::GetEventName(event),
                      leavr_strerror(result));
        }
    });

    // ============================================================
    // 注册状态转移规则 (源状态 + 事件 → 目标状态 + 动作)
    // ============================================================

    // [转移1] BOOT → STANDBY: 初始化完成后自动进入待机
    g_state_machine.RegisterTransition(
        DEVICE_STATE_BOOT, EVENT_INIT_DONE, DEVICE_STATE_STANDBY,
        []() -> int {
            LOG_INFO("Device boot complete, entering STANDBY mode");
            // TODO: 开启 VI (Video Input) 预览通道，显示实时画面
            // TODO: 启动 EIS 预录缓冲，持续缓存编码数据
            return LEAVR_OK;
        });

    // [转移2] STANDBY → RECORDING: 用户按下录像键
    g_state_machine.RegisterTransition(
        DEVICE_STATE_STANDBY, EVENT_KEY_RECORD, DEVICE_STATE_RECORDING,
        []() -> int {
            LOG_INFO("=== START RECORDING ===");
            // TODO: 在 SD 卡创建临时录像文件 (.tmp)，防止异常断电丢数据
            // TODO: 将预录缓冲区中的历史 GOP 先写入文件开头
            // TODO: 启动录像线程: VI→VPSS(缩放/裁剪)→VENC(编码)→AVS(封装)
            // TODO: 启动音频采集: AI(Audio Input)→AENC(音频编码)
            // TODO: 启动 EIS 防抖实时处理 + 启动网络推流(5G/WiFi)
            return LEAVR_OK;
        });

    // [转移3] RECORDING → STANDBY: 用户停止录像
    g_state_machine.RegisterTransition(
        DEVICE_STATE_RECORDING, EVENT_KEY_STOP, DEVICE_STATE_STANDBY,
        []() -> int {
            LOG_INFO("=== STOP RECORDING ===");
            // TODO: 向录像线程发送停止信号，等待线程安全退出
            // TODO: 刷新 AVS (音视频同步) 缓冲区，写出尾部数据
            // TODO: 将 .tmp 重命名为 .mp4，写入 .idx 索引文件(快速定位)
            // TODO: 将文件信息写入 SQLite 数据库(文件路径/时长/大小/时间)
            return LEAVR_OK;
        });

    // [转移4] STANDBY → CAPTURING: 用户按下拍照键
    g_state_machine.RegisterTransition(
        DEVICE_STATE_STANDBY, EVENT_KEY_CAPTURE, DEVICE_STATE_CAPTURING,
        []() -> int {
            LOG_INFO("=== CAPTURE PHOTO ===");
            // TODO: 暂停 VI 预览 → 切换 IMX586 到 Remosaic 模式 (48MP 全分辨率)
            // TODO: 抓取单帧 Bayer 数据 → ISP 处理 → JPEG 硬件编码 → 写入文件
            // TODO: 恢复 VI 预览通道
            // 注意: 拍照是异步的，ISP 处理完成后发送 EVENT_CAPTURE_DONE
            return LEAVR_OK;
        });

    // [转移5] CAPTURING → STANDBY: 拍照完成，回到待机
    g_state_machine.RegisterTransition(
        DEVICE_STATE_CAPTURING, EVENT_CAPTURE_DONE, DEVICE_STATE_STANDBY,
        []() -> int {
            LOG_INFO("Capture done, back to STANDBY");
            return LEAVR_OK;
        });

    // 启动状态机，发送 INIT_DONE 事件触发 BOOT → STANDBY 转移
    g_state_machine.Init();
    g_state_machine.PostEvent(EVENT_INIT_DONE);

    LOG_INFO("System initialization complete");
    return LEAVR_OK;
}

// ================================================================
// 状态监视器线程
// ================================================================

/**
 * @brief 定期打印设备当前状态
 *
 * 每 10 秒输出一次状态，方便后台运行时从日志追溯设备行为。
 * 产线部署后可通过配置文件开关控制是否启用。
 */
static void* status_monitor_thread(void* arg) {
    (void)arg;  // 未使用参数
    while (g_running) {
        DeviceState state = g_state_machine.GetState();
        LOG_INFO("Status: state=%s",
                 StateMachine::GetStateName(state));
        sleep(10);
    }
    return nullptr;
}

// ================================================================
// 模拟按键线程 (仅用于开发/调试)
// ================================================================

/**
 * @brief 模拟按键序列，用于无硬件时的功能测试
 *
 * 测试流程:
 *   等待 2s → 按下 "录像键" → 录像 5s → 按下 "停止键"
 *          → 按下 "拍照键" → 拍摄完成 → 退出
 *
 * 最终产品中此线程应被移除，改为从 GPIO/触摸屏 HAL 读取真实按键。
 */
static void* mock_key_thread(void* arg) {
    (void)arg;  // 未使用参数
    // 等待系统初始化完成
    sleep(2);

    // --- 测试录像流程 ---
    LOG_INFO("[MOCK] Press RECORD key");
    g_state_machine.PostEvent(EVENT_KEY_RECORD);

    sleep(5);  // 模拟录制 5 秒

    LOG_INFO("[MOCK] Press STOP key");
    g_state_machine.PostEvent(EVENT_KEY_STOP);

    sleep(2);

    // --- 测试拍照流程 ---
    LOG_INFO("[MOCK] Press CAPTURE key");
    g_state_machine.PostEvent(EVENT_KEY_CAPTURE);

    sleep(1);  // 模拟 ISP 处理耗时

    LOG_INFO("[MOCK] Capture done");
    g_state_machine.PostEvent(EVENT_CAPTURE_DONE);

    return nullptr;
}

// ================================================================
// 主函数
// ================================================================

/**
 * @brief LEAVR 执法记录仪主入口
 *
 * 启动流程:
 *   1. setup_signal_handlers()  — 注册信号处理，保证可优雅退出
 *   2. system_init()            — 初始化所有子系统 (日志/SD/EIS/状态机)
 *   3. 创建工作线程              — 状态监视 + 模拟按键(调试用)
 *   4. 主循环                   — 100ms 周期轮询，处理按键/传感器/喂狗
 *   5. 退出清理                 — 等待线程结束，刷新日志
 *
 * 主循环设计说明:
 *   - 100ms 轮询周期对按键响应延迟 < 200ms (优于用户体验要求)
 *   - 看门狗通常在 1-2s 超时，100ms 喂狗间隔余量充足
 *   - 使用 usleep 而非 sleep(0) 降低 CPU 占用
 *
 * @return EXIT_SUCCESS 正常退出, EXIT_FAILURE 初始化失败
 */
int main(int argc, char* argv[]) {
    (void)argc;  // 当前未使用命令行参数
    (void)argv;

    // ---- 第1步: 注册信号处理 ----
    setup_signal_handlers();

    // ---- 第2步: 初始化所有子系统 ----
    int ret = system_init();
    if (ret != LEAVR_OK) {
        fprintf(stderr, "System init failed: %d\n", ret);
        return EXIT_FAILURE;
    }

    // ---- 第3步: 启动辅助线程 ----
    // 状态监视: 每 10s 打印一次设备状态
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, nullptr, status_monitor_thread, nullptr);

    // 模拟按键: 仅开发阶段使用，产品发布时删除
    pthread_t mock_thread;
    pthread_create(&mock_thread, nullptr, mock_key_thread, nullptr);

    // ---- 第4步: 主循环 (100ms 事件轮询) ----
    LOG_INFO("Main loop started, waiting for events...");
    while (g_running) {
        // TODO: 从按键 HAL 读取 GPIO 事件，调用 PostEvent()
        // TODO: 从传感器监测线程读取事件 (低电量/过热/撞击)
        // TODO: 向看门狗设备写入喂狗信号 (/dev/watchdog)

        usleep(100000);  // 100ms 轮询间隔
    }

    // ---- 第5步: 退出清理 ----
    LOG_INFO("Shutting down...");
    LOG_FLUSH();  // 强制刷新日志缓冲区到 SD 卡

    // 等待工作线程安全结束
    pthread_join(mock_thread, nullptr);
    pthread_join(monitor_thread, nullptr);

    return EXIT_SUCCESS;
}