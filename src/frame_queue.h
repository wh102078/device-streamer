#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

struct QueuedFrame {
    std::vector<uint8_t> data;
    unsigned sequence = 0;
    uint64_t timestamp_us = 0;  // 采集时间，用于生成 PTS
};

// push 不阻塞；pop 等待直到有帧或 stop()。
class LatestFrameQueue {
public:
    explicit LatestFrameQueue(std::size_t capacity) : capacity_(capacity) {}

    // 队列满时丢最旧帧
    void push(QueuedFrame frame);

    // 等待有帧或 stop()，超时返回 false
    bool pop(QueuedFrame* out, int timeout_ms);

    // 终止队列并唤醒等待者；可重复调用
    void stop();
    bool stopped() const;
    std::size_t size() const;
    unsigned dropped() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<QueuedFrame> items_;
    std::size_t capacity_;
    bool stopped_ = false;
    unsigned dropped_ = 0;
};
