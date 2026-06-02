/**
 * @file leavr_interfaces.h
 * @brief LEAVR 执法记录仪 - 核心 C++ 抽象接口
 * @version 1.0
 */

#ifndef LEAVR_INTERFACES_H
#define LEAVR_INTERFACES_H

#include "leavr_types.h"
#include "leavr_errors.h"
#include <functional>
#include <vector>
#include <string>

namespace leavr {

/* ================================================================
 * IMediaPipeline - 媒体管线抽象接口
 * ================================================================ */
class IMediaPipeline {
public:
    virtual ~IMediaPipeline() = default;

    virtual int Init(const RecordConfig& cfg) = 0;
    virtual int Start() = 0;
    virtual int Pause() = 0;
    virtual int Resume() = 0;
    virtual int Stop() = 0;

    virtual int GetMainStream(VencStream& stream, int timeout_ms) = 0;
    virtual int GetSubStream(VencStream& stream, int timeout_ms) = 0;
    virtual int ReleaseStream(VencStream& stream) = 0;

    virtual int SetOsd(const OsdConfig& cfg) = 0;
    virtual int CaptureJpeg(const char* path, const JpegConfig& cfg) = 0;

    virtual float GetActualFps() = 0;
    virtual int GetBitrate() = 0;
};

/* ================================================================
 * IEisProcessor - EIS 防抖处理器接口
 * ================================================================ */
class IEisProcessor {
public:
    virtual ~IEisProcessor() = default;

    virtual int Init(int input_w, int input_h, int output_w, int output_h) = 0;
    virtual int PushGyroData(const EisFrameData& data) = 0;
    virtual EisCropWindow GetCropWindow() = 0;
    virtual int Start() = 0;
    virtual int Stop() = 0;
    virtual int SetStrength(int level) = 0;  // 1-100
    virtual int Calibrate() = 0;
};

/* ================================================================
 * IStorageManager - 存储管理接口
 * ================================================================ */
class IStorageManager {
public:
    virtual ~IStorageManager() = default;

    virtual int Init(const char* mount_point) = 0;
    virtual uint64_t GetFreeSpace() = 0;
    virtual uint64_t GetTotalSpace() = 0;
    virtual bool IsCardPresent() = 0;

    virtual int CreateFile(const char* path) = 0;       // returns fd
    virtual int Write(int fd, const void* buf, size_t len) = 0;
    virtual int Read(int fd, void* buf, size_t len, off_t offset) = 0;
    virtual int CloseFile(int fd) = 0;

    virtual int GenerateVideoPath(char* path, size_t len) = 0;
    virtual int GeneratePhotoPath(char* path, size_t len) = 0;
    virtual int GenerateAudioPath(char* path, size_t len) = 0;

    virtual int RecycleOldestFile() = 0;
    virtual bool IsFileUploaded(const char* path) = 0;
    virtual int MarkUploaded(const char* path) = 0;
    virtual int GetPendingUploads(std::vector<std::string>& files) = 0;
    virtual int GetLockedFileCount() = 0;
};

/* ================================================================
 * IEncryptor - 加密模块接口
 * ================================================================ */
class IEncryptor {
public:
    virtual ~IEncryptor() = default;

    virtual int Init(CryptoAlgorithm algo, const uint8_t* key, size_t key_len) = 0;
    virtual int SetIV(const uint8_t* iv, size_t iv_len) = 0;
    virtual int GetIV(uint8_t* iv, size_t* iv_len) = 0;

    virtual int EncryptFrame(const uint8_t* in, size_t in_len,
                              uint8_t* out, size_t* out_len) = 0;
    virtual int DecryptFrame(const uint8_t* in, size_t in_len,
                              uint8_t* out, size_t* out_len) = 0;

    virtual int ComputeHmac(const char* file_path, const uint8_t* key,
                             size_t key_len, uint8_t* hmac_out) = 0;
    virtual bool VerifyHmac(const char* file_path, const uint8_t* key,
                             size_t key_len, const uint8_t* hmac) = 0;
    virtual int ComputeSha256(const char* file_path, uint8_t* hash_out) = 0;
};

/* ================================================================
 * IKeyManager - 密钥管理接口
 * ================================================================ */
class IKeyManager {
public:
    virtual ~IKeyManager() = default;

    virtual int LoadDeviceRootKey() = 0;
    virtual int DeriveVideoKey(uint8_t* key, size_t key_len) = 0;
    virtual int DeriveFileKey(uint64_t timestamp, uint8_t* key, size_t key_len) = 0;
    virtual int DeriveAuthKey(uint8_t* key, size_t key_len) = 0;
    virtual bool IsRootKeyValid() = 0;
};

/* ================================================================
 * IStreamServer - 网络推流接口
 * ================================================================ */
class IStreamServer {
public:
    virtual ~IStreamServer() = default;

    using OnClientChange = std::function<void(int client_count)>;
    using OnError = std::function<void(const char* error)>;

    virtual int StartRtspServer(int port, bool auth_enabled,
                                 const char* username, const char* password) = 0;
    virtual int StopRtspServer() = 0;

    virtual int StartRtmpPush(const char* url) = 0;
    virtual int StopRtmpPush() = 0;

    virtual int StartOnvifService() = 0;
    virtual int StopOnvifService() = 0;

    virtual int StartGb28181(const Gb28181Config& cfg) = 0;
    virtual int StopGb28181() = 0;

