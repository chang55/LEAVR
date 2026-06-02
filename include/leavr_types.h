/**
 * @file leavr_types.h
 * @brief LEAVR 执法记录仪 - 通用类型定义
 * @version 1.0
 */

#ifndef LEAVR_TYPES_H
#define LEAVR_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 编码类型枚举
 * ================================================================ */
typedef enum {
    PAYLOAD_H264 = 0,       // H.264
    PAYLOAD_H265 = 1,       // H.265 / HEVC
    PAYLOAD_JPEG = 2,       // JPEG (拍照)
    PAYLOAD_MJPEG = 3,      // MJPEG
} PayloadType;

/* ================================================================
 * 音频编码类型
 * ================================================================ */
typedef enum {
    AUDIO_CODEC_AAC = 0,    // AAC (录像用)
    AUDIO_CODEC_PCM = 1,    // PCM / WAV
    AUDIO_CODEC_MP3 = 2,    // MP3
    AUDIO_CODEC_G711A = 3,  // G.711 A-law
    AUDIO_CODEC_G711U = 4,  // G.711 μ-law
} AudioCodec;

/* ================================================================
 * 码率控制模式
 * ================================================================ */
typedef enum {
    RC_MODE_CBR = 0,        // 固定码率
    RC_MODE_VBR = 1,        // 可变码率
    RC_MODE_AVBR = 2,       // 自适应可变码率
    RC_MODE_QVBR = 3,       // 质量可变码率
} RcMode;

/* ================================================================
 * 录像模式
 * ================================================================ */
typedef enum {
    RECORD_MODE_NORMAL = 0,     // 普通录像
    RECORD_MODE_PREVIEW = 1,    // 预览模式 (预录缓冲)
    RECORD_MODE_TIMED = 2,      // 定时录像
} RecordMode;

/* ================================================================
 * 分辨率预设
 * ================================================================ */
typedef enum {
    RES_4K_2160P = 0,       // 3840x2160
    RES_6M_2048P = 1,       // 3072x2048
    RES_1080P_60FPS = 2,    // 1920x1080 @60fps
    RES_1080P_30FPS = 3,    // 1920x1080 @30fps
    RES_720P = 4,           // 1280x720
    RES_D1 = 5,             // 704x576 (子码流)
} ResolutionPreset;

/* ================================================================
 * 网络类型
 * ================================================================ */
typedef enum {
    NET_TYPE_NONE = 0,
    NET_TYPE_WIFI = 1,
    NET_TYPE_5G = 2,
    NET_TYPE_ETHERNET = 3,
} NetworkType;

/* ================================================================
 * 设备状态
 * ================================================================ */
typedef enum {
    DEVICE_STATE_BOOT = 0,
    DEVICE_STATE_STANDBY = 1,
    DEVICE_STATE_RECORDING = 2,
    DEVICE_STATE_PAUSED = 3,
    DEVICE_STATE_CAPTURING = 4,
    DEVICE_STATE_PLAYBACK = 5,
    DEVICE_STATE_MENU = 6,
    DEVICE_STATE_USB = 7,
    DEVICE_STATE_SUSPEND = 8,
} DeviceState;

/* ================================================================
 * 事件类型
 * ================================================================ */
typedef enum {
    EVENT_INIT_DONE = 0,
    EVENT_KEY_RECORD = 1,
    EVENT_KEY_CAPTURE = 2,
    EVENT_KEY_STOP = 3,
    EVENT_KEY_PAUSE = 4,
    EVENT_KEY_RESUME = 5,
    EVENT_KEY_PLAYBACK = 6,
    EVENT_KEY_MENU = 7,
    EVENT_KEY_BACK = 8,
    EVENT_KEY_UP = 9,
    EVENT_KEY_DOWN = 10,
    EVENT_KEY_OK = 11,
    EVENT_USB_CONNECT = 20,
    EVENT_USB_DISCONNECT = 21,
    EVENT_BATTERY_LOW = 30,
    EVENT_BATTERY_CRITICAL = 31,
    EVENT_SDCARD_FULL = 40,
    EVENT_SDCARD_REMOVED = 41,
    EVENT_TIMER_TIMEOUT = 50,
    EVENT_CAPTURE_DONE = 51,
    EVENT_SEGMENT_TIMER = 52,
    EVENT_OVERRUN_TIMER = 53,
    EVENT_TIMED_START = 54,
    EVENT_TIMED_STOP = 55,
    EVENT_NETWORK_UP = 60,
    EVENT_NETWORK_DOWN = 61,
    EVENT_WATCHDOG_TIMEOUT = 99,
} EventType;

/* ================================================================
 * 按键码
 * ================================================================ */
