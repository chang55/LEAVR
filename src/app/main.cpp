/**
 * @file main.cpp
 * @brief HI3516CV610 + SC4336P 执法记录仪原型入口
 *
 * SC4336P 用于当前双码流、RTSP 和陀螺仪 EIS 原型验证。
 * 媒体层通过 SensorProfile 隔离传感器差异，后续迁移到 IMX586。
 */

#include "app/state_machine.h"
#include "hal/icm20948_hal.h"
#include "media/eis/eis_processor.h"
#include "media/pipeline/mpp_media_pipeline.h"
#include "network/rtsp_server.h"
#include "storage/raw_video_recorder.h"
#include "utils/config_parser.h"
#include "utils/logger.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace leavr;

static StateMachine g_state_machine;
static MppMediaPipeline g_media;
static RawVideoRecorder g_recorder;
static RtspServer g_rtsp;
static Icm20948Hal g_imu;
static EisProcessor g_eis;
static MediaPipelineConfig g_media_config;
static volatile sig_atomic_t g_running = 1;
static bool g_imu_available = false;
static int g_rtsp_port = 554;
static int g_eis_strength = 50;

static void signal_handler(int) {
    g_running = 0;
}

static void setup_signal_handlers() {
    struct sigaction sa = {};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
}

static int ensure_directory(const char* path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST) return LEAVR_OK;
    return LEAVR_ERR_IO;
}

static std::string make_record_path() {
    time_t now = time(nullptr);
    struct tm tm_buf = {};
    localtime_r(&now, &tm_buf);
    char path[256];
    snprintf(path, sizeof(path),
             "/mnt/sdcard/Video/LEAVR_%04d%02d%02d_%02d%02d%02d.h265",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    return path;
}

static int register_state_actions() {
    g_state_machine.Init();
    g_state_machine.SetEventCallback([](EventType event, int result) {
        if (result != LEAVR_OK) {
            LOG_ERROR("Event %s failed: %s",
                      StateMachine::GetEventName(event), leavr_strerror(result));
        }
    });

    g_state_machine.RegisterTransition(
        DEVICE_STATE_BOOT, EVENT_INIT_DONE, DEVICE_STATE_STANDBY,
        []() -> int {
            LOG_INFO("Device ready: SC4336P live preview active");
            return LEAVR_OK;
        });

    g_state_machine.RegisterTransition(
        DEVICE_STATE_STANDBY, EVENT_KEY_RECORD, DEVICE_STATE_RECORDING,
        []() -> int {
            if (ensure_directory("/mnt/sdcard") != LEAVR_OK ||
                ensure_directory("/mnt/sdcard/Video") != LEAVR_OK) {
                return LEAVR_ERR_FILE_OPEN;
            }
            const std::string path = make_record_path();
            return g_recorder.StartRecording(path.c_str(), g_media_config.main_stream);
        });

    g_state_machine.RegisterTransition(
        DEVICE_STATE_RECORDING, EVENT_KEY_STOP, DEVICE_STATE_STANDBY,
        []() -> int {
            return g_recorder.StopRecording();
        });
    return LEAVR_OK;
}

static int start_eis() {
    int ret = g_eis.Init(g_media_config.sensor.native_width,
                         g_media_config.sensor.native_height,
                         g_media_config.sensor.eis_crop_width,
                         g_media_config.sensor.eis_crop_height);
    if (ret != LEAVR_OK) return ret;
    ret = g_eis.Start();
    if (ret != LEAVR_OK) return ret;
    g_eis.SetStrength(g_eis_strength);

    g_imu.SetDataCallback([](const EisFrameData& data) {
        const int push_ret = g_eis.PushGyroData(data);
        if (push_ret != LEAVR_OK && push_ret != LEAVR_ERR_BUSY) {
            LOG_WARN("EIS: gyro push failed: %d", push_ret);
        }
    });
    ret = g_imu.Init("/dev/i2c-1", 0x68);
    if (ret != LEAVR_OK) {
        LOG_WARN("EIS: ICM-20948 unavailable, using centered crop");
        return LEAVR_OK;
    }
    ret = g_imu.Start();
    if (ret == LEAVR_OK) g_imu_available = true;
    return LEAVR_OK;
}

static int system_init() {
    ensure_directory("/mnt/sdcard");
    ensure_directory("/mnt/sdcard/Log");
    int ret = Logger::Instance().Init(ILogger::INFO, "/mnt/sdcard/Log", 50);
    if (ret != LEAVR_OK) return ret;

    LOG_INFO("LEAVR prototype: HI3516CV610 + SC4336P");
    g_media_config = MakeDefaultSc4336PipelineConfig();

    ConfigParser config;
    if (config.Load("/mnt/sdcard/Config/device.conf") == LEAVR_OK) {
        const char* sensor = config.GetString("System", "sensor_type", "sc4336");
        if (strcmp(sensor, "sc4336") != 0) {
            LOG_WARN("Configured sensor '%s' is not implemented; using SC4336P", sensor);
        }
        g_media_config.eis_enable = config.GetBool("EIS", "eis_enable", true);
        g_media_config.main_stream.bitrate =
            config.GetInt("Record", "bitrate", g_media_config.main_stream.bitrate);
        g_media_config.sub_stream.bitrate =
            config.GetInt("Record", "sub_bitrate", g_media_config.sub_stream.bitrate);
        g_rtsp_port = config.GetInt("Network", "rtsp_port", 554);
        const char* strength = config.GetString("EIS", "eis_strength", "medium");
        g_eis_strength = strcmp(strength, "low") == 0 ? 30 :
                         strcmp(strength, "high") == 0 ? 80 : 50;
    }

    ret = register_state_actions();
    if (ret != LEAVR_OK) return ret;

    ret = g_media.Init(g_media_config);
    if (ret != LEAVR_OK) return ret;
    g_media.RegisterSink(&g_recorder);
    g_media.RegisterSink(&g_rtsp);

    ret = g_media.Start();
    if (ret != LEAVR_OK) return ret;

    ret = g_rtsp.Start(g_rtsp_port, "/live/sub", g_media_config.sub_stream);
    if (ret != LEAVR_OK) {
        LOG_WARN("RTSP start failed: %d; local media remains active", ret);
    }

    if (g_media_config.eis_enable) start_eis();
    g_state_machine.PostEvent(EVENT_INIT_DONE);
    return LEAVR_OK;
}

static void* status_monitor_thread(void*) {
    unsigned ticks = 0;
    while (g_running) {
        usleep(100000);
        if (++ticks < 100) continue;
        ticks = 0;
        LOG_INFO("Status: state=%s rtsp_clients=%d recorded_bytes=%llu imu=%s",
                 StateMachine::GetStateName(g_state_machine.GetState()),
                 g_rtsp.GetClientCount(),
                 static_cast<unsigned long long>(g_recorder.GetBytesWritten()),
                 g_imu_available ? "online" : "fallback");
    }
    return nullptr;
}

static void* eis_crop_thread(void*) {
    while (g_running) {
        if (g_media_config.eis_enable) {
            const EisCropWindow crop = g_eis.GetCropWindow();
            const int ret = g_media.SetCropWindow(crop);
            if (ret != LEAVR_OK && ret != LEAVR_ERR_NOT_INIT) {
                LOG_WARN("EIS: VPSS crop update failed: %d", ret);
            }
        }
        usleep(33333);
    }
    return nullptr;
}

static bool has_arg(int argc, char* argv[], const char* arg) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], arg) == 0) return true;
    }
    return false;
}

