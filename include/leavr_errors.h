/**
 * @file leavr_errors.h
 * @brief LEAVR 执法记录仪 - 错误码定义
 * @version 1.0
 */

#ifndef LEAVR_ERRORS_H
#define LEAVR_ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 错误码
 * ================================================================ */

/**
 * @brief LEAVR 统一错误码枚举
 *
 * 错误码按模块分段，每个模块占用 100 的范围，便于快速定位问题来源。
 * - 负值表示错误，0 表示成功。
 * - 新增错误码请遵循现有分段规则，并在 leavr_strerror() 中添加对应字符串。
 */
enum LeavrError {
    LEAVR_OK                    = 0,    /**< 操作成功 */

    /* ---------- 通用错误 (-1 ~ -99) ---------- */
    LEAVR_ERR_PARAM             = -1,   /**< 无效参数 */
    LEAVR_ERR_MEMORY            = -2,   /**< 内存不足 */
    LEAVR_ERR_TIMEOUT           = -3,   /**< 操作超时 */
    LEAVR_ERR_NOT_INIT          = -4,   /**< 模块未初始化 */
    LEAVR_ERR_STATE             = -5,   /**< 状态错误 (当前状态下不允许该操作) */
    LEAVR_ERR_NOT_SUPPORTED     = -6,   /**< 功能不支持 */
    LEAVR_ERR_BUSY              = -7,   /**< 资源忙 */
    LEAVR_ERR_IO                = -8,   /**< I/O 错误 */

    /* ---------- 媒体错误 (-100 ~ -199) ---------- */
    LEAVR_ERR_VI_INIT           = -100, /**< VI (视频输入) 初始化失败 */
    LEAVR_ERR_VI_STREAM         = -101, /**< VI 流异常 */
    LEAVR_ERR_VPSS_INIT         = -110, /**< VPSS (视频处理子系统) 初始化失败 */
    LEAVR_ERR_VPSS_CROP         = -111, /**< VPSS 裁剪操作失败 */
    LEAVR_ERR_VENC_INIT         = -120, /**< VENC (视频编码) 初始化失败 */
    LEAVR_ERR_VENC_STREAM       = -121, /**< VENC 编码流错误 */
    LEAVR_ERR_VDEC_INIT         = -130, /**< VDEC (视频解码) 初始化失败 */
    LEAVR_ERR_AI_INIT           = -140, /**< AI (音频输入) 初始化失败 */
    LEAVR_ERR_AENC_INIT         = -141, /**< AENC (音频编码) 初始化失败 */
    LEAVR_ERR_AVS_INIT          = -150, /**< AVS (音视频复用) 初始化失败 */
    LEAVR_ERR_AVS_MUX           = -151, /**< AVS 复用写入错误 */
    LEAVR_ERR_EIS_INIT          = -160, /**< EIS (电子防抖) 初始化失败 */
    LEAVR_ERR_EIS_CALC          = -161, /**< EIS 计算错误 */
    LEAVR_ERR_OSD_INIT          = -170, /**< OSD (屏幕叠加) 初始化失败 */

    /* ---------- 存储错误 (-200 ~ -299) ---------- */
    LEAVR_ERR_SD_NOT_FOUND      = -200, /**< SD 卡未检测到 */
    LEAVR_ERR_SD_FULL           = -201, /**< SD 卡已满 */
    LEAVR_ERR_FILE_OPEN         = -210, /**< 文件打开失败 */
    LEAVR_ERR_FILE_WRITE        = -211, /**< 文件写入失败 */
    LEAVR_ERR_FILE_READ         = -212, /**< 文件读取失败 */
    LEAVR_ERR_FILE_CLOSE        = -213, /**< 文件关闭失败 */
    LEAVR_ERR_DB_INIT           = -220, /**< 数据库初始化失败 */
    LEAVR_ERR_DB_QUERY          = -221, /**< 数据库查询失败 */

    /* ---------- 网络错误 (-300 ~ -399) ---------- */
    LEAVR_ERR_NET_CONNECT       = -300, /**< 网络连接失败 */
    LEAVR_ERR_NET_TIMEOUT       = -301, /**< 网络超时 */
    LEAVR_ERR_NET_DISCONNECT    = -302, /**< 网络断开 */
    LEAVR_ERR_RTSP_INIT         = -310, /**< RTSP 服务初始化失败 */
    LEAVR_ERR_RTSP_STREAM       = -311, /**< RTSP 推流错误 */
    LEAVR_ERR_RTMP_INIT         = -320, /**< RTMP 客户端初始化失败 */
    LEAVR_ERR_RTMP_PUSH         = -321, /**< RTMP 推流失败 */
    LEAVR_ERR_ONVIF_INIT        = -330, /**< ONVIF 协议栈初始化失败 */
    LEAVR_ERR_GB28181_INIT      = -340, /**< GB/T 28181 协议栈初始化失败 */
    LEAVR_ERR_GB28181_REGISTER  = -341, /**< GB/T 28181 注册失败 */

    /* ---------- 安全错误 (-400 ~ -499) ---------- */
    LEAVR_ERR_ENCRYPT_INIT      = -400, /**< 加密模块初始化失败 */
    LEAVR_ERR_ENCRYPT_FAILED    = -401, /**< 加密操作失败 */
    LEAVR_ERR_DECRYPT_FAILED    = -402, /**< 解密操作失败 */
    LEAVR_ERR_SIGN_FAILED       = -410, /**< 数字签名失败 */
    LEAVR_ERR_VERIFY_FAILED     = -411, /**< 签名验证失败 */
    LEAVR_ERR_KEY_DERIVE        = -420, /**< 密钥派生失败 */

