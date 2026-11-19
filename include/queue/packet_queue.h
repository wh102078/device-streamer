#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

// 编码包队列。push 接管 AVPacket，pop 交给调用方释放。
// DropOldest：满时丢最旧（推流）；Block：满时阻塞生产者（录像）。
class PacketQueue {
public:
    enum class Policy { DropOldest, Block };

    PacketQueue(std::size_t capacity, Policy policy);
    ~PacketQueue();
    PacketQueue(const PacketQueue&) = delete;
    PacketQueue& operator=(const PacketQueue&) = delete;

    bool push(AVPacket* pkt);
    bool pop(AVPacket** out, int timeout_ms);
    void stop();
    bool stopped() const;
    unsigned dropped() const;
    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::vector<AVPacket*> items_;
    std::size_t capacity_;
    Policy policy_;
    bool stopped_ = false;
    unsigned dropped_ = 0;
};
