#pragma once

#include <linux/videodev2.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

// data 在 release() 之前有效。
struct CapturedFrame {
    const uint8_t* data = nullptr;
    size_t size = 0;            // 这一帧实际字节数（bytesused）
    unsigned sequence = 0;      // 驱动给的帧序号，跳号 = 丢帧
    unsigned index = 0;         // 用的是第几块缓冲
    uint64_t timestamp_us = 0;  // 驱动时间戳（微秒），用于生成正确的 PTS
};

class V4L2Capture {
public:
    V4L2Capture() = default;
    ~V4L2Capture();
    V4L2Capture(const V4L2Capture&) = delete;
    V4L2Capture& operator=(const V4L2Capture&) = delete;

    bool open(const char* device, unsigned width, unsigned height, uint32_t pixel_format,
              unsigned buffer_count = 4);
    bool start();
    void stop();
    void close();

    // 坏帧内部丢弃并重试；返回 false 表示超时或设备错误。
    bool grab(CapturedFrame* out);
    void release();

    // 采集线程写、主线程读
    unsigned rejected() const { return rejected_.load(); }

private:
    struct Buffer {
        void* start = nullptr;
        size_t length = 0;
    };

    static constexpr unsigned kMaxBuffers = 4;
    static constexpr unsigned kMaxGrabAttempts = 5000;

    enum class DequeueResult { Ok, Again, Error };

    bool wait_for_frame();
    DequeueResult dequeue(struct v4l2_buffer* buf);
    bool queue(unsigned index);

    int fd_ = -1;
    Buffer buffers_[kMaxBuffers];
    unsigned buffer_count_ = 0;
    bool streaming_ = false;
    int held_index_ = -1;   // 当前拿在手里、还没还给驱动的缓冲
    std::atomic<unsigned> rejected_{0};
};
