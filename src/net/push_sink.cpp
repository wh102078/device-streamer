#include "net/push_sink.h"

#include <utility>

#include "common/stop_flag.h"

PushSink::PushSink(std::string url, AVCodecContext* enc, std::size_t capacity)
    : url_(std::move(url)),
      enc_(enc),
      queue_(capacity, PacketQueue::Policy::DropOldest) {}

PushSink::~PushSink() {
    stop();
}

bool PushSink::start() {
    if (!pusher_.open(url_.c_str(), enc_)) {
        return false;
    }
    thread_ = std::thread(&PushSink::run, this);
    return true;
}

void PushSink::enqueue(AVPacket* pkt) {
    queue_.push(pkt);
}

void PushSink::stop() {
    queue_.stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool PushSink::take_keyframe_request() {
    return keyframe_requested_.exchange(false);
}

void PushSink::run() {
    unsigned attempts = 0;

    while (true) {
        AVPacket* pkt = nullptr;
        if (!queue_.pop(&pkt, 100)) {
            if (queue_.stopped()) {
                break;
            }
            continue;
        }

        const int size = pkt->size;
        if (pusher_.write(pkt)) {
            packets_.fetch_add(1);
            bytes_.fetch_add(static_cast<uint64_t>(size));
        } else {
            av_packet_unref(pkt);
            double recovery_ms = 0;
            if (pusher_.reconnect(url_.c_str(), enc_,
                                  [] { return stopflag::requested(); },
                                  &attempts, &recovery_ms)) {
                keyframe_requested_.store(true);
                last_recovery_ms_.store(recovery_ms);
                reconnects_.store(attempts);
            }
        }

        av_packet_free(&pkt);
    }
}