    virtual int PushVideoFrame(const uint8_t* data, size_t len,
                                uint64_t pts, bool is_key) = 0;
    virtual int PushAudioFrame(const uint8_t* data, size_t len, uint64_t pts) = 0;

    virtual int StopAll() = 0;
};

/* ================================================================
 * INetworkManager - 网络管理接口
 * ================================================================ */
class INetworkManager {
public:
    virtual ~INetworkManager() = default;

    using OnNetworkChange = std::function<void(NetworkType type, bool connected)>;

    virtual int Init(OnNetworkChange callback) = 0;
    virtual int ConnectWiFi(const char* ssid, const char* password) = 0;
    virtual int DisconnectWiFi() = 0;
    virtual int Dial5G() = 0;
    virtual int Hangup5G() = 0;
    virtual NetworkType GetActiveNetwork() = 0;
    virtual int GetSignalStrength() = 0;  // 0-100
    virtual bool IsConnected() = 0;
    virtual int GetUploadBandwidthKbps() = 0;
};

/* ================================================================
 * IIcm20948Hal - ICM-20948 陀螺仪 HAL 接口
 * ================================================================ */
class IIcm20948Hal {
public:
    virtual ~IIcm20948Hal() = default;

    virtual int Init(const char* i2c_dev, uint8_t addr) = 0;
    virtual int SetOdr(uint16_t acc_odr_hz, uint16_t gyro_odr_hz) = 0;
    virtual int SetAccelRange(int g) = 0;     // 2/4/8/16
    virtual int SetGyroRange(int dps) = 0;    // 250/500/1000/2000
    virtual int Start() = 0;
    virtual int Stop() = 0;
    virtual int ReadData(EisFrameData* data, int timeout_ms) = 0;
    virtual int Calibrate() = 0;
    virtual bool SelfTest() = 0;
};

/* ================================================================
 * IGpsHal - GPS HAL 接口
 * ================================================================ */
class IGpsHal {
public:
    virtual ~IGpsHal() = default;

    virtual int Init(const char* uart_dev, int baudrate) = 0;
    virtual int Start() = 0;
    virtual int Stop() = 0;
    virtual int GetPosition(GpsPosition* pos) = 0;
    virtual bool IsFixed() = 0;
    virtual int GetSatelliteCount() = 0;
};

/* ================================================================
 * IBatteryHal - 电池 HAL 接口
 * ================================================================ */
class IBatteryHal {
public:
    virtual ~IBatteryHal() = default;

    virtual int Init() = 0;
    virtual int GetLevelPercent() = 0;     // 0-100
    virtual int GetVoltage() = 0;          // mV
    virtual bool IsCharging() = 0;
    virtual bool IsUsbConnected() = 0;
    virtual bool IsLowBattery() = 0;       // < 15%
    virtual bool IsCriticalBattery() = 0;  // < 5%
};

/* ================================================================
 * IKeyHal - 按键 HAL 接口
 * ================================================================ */
class IKeyHal {
public:
    virtual ~IKeyHal() = default;

    using KeyCallback = std::function<void(KeyCode code, KeyEventType event)>;

    virtual int Init() = 0;
    virtual int RegisterCallback(KeyCallback callback) = 0;
    virtual int SetLongPressMs(int ms) = 0;  // default 800ms
};

/* ================================================================
 * IConfigParser - 配置解析接口
 * ================================================================ */
class IConfigParser {
public:
    virtual ~IConfigParser() = default;

    virtual int Load(const char* file_path) = 0;
    virtual int Save(const char* file_path) = 0;

    virtual const char* GetString(const char* section, const char* key,
                                   const char* default_val) = 0;
    virtual int GetInt(const char* section, const char* key, int default_val) = 0;
    virtual bool GetBool(const char* section, const char* key, bool default_val) = 0;
    virtual double GetDouble(const char* section, const char* key, double default_val) = 0;

    virtual int SetString(const char* section, const char* key, const char* value) = 0;
    virtual int SetInt(const char* section, const char* key, int value) = 0;
    virtual int SetBool(const char* section, const char* key, bool value) = 0;
};

/* ================================================================
 * ILogger - 日志接口
 * ================================================================ */
class ILogger {
public:
    enum Level { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3, FATAL = 4 };

    virtual ~ILogger() = default;

    virtual int Init(Level min_level, const char* log_dir, int max_size_mb) = 0;
    virtual void Log(Level level, const char* file, int line,
                      const char* func, const char* fmt, ...) = 0;
    virtual void Flush() = 0;
};

/* ================================================================
 * ITimer - 定时器接口
 * ================================================================ */
class ITimer {
public:
    using TimerCallback = std::function<void()>;

    virtual ~ITimer() = default;

    virtual int Create(const char* name, int interval_ms, bool repeat,
                        TimerCallback callback) = 0;  // returns timer_id
    virtual int Start(int timer_id) = 0;
    virtual int Stop(int timer_id) = 0;
    virtual int Destroy(int timer_id) = 0;
    virtual bool IsRunning(int timer_id) = 0;
    virtual void SleepMs(int ms) = 0;
};

/* ================================================================
 * ISystemMonitor - 系统监测接口
 * ================================================================ */
class ISystemMonitor {
public:
    virtual ~ISystemMonitor() = default;

    virtual SystemStatus GetStatus() = 0;
    virtual float GetCpuTemperature() = 0;
    virtual float GetCpuUsagePercent() = 0;
    virtual int GetMemoryFreeKb() = 0;
};

} // namespace leavr

#endif // LEAVR_INTERFACES_H