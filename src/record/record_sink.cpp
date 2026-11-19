#include "record/record_sink.h"

#include <utility>

RecordSink::RecordSink(std::string path, const AVCodecParameters* params,
                       AVRational time_base, std::size_t capacity)
    : path_(std::move(path)),
      params_(params),
      time_base_(time_base),
      queue_(capacity, PacketQueue::Policy::Block) {}

RecordSink::~RecordSink() {
    stop();
}

bool RecordSink::start(std::string* error) {
    if (!recorder_.open(path_, params_, time_base_, error)) {
        return false;
    }
    thread_ = std::thread(&RecordSink::run, this);
    return true;
}

void RecordSink::enqueue(AVPacket* pkt) {
    queue_.push(pkt);
}

void RecordSink::stop() {
    queue_.stop();
    if (thread_.joinable()) {
        thread_.join();
    }
    recorder_.close();
}

void RecordSink::run() {
    while (true) {
        AVPacket* pkt = nullptr;
        if (!queue_.pop(&pkt, 100)) {
            if (queue_.stopped()) {
                break;
            }
            continue;
        }

        const int size = pkt->size;
        if (recorder_.write(pkt)) {
            frames_.fetch_add(1);
            bytes_.fetch_add(static_cast<uint64_t>(size));
        }
        av_packet_free(&pkt);
    }
}
