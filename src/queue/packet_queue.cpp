#include "queue/packet_queue.h"

#include <chrono>

PacketQueue::PacketQueue(std::size_t capacity, Policy policy)
    : capacity_(capacity), policy_(policy) {}

PacketQueue::~PacketQueue() {
    stop();
    std::lock_guard<std::mutex> lock(mutex_);
    for (AVPacket* pkt : items_) {
        av_packet_free(&pkt);
    }
    items_.clear();
}

bool PacketQueue::push(AVPacket* pkt) {
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (policy_ == Policy::Block) {
            not_full_.wait(lock, [this] {
                return stopped_ || items_.size() < capacity_;
            });
            if (stopped_) {
                av_packet_free(&pkt);
                return false;
            }
        } else {
            if (stopped_) {
                av_packet_free(&pkt);
                return false;
            }
            if (items_.size() >= capacity_) {
                av_packet_free(&items_.front());
                items_.erase(items_.begin());
                ++dropped_;
            }
        }

        items_.push_back(pkt);
    }
    not_empty_.notify_one();
    return true;
}

bool PacketQueue::pop(AVPacket** out, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    const bool ready = not_empty_.wait_for(
        lock, std::chrono::milliseconds(timeout_ms),
        [this] { return !items_.empty() || stopped_; });
    if (!ready || items_.empty()) {
        return false;
    }

    *out = items_.front();
    items_.erase(items_.begin());
    not_full_.notify_one();
    return true;
}

void PacketQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
}

bool PacketQueue::stopped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopped_;
}

unsigned PacketQueue::dropped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}

std::size_t PacketQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_.size();
}
