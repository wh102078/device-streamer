#include "frame_queue.h"

void LatestFrameQueue::push(QueuedFrame frame) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (items_.size() >= capacity_) {
            items_.erase(items_.begin());  // 丢最旧
            ++dropped_;
        }
        items_.push_back(std::move(frame));
    }
    cv_.notify_one();
}

bool LatestFrameQueue::pop(QueuedFrame* out, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    // wait_for 会释放锁并睡眠；被 notify 或超时后重新持锁
    const bool ready = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                    [this] { return !items_.empty(); });
    if (!ready) {
        return false;
    }
    *out = std::move(items_.front());
    items_.erase(items_.begin());
    return true;
}

std::size_t LatestFrameQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_.size();
}

unsigned LatestFrameQueue::dropped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}
