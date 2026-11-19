#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "queue/packet_queue.h"
#include "net/rtsp_pusher.h"

// 推流线程：队列满时丢最旧帧；发送失败在本线程内重连，不阻塞编码。
class PushSink {
public:
    PushSink(std::string url, AVCodecContext* enc, std::size_t capacity);
    ~PushSink();
    PushSink(const PushSink&) = delete;
    PushSink& operator=(const PushSink&) = delete;

    bool start();
    void enqueue(AVPacket* pkt);
    void stop();

    bool take_keyframe_request();

    uint64_t packets() const { return packets_.load(); }
    uint64_t bytes() const { return bytes_.load(); }
    unsigned reconnects() const { return reconnects_.load(); }
    double last_recovery_ms() const { return last_recovery_ms_.load(); }
    unsigned dropped() const { return queue_.dropped(); }

private:
    void run();

    std::string url_;
    AVCodecContext* enc_;
    PacketQueue queue_;
    RtspPusher pusher_;
    std::thread thread_;
    std::atomic<bool> keyframe_requested_{false};
    std::atomic<uint64_t> packets_{0};
    std::atomic<uint64_t> bytes_{0};
    std::atomic<unsigned> reconnects_{0};
    std::atomic<double> last_recovery_ms_{0};
};
