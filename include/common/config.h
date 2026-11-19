#pragma once

#include <cstddef>

namespace config {

inline constexpr const char* kDevice = "/dev/video0";
inline constexpr const char* kDefaultUrl = "rtsp://127.0.0.1:8554/dev";
inline constexpr const char* kRecordPath = "out/record.mp4";

inline constexpr unsigned kWidth = 640;
inline constexpr unsigned kHeight = 480;
inline constexpr unsigned kFps = 30;

inline constexpr int kBitrate = 2000000;
inline constexpr unsigned kGop = 30;

inline constexpr unsigned kQueueCapacity = 3;  // 采集->编码
inline constexpr unsigned kV4l2Buffers = 4;

inline constexpr std::size_t kPushQueue = 8;    // 编码->推流，满丢最旧
inline constexpr std::size_t kRecordQueue = 32; // 编码->录像，满则阻塞

}  // namespace config
