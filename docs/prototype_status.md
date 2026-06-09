# 执法记录仪原型完成度

## 当前基线

- 平台：HI3516CV610 + SC4336P，Sensor Profile 原始尺寸 2560x1440@30。
- 主码流：1920x1080@30 H.265，4096 Kbps，GOP 30。
- 子码流：1280x720@15 H.264，1024 Kbps，GOP 15。
- EIS：ICM-20948 200Hz 采样，按真实时间戳积分，30Hz 更新 VPSS 组裁剪。

## 已实现

- SYS/VB、VI、ISP、VPSS、双 VENC 初始化、绑定、取流和逆序释放。
- `SensorProfile`、`EncodedVideoFrame`、`IVideoFrameSink` 及媒体业务接口拆分。
- 录像与 RTSP 使用独立有界队列；网络慢客户端不会阻塞录像写线程。
- 主码流采用 `.tmp -> fsync -> rename` 的掉电保护流程保存 Annex-B H.265。
- `/live/sub` RTSP 服务支持 H.264 RTP、TCP interleaved 和 UDP unicast。
- ICM-20948 初始化、量程/ODR 配置、异常回退到居中裁剪。
- SIGTERM/SIGINT 有序停止录像、网络、IMU、EIS 和 MPP。
- 主机核心测试和 ARM musl 交叉编译已通过。

## 尚未完成

- 当前工具链未提供 FFmpeg/libavformat，录像文件是 `.h265` 裸流，不是 MP4。
- 当前工具链未提供 live555；RTSP 使用项目内轻量实现，尚未做 VLC/ffplay 兼容性验收。
- IMX586 MIPI、Sensor 驱动和 ISP 参数尚未适配。
- 2 小时并发稳定性、800ms 延迟、EIS 位移 RMS 降低 30% 均需在目标板实测。
- 录像写失败后的存储告警、自动分段和循环覆盖尚未接入。

## 板端验收

1. 启动 `leavr_app --record-on-boot`，确认 SC4336P、双 VENC 和 ICM-20948 日志。
2. 使用 `ffplay rtsp://<device-ip>:554/live/sub` 分别验证 TCP 和 UDP。
3. 停止录像后使用 `ffprobe` 检查 H.265 裸流分辨率、帧率、码率和 GOP。
4. 执行断网、慢客户端、拔除 IMU、存储写满、重复启停和 SIGTERM 测试。
5. 并发录像与 RTSP 持续 2 小时，记录内存、CPU、丢帧和端到端延迟。

## 简历表述

在 IMX586 上板验证前建议使用：

> 面向 Sony IMX586 设计 HI3516CV610 执法记录仪媒体架构，使用 SC4336 完成双码流采集、RTSP 传输及陀螺仪 EIS 原型验证，并预留多 Sensor Profile 适配机制。
