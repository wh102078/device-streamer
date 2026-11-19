#include "queue/frame_queue.h"

void LatestFrameQueue::push(QueuedFrame frame) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            return;  // 已终止，不再接收新帧
        }
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
    const bool ready = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                    [this] { return !items_.empty() || stopped_; });
    if (!ready) {
        return false;  // 超时
    }

    if (items_.empty()) {
        return false;
    }

    *out = std::move(items_.front());
    items_.erase(items_.begin());
    return true;
}

void LatestFrameQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    cv_.notify_all();  // 唤醒所有等待者
}

bool LatestFrameQueue::stopped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stopped_;
}

std::size_t LatestFrameQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_.size();
}

unsigned LatestFrameQueue::dropped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}
