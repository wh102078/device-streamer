#pragma once

// 编译期配置：改参数只需要动这个文件。
namespace config {

inline constexpr const char* kDevice = "/dev/video0";
inline constexpr const char* kDefaultUrl = "rtsp://127.0.0.1:8554/dev";

inline constexpr unsigned kWidth = 640;
inline constexpr unsigned kHeight = 480;
inline constexpr unsigned kFps = 30;

inline constexpr int kBitrate = 2000000;   // bps
inline constexpr unsigned kGop = 30;       // 关键帧间隔

inline constexpr unsigned kQueueCapacity = 3;  // 采集→编码队列容量
inline constexpr unsigned kV4l2Buffers = 4;    // V4L2 mmap 缓冲块数

}  // namespace config