    /* ---------- 硬件错误 (-500 ~ -599) ---------- */
    LEAVR_ERR_SENSOR_INIT       = -500, /**< 图像传感器初始化失败 */
    LEAVR_ERR_GPS_NO_FIX        = -510, /**< GPS 未定位 */
    LEAVR_ERR_BATTERY_CRITICAL  = -520, /**< 电池电量严重不足 */
    LEAVR_ERR_OVER_TEMP         = -530, /**< 设备过温 */
    LEAVR_ERR_I2C_FAILED        = -540, /**< I2C 通信错误 */
    LEAVR_ERR_SPI_FAILED        = -541, /**< SPI 通信错误 */
};

/**
 * @brief 获取错误码对应的字符串描述
 */
static inline const char* leavr_strerror(int err) {
    switch (err) {
        case LEAVR_OK:                  return "Success";
        case LEAVR_ERR_PARAM:           return "Invalid parameter";
        case LEAVR_ERR_MEMORY:          return "Out of memory";
        case LEAVR_ERR_TIMEOUT:         return "Timeout";
        case LEAVR_ERR_NOT_INIT:        return "Not initialized";
        case LEAVR_ERR_STATE:           return "Invalid state";
        case LEAVR_ERR_NOT_SUPPORTED:   return "Not supported";
        case LEAVR_ERR_BUSY:            return "Resource busy";
        case LEAVR_ERR_IO:              return "I/O error";
        case LEAVR_ERR_VI_INIT:         return "VI init failed";
        case LEAVR_ERR_VI_STREAM:       return "VI stream error";
        case LEAVR_ERR_VPSS_INIT:       return "VPSS init failed";
        case LEAVR_ERR_VPSS_CROP:       return "VPSS crop failed";
        case LEAVR_ERR_VENC_INIT:       return "VENC init failed";
        case LEAVR_ERR_VENC_STREAM:     return "VENC stream error";
        case LEAVR_ERR_VDEC_INIT:       return "VDEC init failed";
        case LEAVR_ERR_AI_INIT:         return "Audio input init failed";
        case LEAVR_ERR_AENC_INIT:       return "Audio encoder init failed";
        case LEAVR_ERR_AVS_INIT:        return "AVS muxer init failed";
        case LEAVR_ERR_AVS_MUX:         return "AVS mux failed";
        case LEAVR_ERR_EIS_INIT:        return "EIS init failed";
        case LEAVR_ERR_EIS_CALC:        return "EIS calculation error";
        case LEAVR_ERR_OSD_INIT:        return "OSD init failed";
        case LEAVR_ERR_SD_NOT_FOUND:    return "SD card not found";
        case LEAVR_ERR_SD_FULL:         return "SD card full";
        case LEAVR_ERR_FILE_OPEN:       return "File open failed";
        case LEAVR_ERR_FILE_WRITE:      return "File write failed";
        case LEAVR_ERR_FILE_READ:       return "File read failed";
        case LEAVR_ERR_FILE_CLOSE:      return "File close failed";
        case LEAVR_ERR_DB_INIT:         return "Database init failed";
        case LEAVR_ERR_DB_QUERY:        return "Database query failed";
        case LEAVR_ERR_NET_CONNECT:     return "Network connect failed";
        case LEAVR_ERR_NET_TIMEOUT:     return "Network timeout";
        case LEAVR_ERR_NET_DISCONNECT:  return "Network disconnected";
        case LEAVR_ERR_RTSP_INIT:       return "RTSP server init failed";
        case LEAVR_ERR_RTSP_STREAM:     return "RTSP stream error";
        case LEAVR_ERR_RTMP_INIT:       return "RTMP client init failed";
        case LEAVR_ERR_RTMP_PUSH:       return "RTMP push failed";
        case LEAVR_ERR_ONVIF_INIT:      return "ONVIF init failed";
        case LEAVR_ERR_GB28181_INIT:    return "GB28181 init failed";
        case LEAVR_ERR_GB28181_REGISTER:return "GB28181 register failed";
        case LEAVR_ERR_ENCRYPT_INIT:    return "Encryptor init failed";
        case LEAVR_ERR_ENCRYPT_FAILED:  return "Encryption failed";
        case LEAVR_ERR_DECRYPT_FAILED:  return "Decryption failed";
        case LEAVR_ERR_SIGN_FAILED:     return "Signature failed";
        case LEAVR_ERR_VERIFY_FAILED:   return "Verification failed";
        case LEAVR_ERR_KEY_DERIVE:      return "Key derivation failed";
        case LEAVR_ERR_SENSOR_INIT:     return "Sensor init failed";
        case LEAVR_ERR_GPS_NO_FIX:      return "GPS no fix";
        case LEAVR_ERR_BATTERY_CRITICAL:return "Battery critical";
        case LEAVR_ERR_OVER_TEMP:       return "Over temperature";
        case LEAVR_ERR_I2C_FAILED:      return "I2C error";
        case LEAVR_ERR_SPI_FAILED:      return "SPI error";
        default:                        return "Unknown error";
    }
}

#ifdef __cplusplus
}
#endif

#endif // LEAVR_ERRORS_H