typedef enum {
    KEY_RECORD = 0,
    KEY_CAPTURE = 1,
    KEY_STOP = 2,
    KEY_PLAYBACK = 3,
    KEY_MENU = 4,
    KEY_UP = 5,
    KEY_DOWN = 6,
    KEY_OK = 7,
    KEY_BACK = 8,
    KEY_POWER = 9,
    KEY_SOS = 10,
    KEY_COUNT = 11,
} KeyCode;

/* ================================================================
 * 按键事件类型
 * ================================================================ */
typedef enum {
    KEY_PRESS = 0,
    KEY_RELEASE = 1,
    KEY_LONG_PRESS = 2,
    KEY_DOUBLE_CLICK = 3,
} KeyEventType;

/* ================================================================
 * 文件类型
 * ================================================================ */
typedef enum {
    FILE_TYPE_VIDEO = 0,
    FILE_TYPE_PHOTO = 1,
    FILE_TYPE_AUDIO = 2,
} FileType;

/* ================================================================
 * 加密算法类型
 * ================================================================ */
typedef enum {
    CRYPTO_AES256_CTR = 0,
    CRYPTO_AES256_GCM = 1,
    CRYPTO_SM4_CTR = 2,
} CryptoAlgorithm;

/* ================================================================
 * 数据结构
 * ================================================================ */

// VENC 编码流
typedef struct {
    uint8_t* data;          // 编码数据指针
    uint32_t size;          // 数据大小 (字节)
    uint64_t pts;           // 时间戳 (微秒)
    bool is_key_frame;      // 是否关键帧
    int chn_id;             // 通道 ID
    uint32_t seq;           // 帧序号
} VencStream;

// 视频帧信息
typedef struct {
    int width;
    int height;
    int fps;
    int bitrate;            // Kbps
    PayloadType codec;
    RcMode rc_mode;
} VideoConfig;

// 音频配置
typedef struct {
    int sample_rate;        // 8000 / 16000 / 48000
    int bit_width;          // 16
    int channels;           // 1 = Mono
    AudioCodec codec;
    int bitrate;            // Kbps (AAC/MP3)
} AudioConfig;

// 录像文件配置
typedef struct {
    int width;
    int height;
    int fps;
    int bitrate;            // Kbps (主码流)
    int sub_bitrate;        // Kbps (子码流)
    PayloadType codec;
    RcMode rc_mode;
    bool eis_enable;
    int pre_record_sec;     // 预录时长 (秒)
    int overrun_sec;        // 延录时长 (秒)
    int segment_sec;        // 分段时长 (秒), 0=不分段
    char file_path[256];
} RecordConfig;

// 拍照配置
typedef struct {
    int width;
    int height;
    int quality;            // 70-100
    int burst_count;        // 连拍张数
    bool timestamp_stamp;
    char file_path[256];
} JpegConfig;

// OSD 配置
typedef struct {
    bool show_timestamp;
    bool show_device_id;
    bool show_police_id;
    bool show_gps;
    bool show_battery;
    bool show_record_icon;
    bool show_network;
    char timestamp_format[32];
    int font_size;
    uint32_t text_color;    // ARGB
    uint32_t bg_color;      // ARGB
} OsdConfig;

// GPS 位置数据
typedef struct {
    double latitude;
    double longitude;
    double altitude;        // 海拔 (m)
    float speed;            // 速度 (km/h)
    int satellites;         // 卫星数
    int fix_quality;        // 0=无效 1=GPS 2=DGPS
    bool is_fixed;          // 是否定位
    uint64_t timestamp;     // 时间戳 (ms)
} GpsPosition;

// EIS 陀螺仪数据
typedef struct {
    float gyro_x;           // 角速度 rad/s
    float gyro_y;
    float gyro_z;
    float accel_x;          // 加速度 m/s²
    float accel_y;
    float accel_z;
    uint64_t timestamp_us;  // 时间戳 (微秒)
} EisFrameData;

// EIS 裁剪窗口
typedef struct {
    int x;
    int y;
    int width;
    int height;
} EisCropWindow;

// GB28181 配置
typedef struct {
    char sip_id[64];
    char sip_domain[64];
    char sip_server[64];
    int sip_port;
    int keepalive_interval; // 秒
    int local_port;
} Gb28181Config;

// 系统设备状态信息
typedef struct {
    DeviceState state;
    int battery_percent;
    bool is_charging;
    bool is_recording;
    bool is_muted;
    bool ir_active;
    NetworkType active_network;
    int network_signal;     // 0-100 信号强度
    uint32_t record_elapsed_sec;
    uint64_t sd_free_bytes;
    uint64_t sd_total_bytes;
    float cpu_temp;         // CPU 温度 (℃)
    GpsPosition gps;
} SystemStatus;

#ifdef __cplusplus
}
#endif

#endif // LEAVR_TYPES_H