static void system_shutdown() {
    if (g_recorder.IsRecording()) g_recorder.StopRecording();
    g_rtsp.Stop();
    if (g_imu_available) g_imu.Stop();
    g_eis.Stop();
    g_media.Stop();
    LOG_INFO("System shutdown complete");
    LOG_FLUSH();
}

int main(int argc, char* argv[]) {
    setup_signal_handlers();
    const int ret = system_init();
    if (ret != LEAVR_OK) {
        fprintf(stderr, "System init failed: %d (%s)\n", ret, leavr_strerror(ret));
        return EXIT_FAILURE;
    }

    pthread_t monitor_thread = 0;
    pthread_t crop_thread = 0;
    pthread_create(&monitor_thread, nullptr, status_monitor_thread, nullptr);
    pthread_create(&crop_thread, nullptr, eis_crop_thread, nullptr);

    if (has_arg(argc, argv, "--record-on-boot")) {
        g_state_machine.PostEvent(EVENT_KEY_RECORD);
    }

    LOG_INFO("Main loop active; RTSP port=%d path=/live/sub", g_rtsp_port);
    while (g_running) usleep(100000);

    if (g_state_machine.IsRecording()) g_state_machine.PostEvent(EVENT_KEY_STOP);
    if (crop_thread) pthread_join(crop_thread, nullptr);
    if (monitor_thread) pthread_join(monitor_thread, nullptr);
    system_shutdown();
    return EXIT_SUCCESS;
}
