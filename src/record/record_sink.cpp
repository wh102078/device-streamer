#include "record/record_sink.h"

#include <cstdio>
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
    unsigned consecutive_errors = 0;

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
            if (consecutive_errors > 0) {
                std::fprintf(stderr, "recording write recovered after %u failures\n",
                             consecutive_errors);
                consecutive_errors = 0;
            }
            frames_.fetch_add(1);
            bytes_.fetch_add(static_cast<uint64_t>(size));
        } else {
            ++consecutive_errors;
            const unsigned total = write_errors_.fetch_add(1) + 1;
            // 首次失败和每 100 次连续失败各打印一次，避免刷屏
            if (consecutive_errors == 1 || consecutive_errors % 100 == 0) {
                std::fprintf(stderr,
                             "recording write failed (%u consecutive, %u total)\n",
                             consecutive_errors, total);
            }
            // 继续消费队列但不再写文件；若不消费，Block 队列会阻塞主线程
        }
        av_packet_free(&pkt);
    }
}
