# 5G/WiFi 联网执法记录仪 — 软件架构与项目计划

> 版本: v2.0  
> 平台: HiSilicon HI3516 (ARM Cortex-A7)  
> 系统: Linux 4.9 + HiMPP  
> 传感器: Sony IMX586 (48MP, 4K)  
> 语言: C/C++  
> 构建: CMake + arm-himix-linux-gcc

---

## 目录

1. [项目概述](#1-项目概述)
2. [硬件平台与关键器件](#2-硬件平台与关键器件)
3. [总体软件架构分层](#3-总体软件架构分层)
4. [系统移植计划](#4-系统移植计划)
5. [驱动移植计划](#5-驱动移植计划)
6. [进程与线程模型](#6-进程与线程模型)
7. [核心模块详细设计](#7-核心模块详细设计)
   - [7.1 主状态机](#71-主状态机-top-level-state-machine)
   - [7.2 录像管线（含陀螺仪防抖 EIS）](#72-录像管线含陀螺仪防抖-eis)
   - [7.3 多分辨率/帧率录像模式](#73-多分辨率帧率录像模式)
   - [7.4 高级录像功能](#74-高级录像功能预录延录分段暂停定时)
   - [7.5 拍照管线（最高 4800W 像素）](#75-拍照管线最高-4800w-像素)
   - [7.6 OSD 信息叠加](#76-osd-信息叠加)
   - [7.7 音频记录](#77-音频记录-wavmp3)
   - [7.8 网络传输架构](#78-网络传输架构)
   - [7.9 存储管理](#79-存储管理)
   - [7.10 安全加密方案](#710-安全加密方案)
8. [关键数据流](#8-关键数据流)
9. [系统配置参数](#9-系统配置参数)
10. [核心接口定义](#10-核心接口定义-c-抽象)
11. [编译与构建系统](#11-编译与构建系统)
12. [线程间通信机制](#12-线程间通信机制)
13. [项目源码结构](#13-项目源码结构)
14. [项目实施计划与里程碑](#14-项目实施计划与里程碑)

---

## 1. 项目概述

### 1.1 项目目标

打造具备**高清音视频采集、5G/WiFi 多网络传输及陀螺仪智能防抖**等功能的执法记录仪，满足执法场景下对记录设备的高效、稳定、智能化需求。

### 1.2 核心功能清单

| 功能域 | 功能项 | 规格说明 |
|--------|--------|----------|
| **影像记录** | 多分辨率录像 | 4K@20fps / 6M(3072×2048)@30fps / 1080p@60fps |
| | 编码格式 | H.264 / H.265，MP4 封装 |
| | 电子防抖 (EIS) | ICM-20948 陀螺仪数据 + ISP 裁剪稳像 |
| | 高级录像 | 预录 / 延录 / 分段 / 暂停 / 定时录像 |
| | OSD 叠加 | 时间戳、设备ID、警员编号、GPS、实时网速 |
| **拍照** | 高像素拍照 | 最高 4800W 像素 (8000×6000)，JPEG 输出 |
| | 连拍 | 支持 3/5/10 张连拍 |
| **音频记录** | 录音格式 | WAV (PCM 无损) / MP3 (压缩) 可选 |
| | 采样率 | 8KHz / 16KHz / 48KHz 可选 |
| **网络传输** | 5G/WiFi 联网 | 5G 模组 (USB) + WiFi 模组 (SDIO) |
| | 推流协议 | RTSP / RTMP |
| | 对接标准 | ONVIF Profile S/G、GB/T 28181 |
| | 远程回放 | RTSP 拉流回放本地录像文件 |
| **外设与交互** | 屏幕 | SPI LCD 彩色屏 + 触摸 / 按键操作 |
| | 传感器 | ICM-20948 9 轴陀螺仪 (I2C) |

### 1.3 项目架构全景图

```
┌──────────────────────────────────────────────────────────────────┐
│                        执法记录仪系统                               │
│                                                                   │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐ │
│  │  IMX586  │  │ICM-20948│  │ 5G 模组  │  │  WiFi 模组       │ │
│  │  Sensor  │  │ 陀螺仪   │  │  (USB)   │  │  (SDIO)         │ │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────────┬─────────┘ │
│       │ MIPI        │ I2C         │ USB 2.0/3.0       │ SDIO     │
│  ┌────┴─────────────┴─────────────┴───────────────────┴─────────┐ │
│  │                   HI3516 SoC                                  │ │
│  │   ISP → VI → VPSS → VENC/JPEG → AVS → Storage/Network        │ │
│  │   Audio Codec → AI → AENC → AVS                              │ │
│  └───────────────────────────────────────────────────────────────┘ │
│       │ MIPI/SPI    │ I2S       │ GPIO       │ UART               │
│  ┌────┴─────┐  ┌────┴─────┐  ┌───┴────┐  ┌───┴────────┐         │
│  │ SPI LCD  │  │ MIC/SPK  │  │按键/LED│  │ GPS / 串口  │         │
│  │  屏幕    │  │ 音频     │  │        │  │  外设       │         │
│  └──────────┘  └──────────┘  └────────┘  └────────────┘         │
│                                                                   │
│  软件栈:                                                          │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │  应用层: 录像/拍照/回放/设置/网络/安全/OSD/EIS 防抖           │ │
│  ├─────────────────────────────────────────────────────────────┤ │
│  │  框架层: MediaPipeline / StreamManager / StorageManager      │ │
│  ├─────────────────────────────────────────────────────────────┤ │
│  │  系统层: Linux 4.9 + HiMPP + 驱动 (USB/SDIO/I2C/SPI/MIPI)   │ │
│  └─────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
```

---

## 2. 硬件平台与关键器件

| 器件 | 型号 | 接口 | 关键参数 |
|------|------|------|----------|
| **SoC** | HiSilicon HI3516 | - | Cortex-A7, ISP, H.265 编码, 最高 4K |
| **图像传感器** | Sony IMX586 | MIPI CSI-2 4-lane | 48MP, 4K@30fps, 1080p@60fps, 支持 WDR |
| **陀螺仪** | ICM-20948 | I2C (地址 0x68/0x69) | 9 轴 (ACC+GYRO+MAG), 最高 1.125KHz ODR |
| **5G 模组** | 高通/展锐方案 | USB 2.0/3.0 | 5G NR SA/NSA, USB 透传 (RNDIS/ECM) |
| **WiFi 模组** | AP6256/AP6275 等 | SDIO 3.0 | 802.11ac, 2.4G/5G 双频 |
| **屏幕** | 2.0" 彩色 LCD | SPI (4-wire) | 320×240 或 480×360 分辨率 |
| **GPS** | L76K 或类似 | UART | 支持 BDS+GPS 双模 |
| **音频** | 内置 Audio Codec | I2S / 模拟 MIC | MEMS MIC + D 类功放 SPK |

---

## 3. 总体软件架构分层

```
┌──────────────────────────────────────────────────────────────┐
│                    【第5层】应用业务层                          │
│  录像控制 │ 拍照控制 │ 回放管理 │ 系统设置 │ 日志管理 │ OTA 升级 │
│  EIS 防抖 │ 预录管理 │ 延录管理 │ 分段录像 │ 暂停录像 │ 定时录像 │
│  红外控制 │ 电池管理 │ LED指示  │ 按键处理 │ 5G/WiFi 切换        │
├──────────────────────────────────────────────────────────────┤
│                    【第4层】网络服务层                          │
│  RTSP Server │ RTMP Client │ ONVIF Service │ GB/T 28181 SIP   │
│  HTTP Upload │ MQTT Client │ WebSocket (配置/调试)              │
│  5G Manager  │ WiFi Manager (双网络自动切换)                    │
├──────────────────────────────────────────────────────────────┤
│                    【第3层】安全/存储层                          │
│  文件加密(AES-256) │ 数字签名(HMAC-SHA256) │ 防篡改校验         │
│  存储管理(FAT32/exFAT) │ 循环覆盖 │ SQLite 文件索引             │
├──────────────────────────────────────────────────────────────┤
│                    【第2层】媒体框架层                          │
│  ┌───────────────────────────────────────────────────────┐   │
│  │           MediaPipeline (媒体管线管理器)                 │   │
│  │  VI → VPSS → ┬→ VENC(H.264/H.265) → AVS(Mux MP4)       │   │
│  │              ├→ JPEG Enc (拍照) → File                  │   │
│  │              ├→ EIS Processor (陀螺仪防抖)              │   │
│  │              └→ Region OSD (时间/ID/GPS 叠加)           │   │
│  │  AI → AENC (AAC/PCM/MP3) → AVS / File                  │   │
│  │  VDEC ← File (回放解码)                                 │   │
│  └───────────────────────────────────────────────────────┘   │
├──────────────────────────────────────────────────────────────┤
│                    【第1层】硬件抽象层 (HAL)                    │
│  IMX586 HAL │ ICM-20948 HAL │ Audio HAL │ 5G HAL │ WiFi HAL   │
│  SPI LCD HAL│ Key HAL       │ LED HAL   │ GPS HAL │ IR-CUT HAL│
├──────────────────────────────────────────────────────────────┤
│                    【第0层】OS & BSP                            │
│  Linux 4.9 Kernel │ HiSilicon MPI (mpp)                       │
│  驱动: MIPI/SDIO/USB/I2C/SPI/UART/I2S/GPIO/ADC               │
└──────────────────────────────────────────────────────────────┘
```

---

## 4. 系统移植计划

### 4.1 U-Boot 移植

| 任务 | 详细内容 | 工作量 |
|------|----------|--------|
| 交叉编译环境搭建 | arm-himix-linux-gcc 工具链安装与配置 | 0.5 天 |
| U-Boot 源码获取 | 海思 SDK 配套 U-Boot 源码 (u-boot-2016.11+) | 0.5 天 |
| DDR 参数配置 | 适配 DDR3/DDR4 参数 (速率、时序、容量) | 1 天 |
| 外设初始化 | 配置 UART 调试串口、eMMC/SD 卡、SPI Flash 启动 | 2 天 |
| Fastboot/Recovery | 支持 USB 烧录、SD 卡升级 | 1 天 |
| 启动优化 | 静默启动、快速启动 (关闭无用初始化) | 1 天 |

### 4.2 Linux 4.9 内核移植

| 任务 | 详细内容 | 工作量 |
|------|----------|--------|
| 内核源码配置 | 海思 SDK 内核 (linux-4.9.y)，使能 HI3516 平台 defconfig | 1 天 |
| MIPI CSI 驱动 | IMX586 Sensor 驱动移植 (MIPI 4-lane, I2C 配置) | 3 天 |
| ISP 适配 | IMX586 3A 算法参数调优 (AE/AWB/AF)、WDR 配置 | 3 天 |
| SDIO WiFi 驱动 | AP6256/AP6275 驱动移植 (bcmdhd/或开源方案) | 2 天 |
| USB 5G 模组 | USB RNDIS/ECM 驱动，PPP 拨号脚本 | 2 天 |
| I2C 驱动 | ICM-20948 陀螺仪驱动移植 | 1.5 天 |
| SPI LCD 驱动 | 屏幕驱动移植 (framebuffer) | 1.5 天 |
| Audio Codec | I2S/PCM 音频驱动，MIC BIAS，SPK PA 控制 | 1.5 天 |
| 其他外设 | GPIO 按键、LED PWM、GPS UART、RTC | 1.5 天 |

### 4.3 文件系统构建

| 任务 | 详细内容 | 工作量 |
|------|----------|--------|
| 根文件系统 | BusyBox 构建 + 必要系统库 (glibc/musl) | 1 天 |
| 系统库 | 海思 mpp 库、live555、SQLite3、mbedTLS、LVGL、cJSON、libmp4v2 | 2 天 |
| 启动脚本 | rcS 初始化、SD 卡挂载、网络配置、应用自启动 | 1 天 |
| OTA 升级 | 双分区 A/B 升级方案 + recovery 模式 | 2 天 |

---

## 5. 驱动移植计划

### 5.1 5G 模组 USB 透传驱动

```
5G 模组 ──USB 2.0/3.0──▶ HI3516 USB Host Controller
                              │
                    ┌─────────┼─────────┐
                    ▼         ▼         ▼
              USB RNDIS   USB ECM   USB ACM
              (网络透传)  (网络透传) (AT指令)
                    │         │         │
                    ▼         ▼         ▼
              usb0 网卡   usb0 网卡   ttyUSB0
                    │         │         │
                    └────┬────┘         │
                         ▼              ▼
                   dhcp/ppp        AT 指令
                   拨号上网        模组管理
```

### 5.2 WiFi (SDIO) 驱动移植

```
WiFi 模组 ──SDIO 3.0──▶ HI3516 SDIO Controller
                              │
                              ▼
                      bcmdhd.ko / 开源驱动
                              │
                    设备树配置 (DTS)
                    - SDIO bus width: 4-bit
                    - 时钟频率: 50MHz / 100MHz
                    - GPIO: WL_REG_ON, WL_HOST_WAKE
                    - 晶振: 37.4MHz / 26MHz
                              │
                              ▼
                      wpa_supplicant
                      (STA / AP 模式)
```

### 5.3 ICM-20948 陀螺仪 (I2C) 驱动

```
HI3516 I2C1 ──▶ ICM-20948 (地址: 0x68)

设备树配置:
  icm20948@68 {
    compatible = "invensense,icm20948";
    reg = <0x68>;
    interrupt-parent = <&gpio>;
    interrupts = <12 IRQ_TYPE_EDGE_RISING>;  // INT 引脚
  };

驱动功能:
  - 初始化: 设置 ACC ±16G, GYRO ±2000dps, ODR 200Hz
  - 数据读取: 通过 I2C burst read 读取 12 字节 (ACC×3 + GYRO×3 各 2 字节)
  - FIFO 模式: 使用 512 字节 FIFO 减少 I2C 读取次数
  - 暴露接口: /sys/class/iio/ 或自定义字符设备 /dev/icm20948
  - 数据格式: float[3] gyro (rad/s) + float[3] accel (m/s²)
```

### 5.4 SPI LCD 屏幕驱动

```
HI3516 SPI0 ──▶ LCD Controller (ST7789 / ILI9341)

设备树配置:
  display: st7789v@0 {
    compatible = "sitronix,st7789v";
    reg = <0>;
    spi-max-frequency = <48000000>;
    dc-gpios = <&gpio 25 GPIO_ACTIVE_HIGH>;
    reset-gpios = <&gpio 24 GPIO_ACTIVE_LOW>;
    backlight-gpios = <&gpio 26 GPIO_ACTIVE_HIGH>;
    rotate = <90>;
    width = <240>;
    height = <320>;
  };

驱动功能:
  - framebuffer 接口 (/dev/fb0)
  - 支持 16bpp (RGB565) / 18bpp
  - LVGL 可直接操作 fb dev
  - 背光 PWM 调节
```

---

## 6. 进程与线程模型

采用**单进程多线程**架构：

```
进程: leavr_app

线程列表:
├── main_thread             (主线程: 事件循环 + 状态机调度)
├── media_record_thread     (录像线程: VI→EIS→VPSS→VENC→AVS→文件写入)
├── media_capture_thread    (拍照线程: VI→VPSS→JPEG→文件)
├── media_playback_thread   (回放线程: 文件→VDEC→VPSS→VO)
├── audio_capture_thread    (音频采集: AI→AENC→AVS/文件)
├── eis_processor_thread    (EIS 防抖: ICM-20948 数据→ISP 裁剪参数计算)
├── net_rtsp_thread         (RTSP Server 线程池, 4 线程)
├── net_rtmp_thread         (RTMP 推流线程)
├── net_onvif_thread        (ONVIF 服务线程)
├── net_gb28181_thread      (GB28181 SIP 注册/保活/推流线程)
├── net_upload_thread       (HTTP 文件上传线程池, 2 线程)
├── net_manager_thread      (5G/WiFi 连接管理与切换)
├── gps_parse_thread        (GPS NMEA 解析线程)
├── sensor_monitor_thread   (陀螺仪/电池/温度监测)
├── key_event_thread        (按键事件检测)
├── storage_monitor_thread  (SD 卡空间监测 + 循环覆盖)
├── watchdog_thread         (看门狗)
└── usb_service_thread      (USB UVC/MTP 模式管理)
```

### 线程优先级

| 线程 | 优先级 (SCHED_FIFO) | 说明 |
|------|---------------------|------|
| eis_processor_thread | 95 | EIS 防抖实时性要求最高 |
| media_record_thread | 90 | 录像核心管线 |
| audio_capture_thread | 85 | 音视频同步 |
| net_rtsp_thread | 80 | 实时推流 |
| media_capture_thread | 75 | 拍照管线 |
| watchdog_thread | 99 | 看门狗守护 |

---

## 7. 核心模块详细设计

### 7.1 主状态机 (Top-Level State Machine)

```
              ┌──────────┐
              │   BOOT   │ 开机初始化
              └────┬─────┘
                   ▼
              ┌──────────┐
         ┌───▶│ STANDBY  │◀──── 待机(预览+OSD)
         │    └──┬───┬───┘
         │       │   │
   停止/返回   录像键 拍照键
         │       │   │
         │       ▼   ▼
         │  ┌──────────┐  ┌──────────┐
         │  │RECORDING │  │CAPTURING │
         │  │ 录像中    │  │ 拍照中    │
         │  └────┬─────┘  └──────────┘
         │       │
         │  ┌────┼────┬────┬────┐
         │  ▼    ▼    ▼    ▼    ▼
         │ PAUSE TIMER SEG OVERRUN
         │ 暂停  定时 分段  超时

         │    ┌──────────┐
         ├───▶│ PLAYBACK │ 回放模式
         │    └──────────┘
         │    ┌──────────┐
         ├───▶│  MENU    │ 菜单模式
         │    └──────────┘
         │    ┌──────────┐
         └───▶│  USB     │ USB 模式 (中断式)
              └──────────┘
```

### 状态转移表

| 当前状态 | 事件 | 下一状态 | 动作 |
|----------|------|----------|------|
| BOOT | INIT_DONE | STANDBY | 开启预览、加载配置、初始化 EIS |
| STANDBY | KEY_RECORD | RECORDING | 启动录像管线、EIS、音频采集 |
| STANDBY | KEY_CAPTURE | CAPTURING | 抓拍一帧 (最高 48MP) |
| STANDBY | KEY_PLAYBACK | PLAYBACK | 进入回放列表 |
| RECORDING | KEY_STOP | STANDBY | 停止管线、写 .idx 签名 |
| RECORDING | KEY_PAUSE | PAUSED | 暂停编码、保持管线 |
| PAUSED | KEY_RESUME | RECORDING | 恢复编码 |
| RECORDING | TIMER_EXPIRE | STANDBY | 定时录像到时、自动停止 |
| RECORDING | SEGMENT_TIMER | RECORDING | 分段写入新文件 |
| RECORDING | LOW_BATTERY | STANDBY | 强制停止保存 |
| RECORDING | OVERRUN_TIMER | RECORDING | 延录计时中 |
| ANY | USB_CONNECT | USB | USB 优先中断 |

### 7.2 录像管线（含陀螺仪防抖 EIS）

```
┌──────────────────────────────────────────────────────────────────────┐
│                录像管线 (Record Pipeline with EIS)                     │
│                                                                       │
│  [IMX586] ──MIPI──▶ [VI Dev 0]                                       │
│                          │                                            │
│                          ▼                                            │
│                     [ISP 处理]                                        │
│                     3A / WDR / 3DNR                                   │
│                          │                                            │
│            ┌─────────────┴─────────────┐                              │
│            │                           │                              │
│            ▼                           ▼                              │
│     [VI Chn 0]                  [VI Chn 1]                            │
│     (全尺寸, 用于 EIS)          (原始直通, 用于子码流)                  │
│            │                           │                              │
│            ▼                           │                              │
│     [VPSS Grp 0]                       │                              │
│     EIS 裁剪 + 缩放                     │                              │
│     ┌──────────────────┐               │                              │
│     │ EIS Processor    │               │                              │
│     │ ┌──────────────┐ │               │                              │
│     │ │ ICM-20948    │ │               │                              │
│     │ │ 陀螺仪数据    │ │               │                              │
│     │ │ gyro[x,y,z]  │ │               │                              │
│     │ └──────┬───────┘ │               │                              │
│     │        ▼         │               │                              │
│     │  Kalman Filter   │               │                              │
│     │  姿态估算        │               │                              │
│     │        ▼         │               │                              │
│     │  偏移量计算      │               │                              │
│     │  (dx, dy, angle) │               │                              │
│     │        ▼         │               │                              │
│     │  VPSS Crop       │               │                              │
│     │  (裁剪+旋转)     │               │                              │
│     └────────┬─────────┘               │                              │
│              │                         │                              │
│              ▼                         ▼                              │
│       [VPSS Chn 0]              [VPSS Chn 1]                          │
│       (主码流 稳像后)            (子码流 D1)                           │
│       4K/6M/1080p              720×576 / 704×576                      │
│              │                         │                              │
│              ▼                         ▼                              │
│       [VENC Chn 0]              [VENC Chn 1]                          │
│       H.265 / H.264             H.264 Baseline                        │
│       CBR / VBR                 CBR 512Kbps                           │
│              │                         │                              │
│              ▼                         │                              │
│       [Region OSD]                    │                              │
│       时间/ID/GPS/网速                │                              │
│              │                         │                              │
│              ▼                         ▼                              │
│       ┌──────────────────────────────────┐                           │
│       │          [AVS Grp 0]             │   [RTSP/RTMP/ONVIF]       │
│       │   MP4 Muxer (视频+音频)          │   子码流网络推流            │
│       └───────────────┬──────────────────┘                           │
│                       │                                              │
│                       ▼                                              │
│               [AES 加密模块]                                         │
│               AES-256-CTR 帧级加密 (可选)                             │
│                       │                                              │
│                       ▼                                              │
│               [File Writer]                                          │
│               → /mnt/sdcard/Video/DEV001_20240602_153000.mp4        │
└──────────────────────────────────────────────────────────────────────┘
```

### EIS 防抖算法流程

```
陀螺仪数据获取:
  1. ICM-20948 以 200Hz ODR 输出角速度 (gyro_x, gyro_y, gyro_z)
  2. 通过 I2C 读取到内存环形缓冲区

数据处理 (每帧触发):
  1. 积分:
     angle_x += gyro_x * dt
     angle_y += gyro_y * dt
  
  2. 卡尔曼滤波:
     - 状态: [angle, bias] (角度 + 陀螺仪偏置)
     - 观测: 角速度积分角度
     - 输出: 平滑后的角度估计
  
  3. 偏移量计算:
     - 根据焦距和像素尺寸, 将角度转换为像素偏移
     - dx = f * tan(angle_y) / pixel_size
     - dy = f * tan(angle_x) / pixel_size
  
  4. VPSS Crop 裁剪:
     - 在原始图像中裁剪出稳定区域
     - 例如: 原始 4000×2250 (6M), 裁出 3840×2160 (4K)
     - 裁剪窗口跟随偏移量移动, 实现稳像
  
  5. 边界处理:
     - 裁剪窗口不能超出原始画面边界
     - 快到边界时做平滑限制 (限幅滤波)
```

### 7.3 多分辨率/帧率录像模式

| 模式 | 分辨率 | 帧率 | 编码 | 码率 (建议) | 适用场景 |
|------|--------|------|------|-------------|----------|
| 模式 1 | 3840×2160 (4K) | 20fps | H.265 | 8 Mbps | 细节取证 |
| 模式 2 | 3072×2048 (6M) | 30fps | H.265 | 10 Mbps | 高清记录 |
| 模式 3 | 1920×1080 (1080p) | 60fps | H.264 | 8 Mbps | 快速运动 |
| 模式 4 | 1920×1080 (1080p) | 30fps | H.265 | 4 Mbps | 常规记录 |
| 模式 5 | 1280×720 (720p) | 30fps | H.264 | 2 Mbps | 省电/省空间 |

**子码流 (用于推流)**: 固定 704×576 (D1) @ 15fps, H.264 Baseline, 512 Kbps

### 像素 Binning/跳过 与 分辨率对应

```
IMX586 原始像素: 8000×6000 (48MP Quad-Bayer)

输出模式:
┌─────────────────────────────────────────────────────┐
│ 4K (3840×2160):                                     │
│   - 使用 Binning 2×2 模式 → 4000×3000               │
│   - VPSS Crop → 3840×2160 (16:9)                    │
│                                                     │
│ 6M (3072×2048):                                     │
│   - 使用 Binning 2×2 → 4000×3000                     │
│   - VPSS Scale → 3072×2048 (3:2)                    │
│                                                     │
│ 1080p (1920×1080):                                  │
│   - 使用 Binning 2×2 → 4000×3000                     │
│   - VPSS Scale → 1920×1080                          │
│                                                     │
│ 4800W 拍照 (8000×6000):                             │
│   - Remosaic (Quad-Bayer → Bayer)                   │
│   - 全像素输出 8000×6000                             │
│   - JPEG Encoder 单帧编码                            │
└─────────────────────────────────────────────────────┘
```

### 7.4 高级录像功能：预录/延录/分段/暂停/定时

#### 7.4.1 预录 (Pre-Record)

```
┌─────────────────────────────────────────────┐
│            预录环形缓冲区 (内存)               │
│                                              │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐ │
│  │GOP│GOP│GOP│GOP│GOP│GOP│...│   │   │   │ │
│  │ 0 │ 1 │ 2 │ 3 │ 4 │ 5 │   │N-2│N-1│ N │ │
│  └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘ │
│    ◄──────────────── write_ptr             │
│                                              │
│  STANDBY 模式下持续运行:                      │
│    VENC 编码 → 写入环形缓冲区 (不落盘)         │
│                                              │
│  录像键按下:                                  │
│    环形缓冲区全部 GOP → 写入 MP4 文件头部      │
│    后续帧 → 继续写入同一文件                  │
│                                              │
│  预录时长可配: 5s / 10s / 15s / 30s           │
│  (N = 预录时长 / GOP 间隔)                    │
└─────────────────────────────────────────────┘
```

#### 7.4.2 延录 (Overrun Record)

```
停止键按下 → 不立即停止 → 继续录像 N 秒 → 自动停止

配置: 延录时长 0s / 5s / 10s / 30s
实现: 停止事件进入延录状态, 延录定时器到期后真正停止
```

#### 7.4.3 分段录像 (Segment)

```
录像过程中每 N 分钟自动分段, 生成新的 MP4 文件

配置:
  - 分段时长: 1min / 3min / 5min / 10min / 关闭
  - 分段时无缝切换 (上一个文件不丢帧, 下一个文件从关键帧开始)

实现:
  1. 定时器触发 → 设置分段标志
  2. 当前 GOP 编码完成 → 刷新 MP4 Buffer
  3. 关闭当前文件 → 写入 .idx 签名 → SQLite 记录
  4. 生成新文件名 → 打开新文件 → 继续编码
  5. 新文件从下一个关键帧开始
```

#### 7.4.4 暂停录像 (Pause)

```
录像中按暂停键 → 暂停编码 → 按继续键 → 恢复编码 (同一文件)

实现:
  1. 暂停: 停止 VENC 取流, 保持管线不销毁
  2. 恢复: 重新启动 VENC 取流, 追加写入同一 MP4 文件
  3. MP4 时间戳需要连续, 中间插入空时间戳段
```

#### 7.4.5 定时录像 (Timer Record)

```
设置开始时间和时长 → 到时间自动开始录像 → 到时长自动停止

实现:
  1. 用户设置: 开始时间 (HH:MM) + 录像时长 (分钟)
  2. RTC 闹钟或定时器检查
  3. 到达开始时间 → 自动进入 RECORDING
  4. 录像时长到期 → 自动停止
```

### 7.5 拍照管线（最高 4800W 像素）

```
┌───────────────────────────────────────────────────────────────┐
│                    拍照管线 (Capture Pipeline)                  │
│                                                                │
│  [IMX586] ──MIPI──▶ [VI Dev 0]                                │
│                                                                │
│  Remosaic 模式 (48MP 全像素):                                   │
│  ① Sensor 配置为 Remosaic 输出 (8000×6000, Bayer)              │
│  ② ISP Remosaic 处理 (Quad-Bayer → 标准 Bayer)                │
│  ③ 3A 统计 (AE/AWB)                                           │
│  ④ 输出 RGB 图像到 VPSS                                       │
│                                                                │
│  VPSS 处理:                                                    │
│  - 缩放/裁剪 (可选)                                            │
│  - 3DNR (数字降噪)                                             │
│  - 锐化增强                                                    │
│                                                                │
│  JPEG Encoder:                                                 │
│  - 硬件 JPEG 编码 (海思 VENC JPEG 模式)                        │
│  - 质量: 可调 70-100%                                          │
│  - EXIF: GPS、时间戳、设备型号                                 │
│                                                                │
│  输出:                                                         │
│  → /mnt/sdcard/Photo/DEV001_20240602_153500.jpg               │
│                                                                │
│  连拍模式:                                                     │
│  - 3 张 / 5 张 / 10 张可选                                     │
│  - 连拍间隔: 200ms (IMX586 15fps@48MP)                         │
│  - 文件名: xxx_001.jpg ~ xxx_010.jpg                           │
└───────────────────────────────────────────────────────────────┘
```

### 7.6 OSD 信息叠加

```
┌─────────────────────────────────────────┐
│ ┌─────────────────────────────────────┐ │
│ │ ● REC [00:15:32]         ████ 85%  │ │  ← 顶部 OSD: 录像状态 + 时长 + 电池
│ │                                     │ │
│ │                                     │ │
│ │          [ 录像画面 ]                │ │
│ │                                     │ │
│ │                                     │ │
│ │  2024-06-02 15:30:00               │ │  ← 底部 OSD: 日期时间
│ │  DEV-001  OFFICER-00001            │ │  ← 设备ID + 警员编号
│ │  LAT:22.5432 LON:113.8765          │ │  ← GPS 坐标
│ │  5G ▂▄▆█  2.4MB/s                  │ │  ← 网络类型 + 码率
│ └─────────────────────────────────────┘ │
└─────────────────────────────────────────┘

OSD 叠加内容:
  - 录像图标 + 已录时长 (REC [HH:MM:SS])
  - 电池电量图标 + 百分比
  - 日期 + 时间 (YYYY-MM-DD HH:MM:SS)
  - 设备编号 (DEV-001)
  - 警员编号 (OFFICER-00001)
  - GPS 经纬度 (LAT:xx.xxxx LON:xx.xxxx)
  - 网络状态 (5G / WiFi 图标 + 实时码率)
  - 分辨率/帧率角标 (如 4K20)
```

### 7.7 音频记录 (WAV/MP3)

```
┌──────────────────────────────────────────────────────────┐
│                   音频记录管线                             │
│                                                           │
│  [MEMS MIC] → [Audio Codec] → [AI Dev 0]                  │
│                                      │                    │
│                          ┌───────────┼───────────┐       │
│                          ▼           ▼           ▼       │
│                     录音模式选择                       │   │
│                          │           │           │       │
│                    ┌─────┴───┐ ┌─────┴───┐ ┌─────┴───┐  │
│                    │ WAV/PCM │ │  MP3    │ │ AAC     │  │
│                    │ 无损    │ │ 有损    │ │ (录像时) │  │
│                    └─────┬───┘ └─────┬───┘ └─────┬───┘  │
│                          │           │           │       │
│                          ▼           ▼           ▼       │
│                    [File Writer] [File Writer] [AVS]    │
│                                                           │
│  WAV 参数:                                                │
│    - 采样率: 16KHz / 48KHz 可选                           │
│    - 位深: 16-bit                                        │
│    - 声道: 1 (Mono)                                      │
│    - 编码: PCM 未压缩                                    │
│                                                           │
│  MP3 参数:                                                │
│    - 采样率: 16KHz / 48KHz 可选                           │
│    - 码率: 64Kbps / 128Kbps / 192Kbps 可选                │
│    - 声道: 1 (Mono)                                      │
│    - 编码: MPEG-1 Layer 3                                │
│                                                           │
│  录音模式:                                                │
│    - 录像同步录音 (AAC, 与视频一起封装在 MP4)             │
│    - 独立录音 (WAV / MP3, 不录视频时使用)                │
│    - 音频文件路径: /mnt/sdcard/Audio/                     │
└──────────────────────────────────────────────────────────┘
```

### 7.8 网络传输架构

```
┌──────────────────────────────────────────────────────────────┐
│                     网络传输架构                               │
│                                                               │
│  ┌────────────────────────────────────────────────────────┐  │
│  │                 Network Manager                         │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────────┐     │  │
│  │  │ 5G Stack │  │WiFi Stack│  │  Network Switch   │     │  │
│  │  │ (USB RNDIS│  │ (SDIO    │  │  WiFi 优先 → 5G  │     │  │
│  │  │  /ECM)   │  │  wpa_sup)│  │  自动切换 + 回切  │     │  │
│  │  └────┬─────┘  └────┬─────┘  └──────────────────┘     │  │
│  └───────┼─────────────┼─────────────────────────────────┘  │
│          │             │                                      │
│          ▼             ▼                                      │
│  ┌────────────────────────────────────────────────────────┐  │
│  │                  Stream Manager                         │  │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐  │  │
│  │  │  RTSP    │ │  RTMP    │ │  ONVIF   │ │ GB28181  │  │  │
│  │  │  Server  │ │  Client  │ │  Service │ │  Client  │  │  │
│  │  │  port 554│ │  Push    │ │  WS-Dis- │ │  SIP UA  │  │  │
│  │  │          │ │          │ │  covery  │ │          │  │  │
│  │  └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘  │  │
│  └───────┼─────────────┼─────────────┼─────────────┼──────┘  │
│          │             │             │             │          │
│          ▼             ▼             ▼             ▼          │
│  ┌──────────────────────────────────────────────────────┐    │
│  │    Stream Source (VENC GetStream / 本地文件回放)       │    │
│  └──────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────┘
```

#### RTSP Server

```
- 基于 live555 或自研轻量 RTSP 库
- 支持 TCP/UDP 传输
- SDP 描述: H.264/H.265 video + AAC audio
- 最大 4 路并发客户端
- Digest Authentication
- URL: rtsp://device_ip:554/live/main  (主码流)
       rtsp://device_ip:554/live/sub   (子码流)
       rtsp://device_ip:554/playback/xxx.mp4  (远程回放)
```

#### RTMP Push

```
- 基于 librtmp / 自研 RTMP 封装
- 推流 URL: rtmp://server_ip:1935/live/stream_key
- 支持 H.264 + AAC
- 断线重连: 指数退避 1s→2s→4s→...→60s
```

#### ONVIF 服务

```
ONVIF Profile S (实时流) + Profile G (录像回放):

Web Services (SOAP/XML):
  ├── Device Service (WS-Discovery, GetDeviceInformation, GetSystemDateAndTime)
  ├── Media Service (GetProfiles, GetStreamUri, GetSnapshotUri)
  ├── PTZ Service (数字 PTZ)
  ├── Recording Service (录像查询/回放)
  └── Event Service (事件通知: 移动侦测等)

WS-Discovery:
  - 组播 239.255.255.250:3702
  - 响应 Probe 请求
  - 宣告 Hello/Bye
```

#### GB/T 28181

```
SIP 协议栈 (基于 eXosip/osip):

流程:
  ① REGISTER → 平台 (SIP Server:5060)
  ② 200 OK ← 平台
  ③ MESSAGE (KeepAlive) → 60s 间隔心跳
  ④ INVITE (含 SDP) ← 平台请求推流
  ⑤ 200 OK (设备 SDP) → 平台
  ⑥ RTP Stream → PS 封装推流 (H.264 + G.711A)
  ⑦ BYE → 停止推流

设备 ID: 34020000001110000001 (20位国标编码)
```

### 7.9 存储管理

```
文件命名规范:
  /mnt/sdcard/
  ├── Video/          # 录像文件
  │   ├── DEV001_20240602_153000_001.mp4  (设备ID_日期_时间_序号)
  │   └── DEV001_20240602_153000_001.idx  (索引/签名)
  ├── Photo/          # 拍照文件
  │   └── DEV001_20240602_153500_001.jpg
  ├── Audio/          # 录音文件
  │   └── DEV001_20240602_160000_001.wav   (.wav 或 .mp3)
  ├── Log/            # 系统日志
  ├── Config/         # 配置文件
  │   └── device.conf
  └── DB/             # SQLite 数据库
      └── media.db

循环覆盖:
  - SD 卡剩余 < 阈值 (默认 500MB) 时触发
  - 删除最旧的非锁定文件
  - 锁定/标记文件跳过
  - 已上传文件优先删除
```

### 7.10 安全加密方案

```
加密: AES-256-CTR (帧级) / AES-256-GCM (文件级)
签名: HMAC-SHA256
密钥: 设备唯一密钥, 出厂烧录 OTP/eFUSE
     → HKDF 派生 Video Key / File Key / Auth Key
.idx 文件: 记录 Video 文件的元数据 + HMAC 签名
```

---

## 8. 关键数据流

### 8.1 录像启动数据流

```
按键 → KeyEvent → MainThread
  → 状态机: STANDBY → RECORDING
  → MediaRecordThread 唤醒:
    ① 生成文件路径 (GenerateVideoPath)
    ② 创建 .tmp 文件
    ③ 配置 VI→ISP→VPSS→EIS→VENC→AVS 管线
    ④ 启动 AI→AENC 音频管线
    ⑤ 启动 EIS Processor (ICM-20948 数据读取)
    ⑥ 设置 OSD (Region)
    ⑦ 从预录缓冲区写缓存帧到文件头
    ⑧ 主循环:
       ├── VENC GetStream
       ├── EIS 裁剪参数更新
       ├── AES 加密
       ├── AVS Muxer 写入
       ├── 子码流推流 (RTSP/RTMP/ONVIF/GB28181)
       ├── 检查分段/延录/暂停/定时
       └── 检查 SD 卡空间
    ⑨ 停止:
       ├── 刷新 AVS Buffer
       ├── 计算 HMAC → 写 .idx
       ├── .tmp → .mp4 重命名
       └── SQLite INSERT
```

### 8.2 EIS 防抖数据流

```
ICM-20948 → I2C (200Hz) → EIS Processor Thread
  ┌────────────────────────────────┐
  │ 环形缓冲区 (Gyro Ring Buffer)   │
  │ 保存最近 100ms 的角速度数据     │
  └───────────────┬────────────────┘
                  │
    每帧 (20/30/60fps) 触发:
      ① 读取环形缓冲区中上一帧到当前帧的 Gyro 数据
      ② 积分计算角度偏移
      ③ 卡尔曼滤波平滑
      ④ 像素偏移换算 (dx, dy)
      ⑤ 更新 VPSS Crop 窗口
      ⑥ 限幅: 确保 Crop 窗口在有效范围内
```

### 8.3 5G/WiFi 切换数据流

```
WiFi Monitor ──▶ 信号强度检测
                       │
              ┌────────┼────────┐
              ▼        ▼        ▼
          强(>-60dBm) 中      弱(<-80dBm)
              │        │        │
              ▼        ▼        ▼
          WiFi 优先  保持当前   切换到 5G
              │                 │
              ▼                 ▼
         usb0 DOWN          usb0 UP
       wlan0 UP            wlan0 DOWN
              │                 │
              ▼                 ▼
       低延迟推流         限制子码流推流
```

---

## 9. 系统配置参数

```ini
# /mnt/sdcard/Config/device.conf

[System]
device_id = DEV001
police_id = 000001
language = zh_CN
timezone = Asia/Shanghai

[Record]
# 分辨率: 3840x2160 / 3072x2048 / 1920x1080 / 1280x720
resolution = 1920x1080
# 帧率: 20 / 30 / 60
fps = 30
# 编码: h264 / h265
codec = h265
bitrate = 4096               # Kbps
sub_bitrate = 512
pre_record_duration = 10     # 预录: 0/5/10/15/30 秒
overrun_duration = 5         # 延录: 0/5/10/30 秒
segment_duration = 300       # 分段: 60/180/300/600 秒, 0=关闭
timed_record_enable = off    # 定时录像
timed_start = 08:00
timed_duration = 60          # 分钟

[Photo]
resolution = 8000x6000       # 48MP 全像素
quality = 95                 # JPEG 质量 70-100
burst_count = 3              # 连拍: 1/3/5/10
timestamp_stamp = on         # 照片时间戳水印

[Audio]
# 录音格式: wav / mp3
record_format = wav
sample_rate = 48000          # 8000 / 16000 / 48000
mp3_bitrate = 128            # Kbps (仅 MP3 模式)
mic_gain = 80                # 0-100
speaker_volume = 70

[EIS]
eis_enable = on
eis_strength = medium        # low / medium / high
eis_crop_margin = 10         # 裁剪边距百分比

[Network]
wifi_mode = sta
wifi_ssid =
wifi_password =
rtmp_url = rtmp://server/live/stream
rtsp_port = 554
rtsp_max_clients = 4
onvif_enable = on
gb28181_enable = on
gb28181_sip_id = 34020000001110000001
gb28181_sip_server = 192.168.1.100
gb28181_sip_port = 5060

[Storage]
loop_record = on
reserved_space = 500         # 保留空间 MB

[Display]
brightness = 80
screen_off_timeout = 30      # 秒

[Power]
low_battery_threshold = 15   # %
auto_sleep_timeout = 300     # 秒
```

---

## 10. 核心接口定义（C++ 抽象）

### 10.1 EIS 防抖接口

```cpp
struct EisFrameData {
    float gyro_x;              // 角速度 rad/s
    float gyro_y;
    float gyro_z;
    float accel_x;             // 加速度 m/s²
    float accel_y;
    float accel_z;
    uint64_t timestamp_us;     // 时间戳 (微秒)
};

struct EisCropWindow {
    int x;                     // 裁剪起始 X
    int y;                     // 裁剪起始 Y
    int width;                 // 裁剪宽度
    int height;                // 裁剪高度
};

class IEisProcessor {
public:
    virtual ~IEisProcessor() = default;
    
    /** 初始化 EIS, 设置输入/输出分辨率 */
    virtual int Init(int input_w, int input_h, int output_w, int output_h) = 0;
    
    /** 输入一帧陀螺仪数据 */
    virtual int PushGyroData(const EisFrameData& data) = 0;
    
    /** 获取当前帧的裁剪窗口 (供 VPSS 使用) */
    virtual EisCropWindow GetCropWindow() = 0;
    
    /** 启动/停止 EIS 处理 */
    virtual int Start() = 0;
    virtual int Stop() = 0;
    
    /** 设置防抖强度 */
    virtual int SetStrength(int level) = 0;  // 1-100
};
```

### 10.2 媒体管线接口

```cpp
struct RecordConfig {
    int width;
    int height;
    int fps;
    int bitrate;               // Kbps
    PayloadType codec;         // H.264 / H.265
    RcMode rc_mode;            // CBR / VBR
    bool eis_enable;
    int pre_record_sec;
    int segment_sec;
    char file_path[256];
};

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
};
```

### 10.3 网络推流接口

```cpp
class IStreamServer {
public:
    virtual ~IStreamServer() = default;

    virtual int StartRtspServer(int port) = 0;
    virtual int StartRtmpPush(const char* url) = 0;
    virtual int StartOnvifService() = 0;
    virtual int StartGb28181(const Gb28181Config& cfg) = 0;

    virtual int PushVideoFrame(const uint8_t* data, size_t len,
                                uint64_t pts, bool is_key) = 0;
    virtual int PushAudioFrame(const uint8_t* data, size_t len, uint64_t pts) = 0;

    virtual int StopAll() = 0;
};
```

### 10.4 ICM-20948 HAL 接口

```cpp
class IIcm20948Hal {
public:
    virtual ~IIcm20948Hal() = default;

    /** 初始化 (I2C 地址: 0x68 或 0x69) */
    virtual int Init(const char* i2c_dev, uint8_t addr) = 0;

    /** 设置输出数据速率 (ACC/GYRO) */
    virtual int SetOdr(uint16_t acc_odr_hz, uint16_t gyro_odr_hz) = 0;

    /** 设置量程 */
    virtual int SetAccelRange(int g) = 0;   // 2/4/8/16
    virtual int SetGyroRange(int dps) = 0;  // 250/500/1000/2000

    /** 启动/停止数据流 */
    virtual int Start() = 0;
    virtual int Stop() = 0;

    /** 读取传感器数据 (阻塞/非阻塞) */
    virtual int ReadData(EisFrameData* data, int timeout_ms) = 0;

    /** 校准 */
    virtual int Calibrate() = 0;
};
```

---

## 11. 编译与构建系统

### CMake 顶层配置

```cmake
cmake_minimum_required(VERSION 3.10)
project(leavr C CXX)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_DIR /opt/hisi-linux/arm-himix-linux)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_DIR}/bin/arm-himix-linux-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_DIR}/bin/arm-himix-linux-g++)

set(HISDK_DIR /opt/hisi-sdk/hi3516)
set(MPP_INCLUDE ${HISDK_DIR}/include)
set(MPP_LIB ${HISDK_DIR}/lib)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Werror")

find_library(MPI_LIB     libmpi.so     ${MPP_LIB})
find_library(LIVE555_LIB liblive555.so ${HISDK_DIR}/third_party)
find_library(SQLITE3_LIB libsqlite3.so ${HISDK_DIR}/third_party)
find_library(MBEDTLS_LIB libmbedtls.so ${HISDK_DIR}/third_party)
find_library(LVGL_LIB    liblvgl.so    ${HISDK_DIR}/third_party)
find_library(MP3LAME_LIB libmp3lame.so ${HISDK_DIR}/third_party)

add_subdirectory(src)
```

### 构建命令

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## 12. 线程间通信机制

| 场景 | 机制 | 说明 |
|------|------|------|
| 按键事件 | SPSC 无锁队列 | 低延迟 (<1ms) |
| EIS 陀螺仪数据 | 共享环形缓冲区 + 信号量 | 零拷贝, 200Hz 高频 |
| 录像帧传输 | 共享环形缓冲区 + 信号量 | 零拷贝, 高吞吐 |
| GPS 数据 | atomic + 双缓冲 | 读写分离 |
| 配置更新 | POSIX Message Queue | 持久化 |
| 日志写入 | Lock-free Queue → 批量 flush | 减少 IO |
| 文件操作 | Mutex + CondVar | 临界区保护 |

---

## 13. 项目源码结构

```
leavr/
├── CMakeLists.txt                          # 顶层 CMake
├── toolchain/
│   └── arm-himix-linux.cmake               # 工具链定义
├── src/
│   ├── CMakeLists.txt
│   ├── app/                                # 应用层
│   │   ├── main.cpp                        # 入口
│   │   ├── state_machine.h/cpp             # 主状态机
│   │   └── event_dispatcher.h/cpp           # 事件分发
│   ├── media/                              # 媒体框架
│   │   ├── pipeline/
│   │   │   ├── record_pipeline.h/cpp       # 录像管线 (含 EIS)
│   │   │   ├── capture_pipeline.h/cpp      # 拍照管线
│   │   │   └── playback_pipeline.h/cpp     # 回放管线
│   │   ├── eis/
│   │   │   ├── eis_processor.h/cpp         # EIS 防抖核心算法
│   │   │   └── kalman_filter.h/cpp         # 卡尔曼滤波器
│   │   ├── vi_manager.h/cpp                # VI 管理
│   │   ├── vpss_manager.h/cpp              # VPSS 管理 (含 EIS 裁剪)
│   │   ├── venc_manager.h/cpp              # VENC 管理
│   │   ├── audio_manager.h/cpp             # 音频管理
│   │   ├── avs_muxer.h/cpp                 # MP4 Muxer
│   │   └── osd_manager.h/cpp               # OSD 叠加
│   ├── storage/                            # 存储管理
│   │   ├── storage_manager.h/cpp
│   │   ├── file_indexer.h/cpp              # SQLite
│   │   ├── pre_record_buffer.h/cpp         # 预录环形缓冲
│   │   └── recycle_strategy.h/cpp
│   ├── network/                            # 网络服务
│   │   ├── rtsp_server.h/cpp
│   │   ├── rtmp_client.h/cpp
│   │   ├── onvif_service.h/cpp             # ONVIF WS-Discovery + SOAP
│   │   ├── gb28181_client.h/cpp            # GB28181 SIP UA
│   │   ├── http_uploader.h/cpp
│   │   ├── stream_manager.h/cpp            # 推流统一管理
│   │   └── network_manager.h/cpp           # 5G/WiFi 管理切换
│   ├── security/                           # 安全加密
│   │   ├── encryptor.h/cpp
│   │   ├── key_manager.h/cpp
│   │   └── signature.h/cpp
│   ├── hal/                                # 硬件抽象层
│   │   ├── imx586_hal.h/cpp                # IMX586 Sensor HAL
│   │   ├── icm20948_hal.h/cpp              # ICM-20948 陀螺仪 HAL
│   │   ├── audio_hal.h/cpp                 # Audio HAL
│   │   ├── gps_hal.h/cpp                   # GPS HAL
│   │   ├── spi_lcd_hal.h/cpp               # SPI LCD HAL
│   │   ├── key_hal.h/cpp                   # 按键 HAL
│   │   ├── led_hal.h/cpp                   # LED HAL
│   │   ├── usb_hal.h/cpp                   # USB HAL
│   │   ├── wifi_hal.h/cpp                  # WiFi HAL
│   │   └── modem_5g_hal.h/cpp              # 5G 模组 HAL (AT + 拨号)
│   ├── ui/                                 # UI (LVGL)
│   │   ├── display.h/cpp
│   │   └── menu_manager.h/cpp
│   └── utils/                              # 工具类
│       ├── logger.h/cpp
│       ├── config_parser.h/cpp
│       ├── watchdog.h/cpp
│       ├── ring_buffer.h/cpp
│       ├── timer.h/cpp
│       └── thread_pool.h/cpp
├── third_party/                            # 第三方库
│   ├── lvgl/                               # LVGL 图形库
│   ├── sqlite3/                            # SQLite3
│   ├── mbedtls/                            # mbedTLS
│   ├── cjson/                              # cJSON
│   ├── libmp4v2/                           # MP4 封装
│   ├── libmp3lame/                         # MP3 编码
│   └── gsoap/                              # ONVIF SOAP 框架
├── config/                                 # 配置文件
│   ├── device.conf
│   └── osd_font.bin
├── scripts/                                # 脚本
│   ├── build.sh
│   ├── flash.sh
│   └── sdcard_prepare.sh
└── docs/                                   # 文档
    ├── software_architecture.md
    └── api_reference.md
```

---

## 14. 项目实施计划与里程碑

### 14.1 阶段划分

| 阶段 | 内容 | 工作量 | 里程碑 |
|------|------|--------|--------|
| **阶段 1: 系统移植** | U-Boot + Kernel 4.9 + RootFS 构建 | 2 周 | 系统启动、串口登录 |
| **阶段 2: 驱动移植** | IMX586 / ICM-20948 / 5G / WiFi / SPI LCD / Audio | 4 周 | 所有外设驱动可用 |
| **阶段 3: 媒体框架** | VI/VPSS/VENC/AVS 管线 + EIS 防抖 | 4 周 | 可录像/拍照 |
| **阶段 4: 应用功能** | 状态机/预录/延录/分段/暂停/定时/OSD | 3 周 | 全功能录像 |
| **阶段 5: 网络传输** | RTSP/RTMP/ONVIF/GB28181 | 4 周 | 多协议推流 |
| **阶段 6: 音频/安全** | WAV/MP3 录音 + AES 加密 + 签名 | 2 周 | 音频+加密 |
| **阶段 7: 集成测试** | 全功能联调、稳定性、功耗测试 | 3 周 | 样机验收 |
| **阶段 8: 认证准备** | GB28181 认证 / ONVIF 兼容性 / CCC | 4 周 | 通过认证 |

### 14.2 详细任务拆解

#### 阶段 1: 系统移植 (2 周)

| # | 任务 | 负责人 | 工时 | 依赖 |
|---|------|--------|------|------|
| 1.1 | 搭建交叉编译环境 | FW | 0.5d | - |
| 1.2 | 获取海思 SDK (uboot + kernel + mpp) | FW | 0.5d | - |
| 1.3 | U-Boot 移植 (DDR/Flash/启动参数) | FW | 2d | 1.2 |
| 1.4 | Kernel 4.9 基础配置 (defconfig) | FW | 1d | 1.2 |
| 1.5 | 根文件系统构建 (BusyBox + 库) | FW | 2d | 1.4 |
| 1.6 | 系统启动脚本 + 自启动 | FW | 1d | 1.5 |
| 1.7 | 整机启动验证 | FW | 1d | 1.3-1.6 |

#### 阶段 2: 驱动移植 (4 周)

| # | 任务 | 负责人 | 工时 | 依赖 |
|---|------|--------|------|------|
| 2.1 | IMX586 Sensor MIPI 驱动 + ISP 3A | FW | 3d | 1.4 |
| 2.2 | ICM-20948 I2C 驱动 | FW | 2d | 1.4 |
| 2.3 | 5G 模组 USB 驱动 (RNDIS/ECM) + AT 命令 | FW | 3d | 1.4 |
| 2.4 | WiFi SDIO 驱动 (AP6256 等) | FW | 3d | 1.4 |
| 2.5 | SPI LCD 驱动 (framebuffer) | FW | 2d | 1.4 |
| 2.6 | Audio Codec 驱动 (I2S MIC/SPK) | FW | 1.5d | 1.4 |
| 2.7 | GPS UART 驱动 + NMEA 解析 | FW | 1.5d | 1.4 |
| 2.8 | GPIO 按键/LED/背光驱动 | FW | 1d | 1.4 |
| 2.9 | 所有外设驱动联调 | FW | 3d | 2.1-2.8 |

#### 阶段 3: 媒体框架 (4 周)

| # | 任务 | 负责人 | 工时 | 依赖 |
|---|------|--------|------|------|
| 3.1 | VI 采集管理 (IMX586 多模式) | SW | 2d | 2.1 |
| 3.2 | VPSS 管理 (缩放/裁剪/EIS 接口) | SW | 2d | 3.1 |
| 3.3 | VENC 管理 (H.264/H.265/JPEG) | SW | 2d | 3.2 |
| 3.4 | EIS 防抖算法实现 (卡尔曼滤波 + 裁剪) | SW | 5d | 2.2, 3.2 |
| 3.5 | EIS 与 VPSS 联调稳像效果 | SW | 2d | 3.4 |
| 3.6 | AVS Muxer (MP4 封装) | SW | 2d | 3.3 |
| 3.7 | 拍照管线 (48MP JPEG) | SW | 2d | 3.2 |
| 3.8 | 媒体管线联调 (录像 + 拍照可用) | SW | 3d | 3.1-3.7 |

#### 阶段 4: 应用功能 (3 周)

| # | 任务 | 负责人 | 工时 | 依赖 |
|---|------|--------|------|------|
| 4.1 | 主状态机实现 | SW | 2d | 3.8 |
| 4.2 | 配置文件解析 (INI parser) | SW | 1d | 4.1 |
| 4.3 | OSD 叠加 (时间/ID/GPS/网速) | SW | 2d | 3.8 |
| 4.4 | 预录实现 (环形缓冲区) | SW | 2d | 3.8 |
| 4.5 | 延录/分段/暂停/定时录像 | SW | 3d | 4.4 |
| 4.6 | 本地回放 (VDEC + 文件列表) | SW | 2d | 3.8 |
| 4.7 | SQLite 文件索引 + 循环覆盖 | SW | 2d | 4.1 |
| 4.8 | UI (LVGL: 状态栏/菜单/设置) | SW | 3d | 2.5 |
| 4.9 | 全功能业务联调 | SW | 3d | 4.1-4.8 |

#### 阶段 5: 网络传输 (4 周)

| # | 任务 | 负责人 | 工时 | 依赖 |
|---|------|--------|------|------|
| 5.1 | 5G 拨号管理 (PPP/RNDIS) + AT 指令 | SW | 2d | 2.3 |
| 5.2 | WiFi 连接管理 (wpa_supplicant) | SW | 2d | 2.4 |
| 5.3 | 网络状态监测 + 5G/WiFi 自动切换 | SW | 2d | 5.1, 5.2 |
| 5.4 | RTSP Server 实现 | SW | 3d | 3.8 |
| 5.5 | RTMP Push 实现 | SW | 2d | 3.8 |
| 5.6 | ONVIF Service (WS-Discovery + Media + Recording) | SW | 5d | 5.4 |
| 5.7 | GB28181 SIP UA (注册+推流+保活) | SW | 5d | 3.8 |
| 5.8 | HTTP 文件上传 (断点续传) | SW | 2d | 5.1, 5.2 |
| 5.9 | 网络协议联调 | SW | 3d | 5.4-5.8 |

#### 阶段 6: 音频/安全 (2 周)

| # | 任务 | 负责人 | 工时 | 依赖 |
|---|------|--------|------|------|
| 6.1 | WAV/PCM 录音实现 | SW | 2d | 2.6 |
| 6.2 | MP3 编码录音 (libmp3lame) | SW | 2d | 2.6 |
| 6.3 | AAC 编码 (录像时) | SW | 1d | 2.6 |
| 6.4 | AES-256-CTR 加密模块 | SW | 2d | 3.8 |
| 6.5 | HMAC-SHA256 签名模块 | SW | 1.5d | 6.4 |
| 6.6 | .idx 文件生成与验证 | SW | 1.5d | 6.5 |
| 6.7 | 音频+加密联调 | SW | 2d | 6.1-6.6 |

#### 阶段 7: 集成测试 (3 周)

| # | 任务 | 负责人 | 工时 | 依赖 |
|---|------|--------|------|------|
| 7.1 | 全功能遍历测试 | QA | 5d | 全部 |
| 7.2 | 功耗测试 (录像/待机/推流) | HW+SW | 3d | 全部 |
| 7.3 | 稳定性测试 (7×24h 录像) | QA | 7d | 全部 |
| 7.4 | 高低温测试 (-20℃~60℃) | HW+SW | 3d | 全部 |
| 7.5 | EIS 防抖效果评测 | QA | 2d | 3.5 |
| 7.6 | 网络推流延迟/稳定性测试 | QA | 3d | 5.9 |
| 7.7 | Bug 修复 | SW | 5d | 7.1-7.6 |

#### 阶段 8: 认证 (4 周)

| # | 任务 | 负责人 | 工时 | 依赖 |
|---|------|--------|------|------|
| 8.1 | GB28181 标准符合性测试 | SW | 5d | 5.7 |
| 8.2 | ONVIF Profile S/G 兼容性测试 | SW | 5d | 5.6 |
| 8.3 | CCC/CE/FCC 认证准备 | HW+SW | 10d | 阶段7 |

### 14.3 甘特图概览

```
月份:         M1         M2         M3         M4         M5
周:     1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20
─────────────────────────────────────────────────────────────────
阶段1:  ████████
阶段2:     ████████████████
阶段3:              ████████████████
阶段4:                       ████████████
阶段5:                            ████████████████
阶段6:                                        ████████
阶段7:                                             ████████████
阶段8:                                                   ████████████████
─────────────────────────────────────────────────────────────────
里程碑:   ▲1          ▲2          ▲3                ▲4          ▲5
   ▲1: 系统启动   ▲2: 驱动就绪   ▲3: 录像/拍照    ▲4: 协议推流  ▲5: 样机验收
```

### 14.4 风险与应对

| 风险 | 影响 | 概率 | 应对措施 |
|------|------|------|----------|
| IMX586 48MP Remosaic 性能不足 | 高 | 中 | 评估 ISP 能力, 降级为 12MP binning 拍照 |
| EIS 稳像效果不达预期 | 中 | 中 | 优化卡尔曼滤波参数, 调整裁剪边距 |
| 5G 模组兼容性问题 | 高 | 中 | 提前验证模组驱动兼容性, 备选模组 |
| ONVIF/GB28181 协议兼容性 | 中 | 高 | 使用成熟开源库 (gSOAP/eXosip) |
| 功耗超标 (4K+5G+EIS) | 高 | 中 | DVFS 动态调频、编码参数动态调整 |
| 海思 SDK 资料不完整 | 高 | 中 | 通过代理商获取完整文档 + FAE 支持 |

---

> **文档版本**: v2.0  
> **最后更新**: 2024-06-02  
> **维护者**: LEAVR 项目组