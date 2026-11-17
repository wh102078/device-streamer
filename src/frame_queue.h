#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

// 队列里的一帧。数据是拷贝出来的堆内存，不依赖 V4L2 的 mmap 缓冲。
struct QueuedFrame {
    std::vector<uint8_t> data;
    unsigned sequence = 0;
};

// 有界帧队列：满了就丢最旧的一帧。
// 实时监控要的是"最新画面"，不是"每一帧都送达"，所以这里不阻塞生产者。
class LatestFrameQueue {
public:
    explicit LatestFrameQueue(std::size_t capacity) : capacity_(capacity) {}

    // 放入一帧。队列满时丢弃最旧的一帧再放入（调用方不会被阻塞）。
    void push(QueuedFrame frame);

    // 取一帧。队列空时最多等待 timeout_ms，超时返回 false。
    bool pop(QueuedFrame* out, int timeout_ms);

    // 当前积压帧数
    std::size_t size() const;

    // 因为队列满而丢弃的帧数
    unsigned dropped() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<QueuedFrame> items_;
    std::size_t capacity_;
    unsigned dropped_ = 0;
};
