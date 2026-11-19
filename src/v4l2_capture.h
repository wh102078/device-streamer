#pragma once

#include <linux/videodev2.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

// 一帧采集结果。data 指向 mmap 出来的驱动缓冲，
// 在调用 release() 之前有效，之后必须视为失效。
struct CapturedFrame {
    const uint8_t* data = nullptr;
    size_t size = 0;            // 这一帧实际字节数（bytesused）
    unsigned sequence = 0;      // 驱动给的帧序号，跳号 = 丢帧
    unsigned index = 0;         // 用的是第几块缓冲
    uint64_t timestamp_us = 0;  // 驱动时间戳（微秒），用于生成正确的 PTS
};

// V4L2 流式采集（mmap 模式）。
// grab()/release() 必须成对使用：grab 拿到的帧在 release 前有效。
class V4L2Capture {
public:
    V4L2Capture() = default;
    ~V4L2Capture();
    V4L2Capture(const V4L2Capture&) = delete;
    V4L2Capture& operator=(const V4L2Capture&) = delete;

    // 打开、查能力、协商格式、申请缓冲、映射、入队（还没开流）
    // 失败返回 false（错误信息已打印到 stderr），不会终止进程
    bool open(const char* device, unsigned width, unsigned height, uint32_t pixel_format);

    // STREAMON：开始出帧。失败返回 false。
    bool start();

    // STREAMOFF：停止出帧（可重复调用）
    void stop();

    // 关闭 fd 并解除映射（析构时自动调用）
    void close();

    // 取一帧校验通过的数据；坏帧会被丢弃并重试。
    // 返回 false 表示连续失败次数过多，通常是设备/相机异常。
    bool grab(CapturedFrame* out);

    // 用完帧后调用，把缓冲还给驱动。不还的话队列会被掏空、最终停流。
    void release();

    // 采集线程写、主线程读，必须是原子变量
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
